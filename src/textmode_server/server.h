// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_SERVER_TCP_H
#define DOSBOX_TEXTMODE_SERVER_TCP_H

#include "textmode_server/command_processor.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace textmode {

using ClientHandle = uintptr_t;

struct BackendEvent {
	enum class Type {
		Connected,
		Data,
		Closed,
	};

	Type type            = Type::Closed;
	ClientHandle client  = 0;
	std::string data     = {};

	BackendEvent() = default;
	BackendEvent(Type event_type, ClientHandle handle, std::string payload = {})
	        : type(event_type), client(handle), data(std::move(payload))
	{}

	static BackendEvent Connected(const ClientHandle client)
	{
		return BackendEvent(Type::Connected, client);
	}

	static BackendEvent Data(const ClientHandle client, std::string payload)
	{
		return BackendEvent(Type::Data, client, std::move(payload));
	}

	static BackendEvent Closed(const ClientHandle client)
	{
		return BackendEvent(Type::Closed, client);
	}
};

class NetworkBackend {
public:
	virtual ~NetworkBackend() = default;

	virtual bool Start(uint16_t port)                        = 0;
	virtual void Stop()                                      = 0;
	virtual std::vector<BackendEvent> Poll()                 = 0;
	virtual bool Send(ClientHandle client, const std::string& payload) = 0;
	virtual void Close(ClientHandle client)                  = 0;
};

class TextModeServer {
public:
	explicit TextModeServer(std::unique_ptr<NetworkBackend> backend);
	~TextModeServer();
	TextModeServer(const TextModeServer&)            = delete;
	TextModeServer& operator=(const TextModeServer&) = delete;

	bool Start(uint16_t port, ICommandProcessor& processor);
	void Stop();
	void Poll();
	void SetCloseAfterResponse(bool enable) { m_close_after_response = enable; }
 	void SetClientCloseCallback(std::function<void(ClientHandle)> callback)
	{
		m_client_close_callback = std::move(callback);
	}

	bool IsRunning() const { return m_running; }
	uint16_t Port() const { return m_port; }
	bool Send(ClientHandle client, const std::string& payload);
	void Close(ClientHandle client);

private:
	struct Session {
		std::string buffer;
	};

	void HandleData(ClientHandle client, const std::string& data);
	void Drop(ClientHandle client);

	std::unique_ptr<NetworkBackend> m_backend;
	ICommandProcessor* m_processor = nullptr;
	std::unordered_map<ClientHandle, Session> m_sessions;
	bool m_running   = false;
	uint16_t m_port  = 0;
	bool m_close_after_response = false;
	std::function<void(ClientHandle)> m_client_close_callback;
};

std::unique_ptr<NetworkBackend> MakeSdlNetBackend();

} // namespace textmode

#endif // DOSBOX_TEXTMODE_SERVER_TCP_H
