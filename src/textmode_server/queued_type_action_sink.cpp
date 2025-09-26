// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/queued_type_action_sink.h"

#include <algorithm>
#include <chrono>
#if defined(ENABLE_TEXTMODE_QUEUE_TRACE)
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#endif
#include <utility>

namespace textmode {

namespace {

#if defined(ENABLE_TEXTMODE_QUEUE_TRACE)
bool trace_enabled()
{
	static const bool enabled = [] {
		const char* flag = std::getenv("TEXTMODE_QUEUE_TRACE");
		return flag && *flag;
	}();
	return enabled;
}

void trace_log(const char* fmt, ...)
{
	if (!trace_enabled()) {
		return;
	}
	std::fprintf(stderr, "[TEXTMODE_QUEUE_TRACE] ");
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}
#else
inline void trace_log(const char*, ...) {}
#endif

bool send_keyboard_action(const ITypeActionSink::KeyboardHandler& handler,
                          const TypeAction::Kind kind,
                          const std::string& key)
{
	if (!handler) {
		return false;
	}

	std::string verb;
	switch (kind) {
	case TypeAction::Kind::Press: verb = "PRESS"; break;
	case TypeAction::Kind::Down: verb  = "DOWN"; break;
	case TypeAction::Kind::Up: verb    = "UP"; break;
	default: return true; // delays are handled elsewhere
	}

	std::string command = verb + ' ' + key;
	const auto response = handler(command);
	if (!response.ok) {
		std::fprintf(stderr,
		            "TEXTMODE: TYPE command '%s' failed: %s",
		            command.c_str(),
		            response.payload.c_str());
		return false;
	}
	return true;
}

bool is_delay_action(const TypeAction::Kind kind)
{
	return kind == TypeAction::Kind::DelayFrames || kind == TypeAction::Kind::DelayMs;
}

} // namespace

QueuedTypeActionSink::QueuedTypeActionSink(SendCallback send_cb,
                                           CloseCallback close_cb)
        : m_send(std::move(send_cb)),
          m_close(std::move(close_cb)),
          m_close_after_response(false),
          m_token_frame_spacing(0),
          m_next_id(1),
          m_pending()
{}

CommandResponse QueuedTypeActionSink::Execute(const TypeCommandPlan& plan,
                                              const CommandOrigin& origin,
                                              const KeyboardHandler& keyboard_handler,
                                              const FrameProvider& frame_provider,
                                              CompletionCallback on_complete)
{
	if (plan.actions.empty()) {
		if (!plan.request_frame) {
			return {true, "OK\n"};
		}
		if (!frame_provider) {
			if (on_complete) {
				on_complete(false);
			}
			return {false, "ERR service unavailable\n"};
		}
		const auto result = frame_provider();
		if (on_complete) {
			on_complete(result.success);
		}
		if (!result.success) {
			return {false, "ERR " + result.error + "\n"};
		}
		return {true, result.frame};
	}

	PendingRequest request;
	request.id               = m_next_id++;
	request.origin           = origin;
	request.plan             = plan;
	request.keyboard_handler = keyboard_handler;
	request.frame_provider   = frame_provider;
	request.on_complete      = std::move(on_complete);

	const bool defer_response = plan.request_frame || m_close_after_response;
	request.notify_completion = defer_response;
	request.send_response     = defer_response && !plan.request_frame;
	if (request.send_response) {
		request.response_payload = "OK\n";
	}

	m_pending.push_back(std::move(request));
trace_log("enqueue id=%llu client=%p deferred=%s frame=%s actions=%zu\n",
          static_cast<unsigned long long>(m_pending.back().id),
          reinterpret_cast<void*>(origin.client),
          defer_response ? "yes" : "no",
          plan.request_frame ? "yes" : "no",
          plan.actions.size());

	if (defer_response) {
		CommandResponse response{true, ""};
		response.deferred    = true;
		response.deferred_id = m_pending.back().id;
		return response;
	}

return {true, "OK\n"};
}

void QueuedTypeActionSink::SetCloseAfterResponse(const bool enable)
{
	m_close_after_response = enable;
}

void QueuedTypeActionSink::SetInterTokenFrameDelay(const uint32_t frames)
{
	m_token_frame_spacing = frames;
}

void QueuedTypeActionSink::Poll()
{
	auto now = std::chrono::steady_clock::now();

	while (!m_pending.empty()) {
		auto& request = m_pending.front();
trace_log("poll id=%llu next=%zu frames=%u resume=%s client=%p\n",
          static_cast<unsigned long long>(request.id),
          request.next_action,
          request.frames_remaining,
          request.resume_at ? "yes" : "no",
          reinterpret_cast<void*>(request.origin.client));

		if (request.frames_remaining > 0) {
			--request.frames_remaining;
			if (request.frames_remaining > 0) {
trace_log("wait id=%llu frames_remaining=%u\n",
          static_cast<unsigned long long>(request.id),
          request.frames_remaining);
				break;
			}
		}

		if (request.resume_at.has_value()) {
			if (now < *request.resume_at) {
trace_log("wait id=%llu resume_pending\n",
          static_cast<unsigned long long>(request.id));
				break;
			}
			request.resume_at.reset();
		}

		bool advanced = false;
		while (request.next_action < request.plan.actions.size()) {
			const auto& action = request.plan.actions[request.next_action];

			if (action.kind == TypeAction::Kind::DelayFrames && action.frames == 0) {
				++request.next_action;
				advanced = true;
				continue;
			}
			if (action.kind == TypeAction::Kind::DelayMs && action.delay_ms.count() <= 0) {
				++request.next_action;
				advanced = true;
				continue;
			}

			switch (action.kind) {
			case TypeAction::Kind::Press:
				request.saw_key_action = true;
#if defined(__clang__) || defined(__GNUC__)
				[[fallthrough]];
#endif
			case TypeAction::Kind::Down:
			case TypeAction::Kind::Up: {
				send_keyboard_action(request.keyboard_handler, action.kind, action.key);
trace_log("action id=%llu kind=%d key=%s\n",
          static_cast<unsigned long long>(request.id),
          static_cast<int>(action.kind),
          action.key.c_str());
				++request.next_action;
				advanced = true;
				const bool next_is_delay = (request.next_action < request.plan.actions.size() &&
				                          is_delay_action(request.plan.actions[request.next_action].kind));
				if (!next_is_delay && m_token_frame_spacing > 0) {
					request.frames_remaining = m_token_frame_spacing;
trace_log("action id=%llu inserted inter-token frames=%u\n",
          static_cast<unsigned long long>(request.id),
          m_token_frame_spacing);
					break;
				}
				break;
			}
			case TypeAction::Kind::DelayMs:
				request.resume_at = now + action.delay_ms;
trace_log("delay id=%llu ms=%lld\n",
          static_cast<unsigned long long>(request.id),
          static_cast<long long>(action.delay_ms.count()));
				++request.next_action;
				advanced = true;
				break;
			case TypeAction::Kind::DelayFrames:
				request.frames_remaining = action.frames;
trace_log("delay id=%llu frames=%u\n",
          static_cast<unsigned long long>(request.id),
          action.frames);
				++request.next_action;
				advanced = true;
				break;
			}

			if (action.kind != TypeAction::Kind::Press &&
			    action.kind != TypeAction::Kind::Down &&
			    action.kind != TypeAction::Kind::Up) {
				break;
			}

			if (request.frames_remaining > 0 || request.resume_at.has_value()) {
				break;
			}

			// only process one key action per poll
			break;
		}

		if (request.next_action >= request.plan.actions.size() &&
		    request.frames_remaining == 0 &&
		    !request.resume_at.has_value()) {
			if (request.saw_key_action && !request.final_frame_wait_inserted) {
				const uint32_t wait_frames = std::max<uint32_t>(1, m_token_frame_spacing);
				request.frames_remaining        = wait_frames;
				request.final_frame_wait_inserted = true;
trace_log("final-wait id=%llu frames=%u\n",
          static_cast<unsigned long long>(request.id),
          wait_frames);
				continue;
			}

			const bool success = [&] {
trace_log("complete id=%llu frame=%s send_response=%s\n",
          static_cast<unsigned long long>(request.id),
          request.plan.request_frame ? "yes" : "no",
          request.send_response ? "yes" : "no");
				bool ok = true;

				if (request.plan.request_frame) {
					std::string payload;
					if (!request.frame_provider) {
						payload = "ERR service unavailable\n";
						ok      = false;
					} else {
						const auto result = request.frame_provider();
						if (!result.success) {
							payload = "ERR " + result.error + "\n";
							ok      = false;
						} else {
							payload = result.frame;
						}
					}

					if (m_send) {
						if (!m_send(request.origin.client, payload)) {
							ok = false;
						}
					}

					if (m_close_after_response && m_close) {
						m_close(request.origin.client);
trace_log("close id=%llu client=%p\n",
          static_cast<unsigned long long>(request.id),
          reinterpret_cast<void*>(request.origin.client));
					}

					return ok;
				}

				if (request.send_response) {
					if (m_send) {
						if (!m_send(request.origin.client, request.response_payload)) {
							ok = false;
						}
					}
					if (m_close_after_response && m_close) {
						m_close(request.origin.client);
					}
				}

				return ok;
			}();

			if (request.notify_completion && request.on_complete) {
				request.on_complete(success);
			}

			m_pending.pop_front();
trace_log("dequeue id=%llu success=%s\n",
          static_cast<unsigned long long>(request.id),
          success ? "yes" : "no");
			now = std::chrono::steady_clock::now();
			continue;
		}

		if (!advanced) {
			break;
		}

		if (request.frames_remaining > 0 || request.resume_at.has_value()) {
			break;
		}

		// processed a key action without delay; allow next poll to continue
		break;
	}
}

void QueuedTypeActionSink::CancelClient(const uintptr_t client)
{
	for (auto it = m_pending.begin(); it != m_pending.end();) {
		if (it->origin.client == client) {
trace_log("cancel id=%llu client=%p\n",
          static_cast<unsigned long long>(it->id),
          reinterpret_cast<void*>(client));
			if (it->notify_completion && it->on_complete) {
				it->on_complete(false);
			}
			it = m_pending.erase(it);
		} else {
			++it;
		}
	}

	if (m_close) {
		m_close(client);
	}
}

} // namespace textmode
