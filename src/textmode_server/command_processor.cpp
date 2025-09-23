// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/command_processor.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "textmode_server/keyboard_processor.h"

namespace textmode {

namespace {

std::string trim(std::string_view str)
{
	const auto begin = str.find_first_not_of(" \t\r\n");
	if (begin == std::string_view::npos) {
		return {};
	}
	const auto end = str.find_last_not_of(" \t\r\n");
	return std::string(str.substr(begin, end - begin + 1));
}

std::string to_upper(std::string_view str)
{
	std::string upper(str);
	std::transform(upper.begin(),
	               upper.end(),
	               upper.begin(),
	               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return upper;
}

struct ApplyToken {
	enum class Kind {
		Keys,
		KeyDown,
		KeyUp,
		Response
	};

	Kind kind;
	std::string value;
};

std::string unquote(std::string_view value)
{
	if (value.size() >= 2 &&
	    ((value.front() == '"' && value.back() == '"') ||
	     (value.front() == '\'' && value.back() == '\''))) {
		return std::string(value.substr(1, value.size() - 2));
	}
	return std::string(value);
}

std::optional<std::vector<ApplyToken>> parse_apply_tokens(const std::string& argument)
{
	std::vector<ApplyToken> tokens;
	std::string_view view(argument);
	while (!view.empty()) {
		const auto non_space = view.find_first_not_of(" \t\r\n");
		if (non_space == std::string_view::npos) {
			break;
		}
		view.remove_prefix(non_space);
		const auto next_space = view.find_first_of(" \t\r\n");
		const auto token_view = (next_space == std::string_view::npos)
		                                ? view
		                                : view.substr(0, next_space);
		if (next_space != std::string_view::npos) {
			view.remove_prefix(next_space);
		} else {
			view = {};
		}

		const auto eq_pos = token_view.find('=');
		if (eq_pos == std::string_view::npos) {
			return std::nullopt;
		}
		const auto name  = to_upper(token_view.substr(0, eq_pos));
		auto value = unquote(token_view.substr(eq_pos + 1));

		if (name == "KEYS") {
			tokens.push_back(ApplyToken{ApplyToken::Kind::Keys, std::move(value)});
		} else if (name == "KEYDOWN") {
			tokens.push_back(ApplyToken{ApplyToken::Kind::KeyDown, std::move(value)});
		} else if (name == "KEYUP") {
			tokens.push_back(ApplyToken{ApplyToken::Kind::KeyUp, std::move(value)});
		} else if (name == "RESPONSE") {
			tokens.push_back(ApplyToken{ApplyToken::Kind::Response, to_upper(value)});
		} else {
			return std::nullopt;
		}
	}
	return tokens;
}

bool press_key_command(const std::function<CommandResponse(const std::string&)>& handler,
                       const std::string& verb,
                       const std::string& key)
{
	if (!handler) {
		return false;
	}
	const auto response = handler(verb + ' ' + key);
	return response.ok;
}


std::vector<std::string> expand_key_sequence(const std::string& sequence)
{
	const auto& key_names = KeyboardCommandProcessor::GetKeyNames();
	size_t offset         = 0;
	std::vector<std::string> expanded;

	while (offset < sequence.size()) {
		const size_t remaining = sequence.size() - offset;
		bool matched           = false;

		for (const auto& name : key_names) {
			const auto len = name.size();
			if (len > remaining) {
				continue;
			}
			auto original = sequence.substr(offset, len);
			const auto segment = to_upper(original);
			if (segment != name) {
				continue;
			}
			if (original.size() == 1) {
				original[0] = static_cast<char>(std::toupper(
				        static_cast<unsigned char>(original[0])));
			}
			expanded.push_back(std::move(original));
			offset += len;
			matched = true;
			break;
		}

		if (!matched) {
			const char ch = sequence[offset];
			std::string key(1, static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
			expanded.push_back(std::move(key));
			++offset;
		}
	}

	return expanded;
}

bool press_or_type(const std::function<CommandResponse(const std::string&)>& handler,
	              const std::string& value)
{
	if (value.empty()) {
		return true;
	}
	const auto expanded = expand_key_sequence(value);
	for (const auto& key : expanded) {
		if (!press_key_command(handler, "PRESS", key)) {
			return false;
		}
	}
	return true;
}

std::string show_spaces(const std::string& frame)
{
	std::string result;
	result.reserve(frame.size());
	for (const auto ch : frame) {
		if (ch == ' ') {
			result += "\xC2\xB7"; // middle dot
		} else {
			result += ch;
		}
	}
	return result;
}

} // namespace

CommandResponse CommandProcessor::HandleCommand(const std::string& raw_command)
{
	const auto trimmed = trim(raw_command);
	if (trimmed.empty()) {
		return {false, "ERR empty command\n"};
	}

	const auto space_pos = trimmed.find(' ');
	const auto verb       = trimmed.substr(0, space_pos);
	const auto verb_upper = to_upper(verb);
	const auto argument   = (space_pos == std::string::npos)
	                               ? std::string()
	                               : trim(trimmed.substr(space_pos + 1));

	if (verb_upper == "STATS") {
		std::ostringstream oss;
		oss << "requests=" << m_requests << ' '
		    << "success=" << m_success << ' '
		    << "failures=" << m_failures << "\n";
		return {true, oss.str()};
	}

	if (verb_upper == "EXIT") {
		++m_requests;
		if (m_exit_handler) {
			m_exit_handler();
		}
		m_exit_requested = true;
		++m_success;
		return {true, "OK\n"};
	}

	if (verb_upper == "GET") {
		if (!m_provider) {
			return {false, "ERR service unavailable\n"};
		}

		++m_requests;
		const bool showspc = to_upper(argument) == "SHOWSPC";
		const auto result  = m_provider();
		if (!result.success) {
			++m_failures;
			return {false, "ERR " + result.error + "\n"};
		}

		++m_success;
		return {true, showspc ? show_spaces(result.frame) : result.frame};
	}

	if (verb_upper == "APPLY") {
		++m_requests;
		if (!m_keyboard_handler) {
			++m_failures;
			return {false, "ERR keyboard unavailable\n"};
		}
		if (argument.empty()) {
			++m_failures;
			return {false, "ERR missing apply arguments\n"};
		}

		auto tokens_opt = parse_apply_tokens(argument);
		if (!tokens_opt) {
			++m_failures;
			return {false, "ERR invalid apply syntax\n"};
		}

		const auto& tokens = *tokens_opt;
		bool want_frame    = true;
		bool success       = true;

		for (const auto& token : tokens) {
			switch (token.kind) {
			case ApplyToken::Kind::Response:
				if (token.value == "OK" || token.value == "NONE") {
					want_frame = false;
				} else if (token.value == "FRAME" || token.value == "PAYLOAD") {
					want_frame = true;
				} else {
					++m_failures;
					return {false, "ERR invalid response mode\n"};
				}
				break;
			case ApplyToken::Kind::Keys:
				success = press_or_type(m_keyboard_handler, token.value);
				break;
			case ApplyToken::Kind::KeyDown:
				success = press_key_command(m_keyboard_handler, "DOWN", token.value);
				break;
			case ApplyToken::Kind::KeyUp:
				success = press_key_command(m_keyboard_handler, "UP", token.value);
				break;
			}

			if (!success) {
				++m_failures;
				return {false, "ERR apply command failed\n"};
			}
		}

		++m_success;
		if (!want_frame) {
			return {true, "OK\n"};
		}

		if (!m_provider) {
			return {false, "ERR service unavailable\n"};
		}

		const auto result = m_provider();
		if (!result.success) {
			++m_failures;
			return {false, "ERR " + result.error + "\n"};
		}

		return {true, result.frame};
	}

	return {false, "ERR unknown command\n"};
}

bool CommandProcessor::ConsumeExitRequest()
{
	if (!m_exit_requested) {
		return false;
	}
	m_exit_requested = false;
	return true;
}

} // namespace textmode
