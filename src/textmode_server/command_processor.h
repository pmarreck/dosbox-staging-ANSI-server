// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H
#define DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "textmode_server/service.h"

namespace textmode {

struct CommandResponse {
	bool ok             = false;
	std::string payload = {};
	bool deferred       = false;
	uint64_t deferred_id = 0;
};

struct CommandOrigin {
	CommandOrigin() = default;
	explicit CommandOrigin(const uintptr_t handle) : client(handle) {}
	uintptr_t client = 0;
};

struct TypeAction {
	enum class Kind { Press, Down, Up, DelayMs, DelayFrames };

	Kind kind = Kind::Press;
	std::string key;
	std::chrono::milliseconds delay_ms = std::chrono::milliseconds{0};
	uint32_t frames = 0;
};

struct TypeCommandPlan {
	TypeCommandPlan() : actions(), request_frame(false) {}

	std::vector<TypeAction> actions;
	bool request_frame;
};

class ITypeActionSink {
public:
	virtual ~ITypeActionSink() = default;
	using KeyboardHandler = std::function<CommandResponse(const std::string&)>;
	using FrameProvider   = std::function<ServiceResult()>;
	using CompletionCallback = std::function<void(bool success)>;

	virtual CommandResponse Execute(const TypeCommandPlan& plan,
	                                const CommandOrigin& origin,
	                                const KeyboardHandler& keyboard_handler,
	                                const FrameProvider& frame_provider,
	                                CompletionCallback on_complete) = 0;
};

class ICommandProcessor {
public:
	virtual ~ICommandProcessor() = default;
	virtual CommandResponse HandleCommand(const std::string& command) = 0;
	virtual CommandResponse HandleCommand(const std::string& command,
	                                      const CommandOrigin& origin)
	{
		(void)origin;
		return HandleCommand(command);
	}
	virtual bool ConsumeExitRequest() { return false; }
};

class CommandProcessor : public ICommandProcessor {
public:
	CommandProcessor(std::function<ServiceResult()> provider,
	               std::function<CommandResponse(const std::string&)> keyboard_handler = {},
	               std::function<void()> exit_handler = {},
	               std::function<std::vector<std::string>()> keys_down_provider = {});

	CommandResponse HandleCommand(const std::string& command) override;
	CommandResponse HandleCommand(const std::string& command,
	                              const CommandOrigin& origin) override;
	bool ConsumeExitRequest() override;
	void SetTypeActionSink(std::shared_ptr<ITypeActionSink> sink);
	void SetMacroInterkeyFrames(uint32_t frames);
	void SetTypeSinkRequiresClient(bool requires_client);
	void SetQueueNonFrameCommands(bool enable);
	void SetAllowDeferredFrames(bool enable);

private:
	CommandResponse HandleCommandInternal(const std::string& command,
	                                      const CommandOrigin& origin);
	CommandResponse HandleTypeCommand(const std::string& argument,
	                                  const CommandOrigin& origin);

	std::function<ServiceResult()> m_provider;
	std::function<CommandResponse(const std::string&)> m_keyboard_handler;
	std::function<void()> m_exit_handler;
	std::function<std::vector<std::string>()> m_keys_down_provider;
	std::shared_ptr<ITypeActionSink> m_type_sink;
	std::optional<CommandOrigin> m_active_origin;
	uint64_t m_requests = 0;
	uint64_t m_success  = 0;
	uint64_t m_failures = 0;
	bool m_exit_requested = false;
	uint32_t m_macro_interkey_frames = 0;
	bool m_type_sink_requires_client = false;
	bool m_queue_non_frame_commands  = true;
	bool m_allow_deferred_frames     = true;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H
