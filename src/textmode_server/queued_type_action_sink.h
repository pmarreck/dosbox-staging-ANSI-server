// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_QUEUED_TYPE_ACTION_SINK_H
#define DOSBOX_TEXTMODE_QUEUED_TYPE_ACTION_SINK_H

#include "textmode_server/command_processor.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace textmode {

class QueuedTypeActionSink final : public ITypeActionSink {
public:
	using SendCallback  = std::function<bool(uintptr_t client, const std::string&)>;
	using CloseCallback = std::function<void(uintptr_t client)>;

	explicit QueuedTypeActionSink(SendCallback send_cb,
	                              CloseCallback close_cb);

	CommandResponse Execute(const TypeCommandPlan& plan,
	                        const CommandOrigin& origin,
	                        const KeyboardHandler& keyboard_handler,
	                        const FrameProvider& frame_provider,
	                        CompletionCallback on_complete) override;

	void SetCloseAfterResponse(bool enable);
	void SetInterTokenFrameDelay(uint32_t frames);
	void Poll();
	void CancelClient(uintptr_t client);

private:
	struct PendingRequest {
		PendingRequest() = default;

		uint64_t id = 0;
		CommandOrigin origin{};
		TypeCommandPlan plan{};
		KeyboardHandler keyboard_handler{};
		FrameProvider frame_provider{};
		CompletionCallback on_complete{};
		size_t next_action = 0;
		std::optional<std::chrono::steady_clock::time_point> resume_at{};
		uint32_t frames_remaining = 0;
		bool notify_completion = false;
		bool send_response = false;
		std::string response_payload{};
		bool saw_key_action = false;
		bool final_frame_wait_inserted = false;
	};

	void finalize_request(const PendingRequest& request,
	                     bool success,
	                     const std::string& payload);
	void complete_front(bool success, const std::string& payload);
	static bool send_action(const KeyboardHandler& handler,
	                       TypeAction::Kind kind,
	                       const std::string& key);

	SendCallback m_send;
	CloseCallback m_close;
	bool m_close_after_response = false;
 	uint32_t m_token_frame_spacing = 0;
 	uint64_t m_next_id = 1;
 	std::deque<PendingRequest> m_pending;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_QUEUED_TYPE_ACTION_SINK_H
