// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/server.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "hardware/serialport/misc_util.h"
#include "misc/logging.h"

#include <SDL_net.h>

namespace textmode {

namespace {

constexpr size_t MaxClients = 8;
constexpr size_t ReceiveBufferSize = 4096;

ClientHandle ToHandle(TCPsocket socket)
{
	return reinterpret_cast<ClientHandle>(socket);
}

TCPsocket FromHandle(const ClientHandle handle)
{
	return reinterpret_cast<TCPsocket>(handle);
}

class SdlNetBackend final : public NetworkBackend {
public:
	SdlNetBackend() = default;
	SdlNetBackend(const SdlNetBackend&)            = delete;
	SdlNetBackend& operator=(const SdlNetBackend&) = delete;

	~SdlNetBackend() override { Stop(); }

	bool Start(const uint16_t port) override
	{
		Stop();

		if (!NetWrapper_InitializeSDLNet()) {
			return false;
		}

		if (!open_listener(port)) {
			return false;
		}

		if (!allocate_socket_set()) {
			Stop();
			return false;
		}

		LOG_INFO("TEXTMODE: Listening on port %u", static_cast<unsigned>(port));
		return true;
	}

	void Stop() override
	{
		for (const auto handle : list_clients()) {
			Close(handle);
		}

		if (m_listener) {
			SDLNet_TCP_Close(m_listener);
			m_listener = nullptr;
		}

		if (m_socket_set) {
			SDLNet_FreeSocketSet(m_socket_set);
			m_socket_set = nullptr;
		}
	}

	std::vector<BackendEvent> Poll() override
	{
		std::vector<BackendEvent> events;

		accept_pending(events);

		if (!m_socket_set) {
			return events;
		}

		const auto ready_count = SDLNet_CheckSockets(m_socket_set, 0);
		if (ready_count <= 0) {
			return events;
		}

		std::vector<ClientHandle> closed_clients;
		for (const auto handle : list_clients()) {
			auto* socket = FromHandle(handle);
			if (!socket) {
				continue;
			}

			if (!SDLNet_SocketReady(socket)) {
				continue;
			}

			char buffer[ReceiveBufferSize] = {};
			const auto received = SDLNet_TCP_Recv(socket, buffer, sizeof(buffer));

			if (received <= 0) {
				closed_clients.push_back(handle);
				continue;
			}

			events.emplace_back(BackendEvent::Data(handle,
			                                     std::string(buffer, buffer + received)));
		}

		for (const auto handle : closed_clients) {
			Close(handle);
			events.emplace_back(BackendEvent::Closed(handle));
		}

		return events;
	}

	bool Send(const ClientHandle client, const std::string& payload) override
	{
		auto* socket = FromHandle(client);
		if (!socket) {
			return false;
		}

		size_t total_sent = 0;
		const auto size   = payload.size();

		while (total_sent < size) {
			const auto remaining = static_cast<int>(size - total_sent);
			const auto chunk = SDLNet_TCP_Send(socket,
			                                 payload.data() + total_sent,
			                                 remaining);
			if (chunk <= 0) {
				return false;
			}
			total_sent += static_cast<size_t>(chunk);
		}

		return true;
	}

	void Close(const ClientHandle client) override
	{
		auto* socket = FromHandle(client);
		if (!socket) {
			return;
		}

		auto it = m_clients.find(client);
		if (it == m_clients.end()) {
			return;
		}

		SDLNet_TCP_DelSocket(m_socket_set, socket);
		SDLNet_TCP_Close(socket);
		m_clients.erase(it);
	}

private:
	bool open_listener(const uint16_t port)
	{
		IPaddress address = {};
		if (SDLNet_ResolveHost(&address, nullptr, port) < 0) {
			LOG_WARNING("TEXTMODE: SDLNet_ResolveHost failed: %s", SDLNet_GetError());
			return false;
		}

		m_listener = SDLNet_TCP_Open(&address);
		if (!m_listener) {
			LOG_WARNING("TEXTMODE: SDLNet_TCP_Open failed: %s", SDLNet_GetError());
			return false;
		}
		return true;
	}

	bool allocate_socket_set()
	{
		m_socket_set = SDLNet_AllocSocketSet(static_cast<int>(MaxClients));
		if (!m_socket_set) {
			LOG_WARNING("TEXTMODE: SDLNet_AllocSocketSet failed: %s", SDLNet_GetError());
			return false;
		}
		return true;
	}

