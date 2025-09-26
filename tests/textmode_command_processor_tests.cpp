// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/command_processor.h"
#include "textmode_server/keyboard_processor.h"

#include <gtest/gtest.h>
#include <memory>

namespace {

using textmode::CommandProcessor;
using textmode::CommandResponse;
using textmode::ServiceResult;
using textmode::CommandOrigin;
using textmode::ITypeActionSink;
using textmode::TypeAction;
using textmode::TypeCommandPlan;

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

class RecordingSink final : public ITypeActionSink {
public:
	CommandResponse response{true, "OK\n", false, 0};
	bool executed = false;
	TypeCommandPlan plan;
	CommandOrigin origin;
	CompletionCallback last_completion;

	CommandResponse Execute(const TypeCommandPlan& plan_in,
	                        const CommandOrigin& origin_in,
	                        const KeyboardHandler& /*keyboard_handler*/,
	                        const FrameProvider& /*frame_provider*/,
	                        CompletionCallback on_complete) override
	{
		executed       = true;
		plan           = plan_in;
		origin         = origin_in;
		last_completion = on_complete;
		return response;
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

TEST_F(TextModeCommandProcessorTest, ViewAliasReturnsFrame)
{
	CommandProcessor processor([] { return MakeSuccess(); });

	const auto response = processor.HandleCommand("VIEW");

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
	          "requests=2 success=1 failures=1 keys_down=\n");
}

TEST_F(TextModeCommandProcessorTest, TypeFailsWithoutKeyboardHandler)
{
	CommandProcessor processor([] { return MakeSuccess(); });
	const auto response = processor.HandleCommand("TYPE HELLO");
	EXPECT_FALSE(response.ok);
	EXPECT_EQ(response.payload, "ERR keyboard unavailable\n");
}

TEST_F(TextModeCommandProcessorTest, TypePressesKeysAndReturnsOk)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});

	const auto response = processor.HandleCommand("TYPE A B");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	ASSERT_EQ(recorded.size(), 2u);
	EXPECT_EQ(recorded[0], "PRESS A");
	EXPECT_EQ(recorded[1], "PRESS B");
}

TEST_F(TextModeCommandProcessorTest, TypeSupportsViewAndShiftSuffix)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});

	const auto response = processor.HandleCommand("TYPE ShiftDown P ShiftUp VIEW");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "frame-raw\n");
	ASSERT_EQ(recorded.size(), 3u);
	EXPECT_EQ(recorded[0], "DOWN Shift");
	EXPECT_EQ(recorded[1], "PRESS P");
	EXPECT_EQ(recorded[2], "UP Shift");
}

TEST_F(TextModeCommandProcessorTest, TypeDelegatesToActionSink)
{
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [](const std::string&) {
		return CommandResponse{true, "OK\n"};
	});

	auto sink = std::make_unique<RecordingSink>();
	auto* sink_ptr = sink.get();
	processor.SetTypeActionSink(std::move(sink));

	const auto response = processor.HandleCommand("TYPE A");

	EXPECT_TRUE(sink_ptr->executed);
	EXPECT_TRUE(response.ok);
	ASSERT_EQ(sink_ptr->plan.actions.size(), 1u);
	EXPECT_EQ(sink_ptr->plan.actions[0].kind, TypeAction::Kind::Press);
}

TEST_F(TextModeCommandProcessorTest, TypeRecognisesFrameDelayToken)
{
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [](const std::string&) {
		return CommandResponse{true, "OK\n"};
	});
	processor.SetMacroInterkeyFrames(0);
	auto sink = std::make_unique<RecordingSink>();
	auto* sink_ptr = sink.get();
	processor.SetTypeActionSink(std::move(sink));

	processor.HandleCommand("TYPE A 3frames VIEW");

	ASSERT_TRUE(sink_ptr->executed);
	ASSERT_EQ(sink_ptr->plan.actions.size(), 2u);
	EXPECT_EQ(sink_ptr->plan.actions[0].kind, TypeAction::Kind::Press);
	EXPECT_EQ(sink_ptr->plan.actions[1].kind, TypeAction::Kind::DelayFrames);
	EXPECT_EQ(sink_ptr->plan.actions[1].frames, 3u);
	EXPECT_TRUE(sink_ptr->plan.request_frame);
}

