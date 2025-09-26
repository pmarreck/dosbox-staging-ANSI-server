// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/keyboard_processor.h"

#include <algorithm>
#include <cctype>
#if defined(ENABLE_TEXTMODE_QUEUE_TRACE)
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#endif
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace textmode {

namespace {

using KeyMap = std::unordered_map<std::string, KBD_KEYS>;

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

const KeyMap& get_key_map()
{
	static const KeyMap map = {
	        {"Esc", KBD_esc},
	        {"Escape", KBD_esc},
	        {"Tab", KBD_tab},
	        {"Backspace", KBD_backspace},
	        {"Bksp", KBD_backspace},
	        {"Enter", KBD_enter},
	        {"Return", KBD_enter},
	        {"Space", KBD_space},
	        {"Spacebar", KBD_space},

	        {"LeftAlt", KBD_leftalt},
	        {"Alt", KBD_leftalt},
	        {"RightAlt", KBD_rightalt},
	        {"LeftCtrl", KBD_leftctrl},
	        {"Ctrl", KBD_leftctrl},
	        {"Control", KBD_leftctrl},
	        {"RightCtrl", KBD_rightctrl},
	        {"LeftShift", KBD_leftshift},
	        {"Shift", KBD_leftshift},
	        {"RightShift", KBD_rightshift},
	        {"LeftGui", KBD_leftgui},
	        {"Gui", KBD_leftgui},
	        {"Win", KBD_leftgui},
	        {"Windows", KBD_leftgui},
	        {"RightGui", KBD_rightgui},
	        {"CapsLock", KBD_capslock},
	        {"NumLock", KBD_numlock},
	        {"ScrollLock", KBD_scrolllock},

	        {"Grave", KBD_grave},
	        {"Backquote", KBD_grave},
	        {"Backtick", KBD_grave},
	        {"Minus", KBD_minus},
	        {"Hyphen", KBD_minus},
	        {"Equals", KBD_equals},
	        {"Plus", KBD_equals},
	        {"Backslash", KBD_backslash},
	        {"LeftBracket", KBD_leftbracket},
	        {"LBracket", KBD_leftbracket},
	        {"OpenBracket", KBD_leftbracket},
	        {"RightBracket", KBD_rightbracket},
	        {"RBracket", KBD_rightbracket},
	        {"CloseBracket", KBD_rightbracket},
	        {"Semicolon", KBD_semicolon},
	        {"Colon", KBD_semicolon},
	        {"Apostrophe", KBD_quote},
	        {"Quote", KBD_quote},
	        {"Oem102", KBD_oem102},
	        {"LessGreater", KBD_oem102},
	        {"Period", KBD_period},
	        {"Dot", KBD_period},
	        {"Comma", KBD_comma},
	        {"Slash", KBD_slash},
	        {"ForwardSlash", KBD_slash},
	        {"Abnt1", KBD_abnt1},

	        {"PrintScreen", KBD_printscreen},
	        {"PrtSc", KBD_printscreen},
	        {"SysRq", KBD_printscreen},
	        {"Pause", KBD_pause},
	        {"Break", KBD_pause},

	        {"Insert", KBD_insert},
	        {"Ins", KBD_insert},
	        {"Delete", KBD_delete},
	        {"Del", KBD_delete},
	        {"Home", KBD_home},
	        {"End", KBD_end},
	        {"PageUp", KBD_pageup},
	        {"PgUp", KBD_pageup},
	        {"PageDown", KBD_pagedown},
	        {"PgDn", KBD_pagedown},
	        {"Left", KBD_left},
	        {"LeftArrow", KBD_left},
	        {"Up", KBD_up},
	        {"UpArrow", KBD_up},
	        {"Down", KBD_down},
	        {"DownArrow", KBD_down},
	        {"Right", KBD_right},
	        {"RightArrow", KBD_right},

	        {"Numpad0", KBD_kp0},
	        {"Numpad1", KBD_kp1},
	        {"Numpad2", KBD_kp2},
	        {"Numpad3", KBD_kp3},
	        {"Numpad4", KBD_kp4},
	        {"Numpad5", KBD_kp5},
	        {"Numpad6", KBD_kp6},
	        {"Numpad7", KBD_kp7},
	        {"Numpad8", KBD_kp8},
	        {"Numpad9", KBD_kp9},
	        {"NumpadDivide", KBD_kpdivide},
	        {"NumpadMultiply", KBD_kpmultiply},
	        {"NumpadMinus", KBD_kpminus},
	        {"NumpadPlus", KBD_kpplus},
	        {"NumpadEnter", KBD_kpenter},
	        {"NumpadPeriod", KBD_kpperiod},
	        {"NumpadDecimal", KBD_kpperiod},
	};
	return map;
}

std::optional<KBD_KEYS> map_single_character(const char ch)
{
	if (ch >= '0' && ch <= '9') {
		switch (ch) {
		case '1': return KBD_1;
		case '2': return KBD_2;
		case '3': return KBD_3;
		case '4': return KBD_4;
		case '5': return KBD_5;
		case '6': return KBD_6;
		case '7': return KBD_7;
		case '8': return KBD_8;
		case '9': return KBD_9;
		case '0': return KBD_0;
		default: break;
		}
	}

	if (ch >= 'A' && ch <= 'Z') {
		static const std::unordered_map<char, KBD_KEYS> letter_map = {
		        {'A', KBD_a}, {'B', KBD_b}, {'C', KBD_c}, {'D', KBD_d},
		        {'E', KBD_e}, {'F', KBD_f}, {'G', KBD_g}, {'H', KBD_h},
		        {'I', KBD_i}, {'J', KBD_j}, {'K', KBD_k}, {'L', KBD_l},
		        {'M', KBD_m}, {'N', KBD_n}, {'O', KBD_o}, {'P', KBD_p},
		        {'Q', KBD_q}, {'R', KBD_r}, {'S', KBD_s}, {'T', KBD_t},
		        {'U', KBD_u}, {'V', KBD_v}, {'W', KBD_w}, {'X', KBD_x},
		        {'Y', KBD_y}, {'Z', KBD_z},
		};

		if (const auto it = letter_map.find(ch); it != letter_map.end()) {
			return it->second;
		}
	}

	return std::nullopt;
}

std::optional<KBD_KEYS> map_f_key(const std::string& name)
{
	if (name.size() < 2 || name.front() != 'F') {
		return std::nullopt;
	}

	const auto number_part = name.substr(1);
	if (!std::all_of(number_part.begin(), number_part.end(), [](unsigned char c) {
		return std::isdigit(c) != 0;
	})) {
		return std::nullopt;
	}

	const int value = std::stoi(number_part);
	if (value < 1 || value > 12) {
		return std::nullopt;
	}

	return static_cast<KBD_KEYS>(KBD_f1 + (value - 1));
}

CommandResponse ok_response()
{
	return {true, "OK\n"};
}

CommandResponse error_response(const std::string& message)
{
	return {false, "ERR " + message + "\n"};
}

} // namespace

std::string format_display_name(const std::string& token)
{
	std::string upper(token);
	std::transform(upper.begin(),
	               upper.end(),
	               upper.begin(),
	               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	if (upper == "SHIFT" || upper == "LSHIFT" || upper == "RSHIFT") {
		return "Shift";
	}
	if (upper == "CTRL" || upper == "LCTRL" || upper == "RCTRL" ||
	    upper == "CONTROL") {
		return "Ctrl";
	}
	if (upper == "ALT" || upper == "LALT" || upper == "RALT" ||
	    upper == "LEFTALT" || upper == "RIGHTALT") {
		return "Alt";
	}
	if (upper == "CAPS" || upper == "CAPSLOCK") {
		return "CapsLock";
	}
	if (upper.rfind("NUMPAD", 0) == 0) {
		std::string rest = upper.substr(6);
		for (auto& ch : rest) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return "NumPad" + rest;
	}
	if (upper.rfind("KP", 0) == 0) {
		std::string rest = upper.substr(2);
		for (auto& ch : rest) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return "NumPad" + rest;
	}
	if (upper.size() > 1 && upper.front() == 'F' &&
	    std::all_of(upper.begin() + 1, upper.end(), [](unsigned char c) {
		    return std::isdigit(c) != 0;
	    })) {
		return "F" + upper.substr(1);
	}
	if (upper.size() == 1) {
		return std::string(1, upper.front());
	}

	// Default: Title-case the token
	std::string result;
	result.reserve(upper.size());
	bool new_word = true;
	for (const auto ch : upper) {
		if (!std::isalnum(static_cast<unsigned char>(ch))) {
			result.push_back(ch);
			new_word = true;
			continue;
		}
		if (new_word) {
			result.push_back(static_cast<char>(std::toupper(ch)));
			new_word = false;
		} else {
			result.push_back(static_cast<char>(std::tolower(ch)));
		}
		if (std::isdigit(static_cast<unsigned char>(ch))) {
			new_word = true;
		}
	}
	return result;
}

KeyboardCommandProcessor::KeyboardCommandProcessor(KeySink sink)
        : m_sink(std::move(sink)),
          m_pressed(),
          m_commands(0),
          m_success(0),
          m_failures(0)
{
}

CommandResponse KeyboardCommandProcessor::HandleCommand(const std::string& raw_command)
{
	const auto trimmed = Trim(raw_command);
	if (trimmed.empty()) {
		trace_log("kbd command empty raw='%s'\n", raw_command.c_str());
		return error_response("empty command");
	}

	const auto space_pos = trimmed.find(' ');
	const auto verb      = trimmed.substr(0, space_pos);
	const auto verb_upper = ToUpper(verb);
	const auto args = (space_pos == std::string::npos)
	                          ? std::string()
	                          : Trim(trimmed.substr(space_pos + 1));
	trace_log("kbd command verb=%s args='%s'\n", verb_upper.c_str(), args.c_str());

	++m_commands;

	CommandResponse response;
	if (verb_upper == "PRESS") {
		response = HandlePress(args);
	} else if (verb_upper == "DOWN") {
		response = HandleDown(args);
	} else if (verb_upper == "UP") {
		response = HandleUp(args);
	} else if (verb_upper == "RESET") {
		response = HandleReset();
	} else if (verb_upper == "STATS") {
		response = HandleStats();
	} else {
		response = error_response("unknown command");
	}

	if (response.ok) {
		++m_success;
	} else {
		++m_failures;
	}
	trace_log("kbd command result ok=%s payload='%s'\n",
	          response.ok ? "yes" : "no",
	          response.payload.c_str());
	return response;
}

void KeyboardCommandProcessor::Reset()
{
	for (const auto& entry : m_pressed) {
		trace_log("kbd reset release key=%d\n", static_cast<int>(entry.first));
		m_sink(entry.first, false);
	}
	m_pressed.clear();
}

CommandResponse KeyboardCommandProcessor::HandlePress(const std::string& args)
{
	trace_log("kbd press args='%s'\n", args.c_str());
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		trace_log("kbd press error missing key\n");
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		trace_log("kbd press error unexpected args='%s'\n", remainder.c_str());
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		trace_log("kbd press error unknown token='%s'\n", token->c_str());
		return error_response("unknown key");
	}

	if (m_pressed.contains(*key)) {
		trace_log("kbd press error already down key=%d\n", static_cast<int>(*key));
		return error_response("key already down");
	}

	trace_log("kbd press sink key=%d down/up\n", static_cast<int>(*key));
	m_sink(*key, true);
	m_sink(*key, false);
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleDown(const std::string& args)
{
	trace_log("kbd down args='%s'\n", args.c_str());
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		trace_log("kbd down error missing key\n");
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		trace_log("kbd down error unexpected args='%s'\n", remainder.c_str());
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		trace_log("kbd down error unknown token='%s'\n", token->c_str());
		return error_response("unknown key");
	}

	if (m_pressed.contains(*key)) {
		trace_log("kbd down error already down key=%d\n", static_cast<int>(*key));
		return error_response("key already down");
	}

	trace_log("kbd down sink key=%d\n", static_cast<int>(*key));
	m_sink(*key, true);
	m_pressed[*key] = format_display_name(*token);
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleUp(const std::string& args)
{
	trace_log("kbd up args='%s'\n", args.c_str());
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		trace_log("kbd up error missing key\n");
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		trace_log("kbd up error unexpected args='%s'\n", remainder.c_str());
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		trace_log("kbd up error unknown token='%s'\n", token->c_str());
		return error_response("unknown key");
	}

	const auto it = m_pressed.find(*key);
	if (it == m_pressed.end()) {
		trace_log("kbd up error key not down key=%d\n", static_cast<int>(*key));
		return error_response("key not down");
	}

	trace_log("kbd up sink key=%d\n", static_cast<int>(*key));
	m_sink(*key, false);
	m_pressed.erase(it);
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleReset()
{
	Reset();
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleStats() const
{
	std::ostringstream oss;
	oss << "commands=" << m_commands << ' '
	    << "success=" << m_success << ' '
	    << "failures=" << m_failures << "\n";
	return {true, oss.str()};
}

std::optional<KBD_KEYS> KeyboardCommandProcessor::ParseKeyName(const std::string& name)
{
	if (name.empty()) {
		return std::nullopt;
	}

	if (name.size() == 1) {
		return map_single_character(name.front());
	}

	if (const auto f_key = map_f_key(name); f_key) {
		return f_key;
	}

	if (const auto it = get_key_map().find(name); it != get_key_map().end()) {
		return it->second;
	}

	return std::nullopt;
}

const std::vector<std::string>& KeyboardCommandProcessor::GetKeyNames()
{
	static const std::vector<std::string> names = [] {
		std::vector<std::string> result;
		const auto& map = get_key_map();
		result.reserve(map.size() + 26 + 10 + 12);
		for (const auto& entry : map) {
			result.push_back(entry.first);
		}
		for (int f = 1; f <= 12; ++f) {
			result.push_back("F" + std::to_string(f));
		}
		for (char c = 'A'; c <= 'Z'; ++c) {
			result.emplace_back(1, c);
		}
		for (char c = '0'; c <= '9'; ++c) {
			result.emplace_back(1, c);
		}
		std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
			if (a.size() != b.size()) {
				return a.size() > b.size();
			}
			return a < b;
		});
		result.erase(std::unique(result.begin(), result.end()), result.end());
		return result;
	}();

	return names;
}

std::string KeyboardCommandProcessor::Trim(std::string_view text)
{
	const auto begin = text.find_first_not_of(" \t\r\n");
	if (begin == std::string_view::npos) {
		return {};
	}
	const auto end = text.find_last_not_of(" \t\r\n");
	return std::string(text.substr(begin, end - begin + 1));
}

std::string KeyboardCommandProcessor::ToUpper(std::string_view text)
{
	std::string result(text);
	std::transform(result.begin(),
	               result.end(),
	               result.begin(),
	               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return result;
}

std::optional<std::string> KeyboardCommandProcessor::FirstToken(const std::string& args,
                                                                std::string& remainder_out)
{
	const auto trimmed = Trim(args);
	if (trimmed.empty()) {
		remainder_out.clear();
		return std::nullopt;
	}

	const auto pos = trimmed.find_first_of(" \t");
	if (pos == std::string::npos) {
		remainder_out.clear();
		return trimmed;
	}

	remainder_out = Trim(trimmed.substr(pos + 1));
	return trimmed.substr(0, pos);
}

std::vector<std::string> KeyboardCommandProcessor::ActiveKeys() const
{
	std::vector<std::string> keys;
	keys.reserve(m_pressed.size());
	for (const auto& entry : m_pressed) {
		keys.push_back(entry.second);
	}
	std::sort(keys.begin(), keys.end());
	return keys;
}

} // namespace textmode
