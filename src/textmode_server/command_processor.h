// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H
#define DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H

#include <cstdint>
#include <functional>
#include <string>

#include "textmode_server/service.h"

namespace textmode {

struct CommandResponse {
	bool ok             = false;
	std::string payload = {};
};

class ICommandProcessor {
public:
	virtual ~ICommandProcessor() = default;
	virtual CommandResponse HandleCommand(const std::string& command) = 0;
	virtual bool ConsumeExitRequest() { return false; }
};

class CommandProcessor : public ICommandProcessor {
public:
	CommandProcessor(std::function<ServiceResult()> provider,
	               std::function<CommandResponse(const std::string&)> keyboard_handler = {},
	               std::function<void()> exit_handler = {})
	        : m_provider(std::move(provider)),
	          m_keyboard_handler(std::move(keyboard_handler)),
	          m_exit_handler(std::move(exit_handler))
	{}

	CommandResponse HandleCommand(const std::string& command) override;
	bool ConsumeExitRequest() override;

private:
	std::function<ServiceResult()> m_provider;
	std::function<CommandResponse(const std::string&)> m_keyboard_handler;
	std::function<void()> m_exit_handler;
	uint64_t m_requests = 0;
	uint64_t m_success  = 0;
	uint64_t m_failures = 0;
	bool m_exit_requested = false;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_COMMAND_PROCESSOR_H