TEST_F(TextModeCommandProcessorTest, TypeStringInsertsConfiguredFrameDelays)
{
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [](const std::string&) {
		return CommandResponse{true, "OK\n"};
	});
	processor.SetMacroInterkeyFrames(2);
	auto sink = std::make_unique<RecordingSink>();
	auto* sink_ptr = sink.get();
	processor.SetTypeActionSink(std::move(sink));

	processor.HandleCommand("TYPE \"AB\"");

	ASSERT_TRUE(sink_ptr->executed);
	const auto& actions = sink_ptr->plan.actions;
	ASSERT_GE(actions.size(), 1u);
	bool saw_delay = false;
	for (const auto& action : actions) {
		if (action.kind == TypeAction::Kind::DelayFrames) {
			saw_delay = true;
			EXPECT_EQ(action.frames, 2u);
		}
	}
	EXPECT_TRUE(saw_delay);
}

TEST_F(TextModeCommandProcessorTest, TypeExpandsQuotedStrings)
{
	std::vector<std::string> recorded;
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		recorded.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	});

	const auto response = processor.HandleCommand("TYPE \"Peter\"");

	ASSERT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	// Expect Shift wrapping the initial capital P only once
	ASSERT_EQ(recorded.size(), 7u);
	EXPECT_EQ(recorded[0], "DOWN Shift");
	EXPECT_EQ(recorded[1], "PRESS P");
	EXPECT_EQ(recorded[2], "UP Shift");
	EXPECT_EQ(recorded[3], "PRESS E");
	EXPECT_EQ(recorded[4], "PRESS T");
	EXPECT_EQ(recorded[5], "PRESS E");
	EXPECT_EQ(recorded[6], "PRESS R");
}

TEST_F(TextModeCommandProcessorTest, TypeReportsKeysDownInStats)
{
	std::vector<std::pair<KBD_KEYS, bool>> sink_events;
	textmode::KeyboardCommandProcessor keyboard([&](KBD_KEYS key, bool pressed) {
		sink_events.emplace_back(key, pressed);
	});

	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const auto type_response = processor.HandleCommand("TYPE ShiftDown CtrlDown F1");
	EXPECT_TRUE(type_response.ok);
	EXPECT_EQ(type_response.payload, "OK\n");

	const auto stats = processor.HandleCommand("STATS");
	ASSERT_TRUE(stats.ok);
	EXPECT_NE(stats.payload.find("keys_down=Ctrl,Shift"), std::string::npos);

	const auto release = processor.HandleCommand("TYPE CtrlUp ShiftUp");
	EXPECT_TRUE(release.ok);
	const auto stats_clear = processor.HandleCommand("STATS");
	ASSERT_TRUE(stats_clear.ok);
	EXPECT_NE(stats_clear.payload.find("keys_down="), std::string::npos);
	EXPECT_EQ(stats_clear.payload.find("keys_down=Ctrl"), std::string::npos);
}

TEST_F(TextModeCommandProcessorTest, TypeSequenceClearsKeys)
{
	textmode::KeyboardCommandProcessor keyboard([](KBD_KEYS, bool) {});
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const auto type_text = processor.HandleCommand("TYPE \"Hello\"");
	EXPECT_TRUE(type_text.ok);
	EXPECT_TRUE(keyboard.ActiveKeys().empty());

	const auto shift_down = processor.HandleCommand("TYPE ShiftDown");
	EXPECT_TRUE(shift_down.ok);
	const auto down_keys = keyboard.ActiveKeys();
	ASSERT_EQ(down_keys.size(), 1u);
	EXPECT_EQ(down_keys.front(), "Shift");

	const auto shift_up = processor.HandleCommand("TYPE ShiftUp");
	EXPECT_TRUE(shift_up.ok);
	EXPECT_TRUE(keyboard.ActiveKeys().empty());
}

