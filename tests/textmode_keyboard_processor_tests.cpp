// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/keyboard_processor.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

using textmode::CommandResponse;
using textmode::KeyboardCommandProcessor;

class KeyboardProcessorTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		sink = [this](KBD_KEYS key, bool pressed) {
			events.emplace_back(key, pressed);
		};
		processor = std::make_unique<KeyboardCommandProcessor>(sink);
	}

	std::pair<bool, std::string> Execute(const std::string& command)
	{
		const CommandResponse response = processor->HandleCommand(command);
		return {response.ok, response.payload};
	}

	KeyboardCommandProcessor::KeySink sink;
	std::vector<std::pair<KBD_KEYS, bool>> events;
	std::unique_ptr<KeyboardCommandProcessor> processor;
};

TEST_F(KeyboardProcessorTest, PressSendsKeyDownAndUp)
{
	const auto [ok, payload] = Execute("PRESS a");

	ASSERT_TRUE(ok);
	EXPECT_EQ(payload, "OK\n");

	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events[0].first, KBD_a);
	EXPECT_TRUE(events[0].second);
	EXPECT_EQ(events[1].first, KBD_a);
	EXPECT_FALSE(events[1].second);
}

TEST_F(KeyboardProcessorTest, DownThenUp)
{
	auto [ok_down, payload_down] = Execute("DOWN leftshift");

	ASSERT_TRUE(ok_down);
	EXPECT_EQ(payload_down, "OK\n");

	auto [ok_up, payload_up] = Execute("UP LEFTSHIFT");
	ASSERT_TRUE(ok_up);
	EXPECT_EQ(payload_up, "OK\n");

	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events[0], std::make_pair(KBD_leftshift, true));
	EXPECT_EQ(events[1], std::make_pair(KBD_leftshift, false));
}

TEST_F(KeyboardProcessorTest, DuplicateDownFails)
{
	ASSERT_TRUE(Execute("DOWN ctrl").first);

	auto [ok, payload] = Execute("DOWN CTRL");
	EXPECT_FALSE(ok);
	EXPECT_EQ(payload, "ERR key already down\n");
}

TEST_F(KeyboardProcessorTest, UpWithoutDownFails)
{
	auto [ok, payload] = Execute("UP o");
	EXPECT_FALSE(ok);
	EXPECT_EQ(payload, "ERR key not down\n");
}

TEST_F(KeyboardProcessorTest, UnknownKeyRejected)
{
	auto [ok, payload] = Execute("PRESS notakey");
	EXPECT_FALSE(ok);
	EXPECT_EQ(payload, "ERR unknown key\n");
}

TEST_F(KeyboardProcessorTest, ResetReleasesHeldKeys)
{
	ASSERT_TRUE(Execute("DOWN z").first);
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events.front(), std::make_pair(KBD_z, true));

	auto [ok, payload] = Execute("RESET");
	EXPECT_TRUE(ok);
	EXPECT_EQ(payload, "OK\n");

	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events.back().first, events.front().first);
	EXPECT_FALSE(events.back().second);

	auto [up_ok, up_payload] = Execute("UP z");
	EXPECT_FALSE(up_ok);
	EXPECT_EQ(up_payload, "ERR key not down\n");
}

TEST_F(KeyboardProcessorTest, StatsReportCounts)
{
	EXPECT_TRUE(Execute("PRESS 1").first);
	EXPECT_FALSE(Execute("DOWN unknown").first);

	auto [ok, payload] = Execute("STATS");
	ASSERT_TRUE(ok);
	EXPECT_EQ(payload, "commands=3 success=1 failures=1\n");
}

} // namespace
