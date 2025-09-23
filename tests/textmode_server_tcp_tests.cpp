// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/server.h"

#include "textmode_server/command_processor.h"
#include "textmode_server/keyboard_processor.h"

#include <gtest/gtest.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using textmode::BackendEvent;
using textmode::CommandProcessor;
using textmode::CommandResponse;
using textmode::NetworkBackend;
using textmode::ServiceResult;
using textmode::TextModeServer;
using textmode::ClientHandle;

class FakeBackend : public NetworkBackend {
public:
	bool Start(const uint16_t port) override
	{
		started_port = port;
		return start_result;
	}

	void Stop() override { stopped = true; }

	std::vector<BackendEvent> Poll() override
	{
		if (pending_events.empty()) {
			return {};
		}

		auto events = std::move(pending_events.front());
		pending_events.pop_front();
		return events;
	}

	bool Send(const ClientHandle client, const std::string& payload) override
	{
		sent.emplace_back(client, payload);
		return send_result;
	}

	void Close(const ClientHandle client) override
	{
		closed_clients.push_back(client);
	}

	void QueueEvents(std::vector<BackendEvent> events)
	{
		pending_events.emplace_back(std::move(events));
	}

	uint16_t started_port = 0;
	bool start_result     = true;
	bool stopped          = false;
	bool send_result      = true;

	std::deque<std::vector<BackendEvent>> pending_events = {};
	std::vector<std::pair<ClientHandle, std::string>> sent = {};
	std::vector<ClientHandle> closed_clients               = {};
};

class TextModeServerTcpTest : public ::testing::Test {
protected:
	ServiceResult MakeSuccess()
	{
		return ServiceResult{true, "FRAME\n", {}};
	}

	ServiceResult MakeFailure()
	{
		return ServiceResult{false, {}, "no frame"};
	}
};

TEST_F(TextModeServerTcpTest, StartsAndStops)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	CommandProcessor processor([] { return ServiceResult{true, "", {}}; });

	TextModeServer server(std::move(backend));

	ASSERT_TRUE(server.Start(6123, processor));
	EXPECT_EQ(backend_ptr->started_port, 6123);

	server.Stop();
	EXPECT_TRUE(backend_ptr->stopped);
}

TEST_F(TextModeServerTcpTest, DispatchesCommands)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	int request_count = 0;
	CommandProcessor processor([&] {
		++request_count;
		return MakeSuccess();
	});

	TextModeServer server(std::move(backend));
	ASSERT_TRUE(server.Start(6000, processor));

	const ClientHandle client = 1;

	backend_ptr->QueueEvents({BackendEvent::Connected(client)});
	server.Poll();

	backend_ptr->QueueEvents({BackendEvent::Data(client, "GET\nSTATS\n")});
	server.Poll();

	ASSERT_EQ(request_count, 1);
	ASSERT_EQ(backend_ptr->sent.size(), 2u);
	EXPECT_EQ(backend_ptr->sent[0].first, client);
	EXPECT_EQ(backend_ptr->sent[0].second, "FRAME\n");
	EXPECT_EQ(backend_ptr->sent[1].second, "requests=1 success=1 failures=0\n");
}

TEST_F(TextModeServerTcpTest, HandlesPartialLines)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	CommandProcessor processor([&] { return MakeSuccess(); });

	TextModeServer server(std::move(backend));
	ASSERT_TRUE(server.Start(6000, processor));

	const ClientHandle client = 7;

	backend_ptr->QueueEvents({BackendEvent::Connected(client)});
	server.Poll();

	backend_ptr->QueueEvents({BackendEvent::Data(client, "G")});
	server.Poll();
	EXPECT_TRUE(backend_ptr->sent.empty());

	backend_ptr->QueueEvents({BackendEvent::Data(client, "ET\n")});
	server.Poll();

	ASSERT_EQ(backend_ptr->sent.size(), 1u);
	EXPECT_EQ(backend_ptr->sent[0].second, "FRAME\n");
}

TEST_F(TextModeServerTcpTest, SendsErrors)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	CommandProcessor processor([&] { return MakeFailure(); });

	TextModeServer server(std::move(backend));
	ASSERT_TRUE(server.Start(6000, processor));

	const ClientHandle client = 11;
	backend_ptr->QueueEvents({BackendEvent::Connected(client)});
	server.Poll();

	backend_ptr->QueueEvents({BackendEvent::Data(client, "GET\n")});
	server.Poll();

	ASSERT_EQ(backend_ptr->sent.size(), 1u);
	EXPECT_EQ(backend_ptr->sent[0].second, "ERR no frame\n");
}

TEST_F(TextModeServerTcpTest, SupportsKeyboardProcessor)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	std::vector<std::pair<KBD_KEYS, bool>> events;
	textmode::KeyboardCommandProcessor keyboard([&](KBD_KEYS key, bool pressed) {
		events.emplace_back(key, pressed);
	});

	TextModeServer server(std::move(backend));
	ASSERT_TRUE(server.Start(6010, keyboard));

	const ClientHandle client = 13;
	backend_ptr->QueueEvents({BackendEvent::Connected(client)});
	server.Poll();

	backend_ptr->QueueEvents({BackendEvent::Data(client, "DOWN a\nUP a\n")});
	server.Poll();

	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events[0], std::make_pair(KBD_a, true));
	EXPECT_EQ(events[1], std::make_pair(KBD_a, false));

	ASSERT_EQ(backend_ptr->sent.size(), 2u);
	EXPECT_EQ(backend_ptr->sent[0].second, "OK\n");
	EXPECT_EQ(backend_ptr->sent[1].second, "OK\n");
}

TEST_F(TextModeServerTcpTest, ExitCommandSignalsShutdown)
{
	auto backend = std::make_unique<FakeBackend>();
	FakeBackend* backend_ptr = backend.get();

	bool exit_called = false;
	CommandProcessor processor([] { return ServiceResult{true, "", {}}; },
	                          {},
	                          [&] { exit_called = true; });

	TextModeServer server(std::move(backend));
	ASSERT_TRUE(server.Start(6020, processor));

	const ClientHandle client = 17;
	backend_ptr->QueueEvents({BackendEvent::Connected(client)});
	server.Poll();

	backend_ptr->QueueEvents({BackendEvent::Data(client, "EXIT\n")});
	server.Poll();

	ASSERT_TRUE(exit_called);
	ASSERT_EQ(backend_ptr->sent.size(), 1u);
	EXPECT_EQ(backend_ptr->sent[0].second, "OK\n");
}

} // namespace
