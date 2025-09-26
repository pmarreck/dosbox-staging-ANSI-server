// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/queued_type_action_sink.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using textmode::CommandOrigin;
using textmode::CommandResponse;
using textmode::QueuedTypeActionSink;
using textmode::TypeAction;
using textmode::TypeCommandPlan;

struct FakeResponseSink {
	struct Event {
		uintptr_t client = 0;
		std::string payload;
	};

	bool Send(uintptr_t client, const std::string& payload)
	{
		events.push_back(Event{client, payload});
		return true;
	}

	void Close(uintptr_t client) { closed.push_back(client); }

	std::vector<Event> events;
	std::vector<uintptr_t> closed;
};

TEST(QueuedTypeActionSinkTest, ExecutesActionsAcrossPolls)
{
	FakeResponseSink sink_backend;
	QueuedTypeActionSink sink(
	        [&](uintptr_t client, const std::string& payload) {
		return sink_backend.Send(client, payload);
	},
	        [&](uintptr_t client) { sink_backend.Close(client); });

	std::vector<std::string> keyboard_commands;
	const auto keyboard_handler = [&](const std::string& cmd) {
		keyboard_commands.push_back(cmd);
		return CommandResponse{true, "OK\n"};
	};

	bool completion_called = false;
	bool completion_success = false;

	TypeCommandPlan plan;
	TypeAction press{};
	press.kind = TypeAction::Kind::Press;
	press.key  = "A";
	plan.actions.push_back(press);
	TypeAction delay{};
	delay.kind   = TypeAction::Kind::DelayFrames;
	delay.frames = 1;
	plan.actions.push_back(delay);
	plan.request_frame = true;

	int frames_provided = 0;
	const auto frame_provider = [&] {
		++frames_provided;
		return textmode::ServiceResult{true, "FRAME\n", ""};
	};

	const auto response = sink.Execute(plan,
	                                   CommandOrigin(42),
	                                   keyboard_handler,
	                                   frame_provider,
	                                   [&](bool success) {
		completion_called  = true;
		completion_success = success;
	});

	EXPECT_TRUE(response.deferred);
	EXPECT_NE(response.deferred_id, 0u);
	EXPECT_TRUE(keyboard_commands.empty());
	EXPECT_TRUE(sink_backend.events.empty());

	sink.Poll();
	ASSERT_EQ(keyboard_commands.size(), 1u);
	EXPECT_EQ(keyboard_commands.front(), "PRESS A");
	EXPECT_TRUE(sink_backend.events.empty());
	EXPECT_FALSE(completion_called);

	sink.Poll();
	EXPECT_EQ(frames_provided, 0);
	EXPECT_TRUE(sink_backend.events.empty());
	EXPECT_FALSE(completion_called);

	sink.Poll();
	EXPECT_EQ(frames_provided, 1);
	ASSERT_EQ(sink_backend.events.size(), 1u);
	EXPECT_EQ(sink_backend.events.front().client, 42u);
	EXPECT_EQ(sink_backend.events.front().payload, "FRAME\n");
	EXPECT_TRUE(completion_called);
	EXPECT_TRUE(completion_success);
}

TEST(QueuedTypeActionSinkTest, CancelsPendingRequestOnClientClose)
{
	FakeResponseSink sink_backend;
	QueuedTypeActionSink sink(
	        [&](uintptr_t client, const std::string& payload) {
		return sink_backend.Send(client, payload);
	},
	        [&](uintptr_t client) { sink_backend.Close(client); });

	const auto keyboard_handler = [](const std::string&) {
		return CommandResponse{true, "OK\n"};
	};

	bool completion_called = false;
	bool completion_success = true;

	TypeCommandPlan plan;
	TypeAction press{};
	press.kind = TypeAction::Kind::Press;
	press.key  = "A";
	plan.actions.push_back(press);
	plan.request_frame = true;

	const auto frame_provider = [] {
		return textmode::ServiceResult{true, "FRAME\n", ""};
	};

	const auto response = sink.Execute(plan,
	                                   CommandOrigin(7),
	                                   keyboard_handler,
	                                   frame_provider,
	                                   [&](bool success) {
		completion_called  = true;
		completion_success = success;
	});

	EXPECT_TRUE(response.deferred);

	sink.CancelClient(7);
	EXPECT_TRUE(completion_called);
	EXPECT_FALSE(completion_success);
	EXPECT_TRUE(sink_backend.events.empty());
	EXPECT_FALSE(sink_backend.closed.empty());
	EXPECT_EQ(sink_backend.closed.front(), 7u);
}

} // namespace
