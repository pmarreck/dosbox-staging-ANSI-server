// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_KEYBOARD_PROCESSOR_H
#define DOSBOX_TEXTMODE_KEYBOARD_PROCESSOR_H

#include "textmode_server/command_processor.h"

#include "hardware/input/keyboard.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace textmode {

class KeyboardCommandProcessor : public ICommandProcessor {
public:
	using KeySink = std::function<void(KBD_KEYS, bool)>;

	explicit KeyboardCommandProcessor(KeySink sink);

	CommandResponse HandleCommand(const std::string& command) override;
	void Reset();
	static std::optional<KBD_KEYS> ParseKeyName(const std::string& name);
 	static const std::vector<std::string>& GetKeyNames();

private:
	CommandResponse HandlePress(const std::string& args);
	CommandResponse HandleDown(const std::string& args);
	CommandResponse HandleUp(const std::string& args);
	CommandResponse HandleReset();
	CommandResponse HandleStats() const;

	static std::string Trim(std::string_view text);
	static std::string ToUpper(std::string_view text);
	static std::optional<std::string> FirstToken(const std::string& args,
	                                             std::string& remainder_out);

	KeySink m_sink;
	std::unordered_set<KBD_KEYS> m_pressed;
	uint64_t m_commands = 0;
	uint64_t m_success  = 0;
	uint64_t m_failures = 0;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_KEYBOARD_PROCESSOR_H