TEST_F(TextModeCommandProcessorTest, TypeAcceptsEveryKeyboardToken)
{
	std::vector<std::pair<KBD_KEYS, bool>> sink_events;
	textmode::KeyboardCommandProcessor keyboard([&](KBD_KEYS key, bool pressed) {
		sink_events.emplace_back(key, pressed);
	});

	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	for (const auto& token : textmode::KeyboardCommandProcessor::GetKeyNames()) {
		SCOPED_TRACE(token);
		sink_events.clear();
		const std::string command = std::string("TYPE ") + token;
		const auto response       = processor.HandleCommand(command);
		EXPECT_TRUE(response.ok);
		EXPECT_EQ(response.payload, "OK\n");
		ASSERT_TRUE(keyboard.ActiveKeys().empty())
		        << "Stuck key after token: " << token;
	}
}

TEST_F(TextModeCommandProcessorTest, TypeHandlesModifierDownAndUp)
{
	std::vector<std::pair<KBD_KEYS, bool>> sink_events;
	textmode::KeyboardCommandProcessor keyboard([&](KBD_KEYS key, bool pressed) {
		sink_events.emplace_back(key, pressed);
	});

	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const std::vector<std::string> modifiers = {
	        "Shift",      "LeftShift",  "RightShift", "Ctrl",      "LeftCtrl",
	        "RightCtrl",  "Alt",        "LeftAlt",    "RightAlt",  "CapsLock",
	        "NumLock",    "ScrollLock", "Gui",        "LeftGui",   "RightGui",
	        "Win",        "LWin",       "RWin"};

	for (const auto& mod : modifiers) {
    SCOPED_TRACE(mod);
    sink_events.clear();
    const std::string command = "TYPE " + mod + "Down " + mod + "Up";
    const auto response       = processor.HandleCommand(command);
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(response.payload, "OK\n");
    ASSERT_TRUE(keyboard.ActiveKeys().empty())
            << "Modifier still down: " << mod;
  }
}

TEST_F(TextModeCommandProcessorTest, TypeExpandsSymbolRow)
{
	std::vector<std::pair<KBD_KEYS, bool>> sink_events;
	textmode::KeyboardCommandProcessor keyboard([&](KBD_KEYS key, bool pressed) {
		sink_events.emplace_back(key, pressed);
	});

	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const std::string row = "!@#$%^&*()_+";
    const auto response = processor.HandleCommand("TYPE \"" + row + "\"");
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(response.payload, "OK\n");
    EXPECT_TRUE(keyboard.ActiveKeys().empty());
    EXPECT_FALSE(sink_events.empty());
}

TEST_F(TextModeCommandProcessorTest, TypeHandlesBackslashLiteral)
{
	textmode::KeyboardCommandProcessor keyboard([](KBD_KEYS, bool) {});
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const auto response = processor.HandleCommand("TYPE \\\\");
	EXPECT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	EXPECT_TRUE(keyboard.ActiveKeys().empty());
}

TEST_F(TextModeCommandProcessorTest, CommandsRequireUppercaseVerbs)
{
	CommandProcessor processor([] { return MakeSuccess(); });

	const auto response = processor.HandleCommand("type A");
	EXPECT_FALSE(response.ok);
	EXPECT_EQ(response.payload, "ERR commands are case-sensitive\n");
}

TEST_F(TextModeCommandProcessorTest, TypeTokensAreCaseSensitive)
{
	textmode::KeyboardCommandProcessor keyboard([](KBD_KEYS, bool) {});
	CommandProcessor processor([] { return MakeSuccess(); },
	                          [&](const std::string& cmd) {
		return keyboard.HandleCommand(cmd);
	},
	                          {},
	                          [&] { return keyboard.ActiveKeys(); });

	const auto response = processor.HandleCommand("TYPE shift");
	EXPECT_TRUE(response.ok);
	EXPECT_EQ(response.payload, "OK\n");
	EXPECT_TRUE(keyboard.ActiveKeys().empty());
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