	void accept_pending(std::vector<BackendEvent>& events)
	{
		if (!m_listener) {
			return;
		}

		while (true) {
			TCPsocket client = SDLNet_TCP_Accept(m_listener);
			if (!client) {
				break;
			}

			if (m_clients.size() >= MaxClients) {
				LOG_WARNING("TEXTMODE: Rejecting client, limit reached");
				SDLNet_TCP_Close(client);
				continue;
			}

			if (SDLNet_TCP_AddSocket(m_socket_set, client) < 0) {
				LOG_WARNING("TEXTMODE: SDLNet_TCP_AddSocket failed: %s", SDLNet_GetError());
				SDLNet_TCP_Close(client);
				continue;
			}

			const auto handle = ToHandle(client);
			m_clients.emplace(handle, client);
			events.emplace_back(BackendEvent::Connected(handle));
		}
	}

	std::vector<ClientHandle> list_clients() const
	{
		std::vector<ClientHandle> handles;
		handles.reserve(m_clients.size());
		for (const auto& [handle, _] : m_clients) {
			handles.push_back(handle);
		}
		return handles;
	}

	TCPsocket m_listener    = nullptr;
	SDLNet_SocketSet m_socket_set = nullptr;
	std::map<ClientHandle, TCPsocket> m_clients = {};
};

} // namespace

TextModeServer::TextModeServer(std::unique_ptr<NetworkBackend> backend)
        : m_backend(std::move(backend)),
          m_processor(nullptr),
          m_sessions(),
          m_running(false),
          m_port(0),
          m_close_after_response(false),
          m_client_close_callback()
{
	assert(m_backend);
}

TextModeServer::~TextModeServer()
{
	Stop();
}

bool TextModeServer::Start(const uint16_t port, ICommandProcessor& processor)
{
	if (!m_backend) {
		return false;
	}

	if (m_running && port == m_port) {
		m_processor = &processor;
		return true;
	}

	Stop();

	if (!m_backend->Start(port)) {
		return false;
	}

	m_running   = true;
	m_port      = port;
	m_processor = &processor;
	return true;
}

void TextModeServer::Stop()
{
	if (!m_backend) {
		return;
	}

	for (const auto& [handle, _] : m_sessions) {
		m_backend->Close(handle);
	}

	m_sessions.clear();
	m_backend->Stop();
	m_processor = nullptr;
	m_running   = false;
	m_port      = 0;
}

bool TextModeServer::Send(const ClientHandle client, const std::string& payload)
{
	if (!m_backend) {
		return false;
	}
	return m_backend->Send(client, payload);
}

void TextModeServer::Close(const ClientHandle client)
{
	if (!m_backend) {
		return;
	}
	m_backend->Close(client);
	m_sessions.erase(client);
}

void TextModeServer::Poll()
{
	if (!m_running || !m_backend || !m_processor) {
		return;
	}

	auto events = m_backend->Poll();
	for (const auto& event : events) {
		switch (event.type) {
		case BackendEvent::Type::Connected:
			m_sessions.emplace(event.client, Session{});
			break;
		case BackendEvent::Type::Data:
			HandleData(event.client, event.data);
			break;
		case BackendEvent::Type::Closed:
			Drop(event.client);
			break;
		}
	}
}

void TextModeServer::HandleData(const ClientHandle client, const std::string& data)
{
	auto it = m_sessions.find(client);
	if (it == m_sessions.end()) {
		return;
	}

	auto& buffer = it->second.buffer;
	buffer.append(data);

	while (true) {
		const auto newline_pos = buffer.find('\n');
		if (newline_pos == std::string::npos) {
			break;
		}

		std::string line = buffer.substr(0, newline_pos);
		buffer.erase(0, newline_pos + 1);

		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const auto response = m_processor->HandleCommand(line, CommandOrigin{client});
		if (!response.deferred) {
			if (!m_backend->Send(client, response.payload)) {
				Drop(client);
				break;
			}

			if (m_close_after_response) {
				Drop(client);
				break;
			}

			if (m_processor->ConsumeExitRequest()) {
				Drop(client);
				break;
			}
		}
	}
}

void TextModeServer::Drop(const ClientHandle client)
{
	m_sessions.erase(client);
	if (m_backend) {
		m_backend->Close(client);
	}
	if (m_client_close_callback) {
		m_client_close_callback(client);
	}
}

std::unique_ptr<NetworkBackend> MakeSdlNetBackend()
{
	return std::make_unique<SdlNetBackend>();
}

} // namespace textmode
