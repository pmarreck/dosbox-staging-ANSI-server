// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/command_processor.h"
#include "textmode_server/keyboard_processor.h"

#include <gtest/gtest.h>

namespace {

using textmode::CommandProcessor;
using textmode::CommandResponse;
using textmode::ServiceResult;

class TextModeCommandProcessorTest : public ::testing::Test {
protected:
	static ServiceResult MakeSuccess()
	{
		return ServiceResult{true, "frame-raw\n", ""};
	}

	static ServiceResult MakeFailure()
	{
		return ServiceResult{false, "", "boom"};
	}
};

TEST_F(TextModeCommandProcessorTest, RejectsWhenServiceDisabled)
{
	CommandProcessor processor([] { return ServiceResult{false, "", "text-mode server disabled"}; });

	const auto response = processor.HandleCommand("GET");

	EXPECT_FALSE(response.ok);
	EXPECT_EQ(response.payload, "ERR text-mode server disabled\n");
}

TEST_F(TextModeCommandProcessorTest, ReturnsFrameForGet)
{
	CommandProcessor processor([] { return MakeSuccess(); });

	const auto response = processor.HandleCommand("GET");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "frame-raw\n");
}

TEST_F(TextModeCommandProcessorTest, ReturnsShowSpaceVariant)
{
	CommandProcessor processor([] {
		return ServiceResult{true, "line A B\n", ""};
	});

	const auto response = processor.HandleCommand("GET SHOWSPC");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "line·A·B\n");
}

TEST_F(TextModeCommandProcessorTest, ReportsServiceFailure)
{
	CommandProcessor processor([] { return MakeFailure(); });

	const auto response = processor.HandleCommand("GET");

	EXPECT_FALSE(response.ok);
	EXPECT_EQ(response.payload, "ERR boom\n");
}

TEST_F(TextModeCommandProcessorTest, ProducesStats)
{
	int call_count = 0;
	CommandProcessor processor([&] {
		++call_count;
		if (call_count == 1) {
			return MakeSuccess();
		}
		return MakeFailure();
	});

	processor.HandleCommand("GET");          // success
	processor.HandleCommand("GET");          // failure
	const auto response = processor.HandleCommand("STATS");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload,
	          "requests=2 success=1 failures=1\n");
}

TEST_F(TextModeCommandProcessorTest, ApplyFailsWithoutKeyboardHandler)
{
	CommandProcessor processor([] { return MakeSuccess(); });
	const auto response = processor.HandleCommand("APPLY keys=HELLO");
	EXPECT_FALSE(response.ok);
	EXPECT_EQ(response.payload, "ERR keyboard unavailable\n");
}

TEST_F(TextModeCommandProcessorTest, ApplyPressesKeysAndReturnsFrame)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		if (cmd == "PRESS Hi") {
			return CommandResponse{false, "ERR unknown key\n"};
		}
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});

	const auto response = processor.HandleCommand("APPLY keys=Hi");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "frame-raw\n");
	ASSERT_EQ(recorded.size(), 2u);
	EXPECT_EQ(recorded[0], "PRESS H");
	EXPECT_EQ(recorded[1], "PRESS I");
}

TEST_F(TextModeCommandProcessorTest, ApplySupportsKeyDownUpAndResponseMode)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});

	const auto response =
	        processor.HandleCommand("APPLY keydown=Shift keys=Tab keyup=shift response=ok");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	if (!recorded.empty()) {
		EXPECT_NE(recorded[0], "PRESS Right");
	}
	ASSERT_EQ(recorded.size(), 3u);
	EXPECT_EQ(recorded[0], "DOWN Shift");
	EXPECT_EQ(recorded[1], "PRESS Tab");
	EXPECT_EQ(recorded[2], "UP shift");
}

TEST_F(TextModeCommandProcessorTest, ApplyHandlesLongestKeyMatches)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});
	EXPECT_FALSE(textmode::KeyboardCommandProcessor::ParseKeyName("RightRighta").has_value());
	const auto response = processor.HandleCommand("APPLY keys=RightRighta response=ok");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	for (const auto& cmd : recorded) {
		std::cout << "cmd=" << cmd << std::endl;
	}
	ASSERT_EQ(recorded.size(), 3u);
	EXPECT_EQ(recorded[0], "PRESS Right");
	EXPECT_EQ(recorded[1], "PRESS Right");
	EXPECT_EQ(recorded[2], "PRESS A");
}

TEST_F(TextModeCommandProcessorTest, ExitRequestsShutdown)
{
	bool exit_called = false;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          {},
	                          [&] { exit_called = true; });

	const auto response = processor.HandleCommand("EXIT");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	EXPECT_TRUE(exit_called);

	const auto stats = processor.HandleCommand("STATS");
	ASSERT_TRUE(stats.ok);
	EXPECT_NE(stats.payload.find("requests=1"), std::string::npos);
	EXPECT_NE(stats.payload.find("success=1"), std::string::npos);
	EXPECT_NE(stats.payload.find("failures=0"), std::string::npos);
}

} // namespace
