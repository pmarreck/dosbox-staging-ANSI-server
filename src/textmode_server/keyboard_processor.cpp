// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/keyboard_processor.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace textmode {

namespace {

using KeyMap = std::unordered_map<std::string, KBD_KEYS>;

const KeyMap& get_key_map()
{
	static const KeyMap map = {
	        {"ESC", KBD_esc},
	        {"ESCAPE", KBD_esc},
	        {"TAB", KBD_tab},
	        {"BACKSPACE", KBD_backspace},
	        {"BKSP", KBD_backspace},
	        {"ENTER", KBD_enter},
	        {"RETURN", KBD_enter},
	        {"SPACE", KBD_space},
	        {"SPACEBAR", KBD_space},

	        {"LEFTALT", KBD_leftalt},
	        {"LALT", KBD_leftalt},
	        {"ALT", KBD_leftalt},
	        {"RIGHTALT", KBD_rightalt},
	        {"RALT", KBD_rightalt},

	        {"LEFTCTRL", KBD_leftctrl},
	        {"LCTRL", KBD_leftctrl},
	        {"CTRL", KBD_leftctrl},
	        {"CONTROL", KBD_leftctrl},
	        {"RIGHTCTRL", KBD_rightctrl},
	        {"RCTRL", KBD_rightctrl},

	        {"LEFTSHIFT", KBD_leftshift},
	        {"LSHIFT", KBD_leftshift},
	        {"SHIFT", KBD_leftshift},
	        {"RIGHTSHIFT", KBD_rightshift},
	        {"RSHIFT", KBD_rightshift},

	        {"LEFTGUI", KBD_leftgui},
	        {"LGUI", KBD_leftgui},
	        {"LWIN", KBD_leftgui},
	        {"GUI", KBD_leftgui},
	        {"WIN", KBD_leftgui},
	        {"WINDOWS", KBD_leftgui},
	        {"RIGHTGUI", KBD_rightgui},
	        {"RGUI", KBD_rightgui},
	        {"RWIN", KBD_rightgui},

	        {"CAPSLOCK", KBD_capslock},
	        {"CAPS", KBD_capslock},
	        {"NUMLOCK", KBD_numlock},
	        {"NUM", KBD_numlock},
	        {"SCROLLLOCK", KBD_scrolllock},
	        {"SCROLL", KBD_scrolllock},

	        {"GRAVE", KBD_grave},
	        {"BACKQUOTE", KBD_grave},
	        {"BACKTICK", KBD_grave},
	        {"MINUS", KBD_minus},
	        {"HYPHEN", KBD_minus},
	        {"EQUALS", KBD_equals},
	        {"PLUS", KBD_equals},
	        {"BACKSLASH", KBD_backslash},
	        {"BSLASH", KBD_backslash},
	        {"LEFTBRACKET", KBD_leftbracket},
	        {"LBRACKET", KBD_leftbracket},
	        {"OPENBRACKET", KBD_leftbracket},
	        {"RIGHTBRACKET", KBD_rightbracket},
	        {"RBRACKET", KBD_rightbracket},
	        {"CLOSEBRACKET", KBD_rightbracket},
	        {"SEMICOLON", KBD_semicolon},
	        {"COLON", KBD_semicolon},
	        {"APOSTROPHE", KBD_quote},
	        {"QUOTE", KBD_quote},
	        {"OEM102", KBD_oem102},
	        {"LESSGREATER", KBD_oem102},
	        {"PERIOD", KBD_period},
	        {"DOT", KBD_period},
	        {"COMMA", KBD_comma},
	        {"SLASH", KBD_slash},
	        {"FORWARDSLASH", KBD_slash},
	        {"ABNT1", KBD_abnt1},

	        {"PRINTSCREEN", KBD_printscreen},
	        {"PRTSC", KBD_printscreen},
	        {"SYSRQ", KBD_printscreen},
	        {"PAUSE", KBD_pause},
	        {"BREAK", KBD_pause},

	        {"INSERT", KBD_insert},
	        {"INS", KBD_insert},
	        {"DELETE", KBD_delete},
	        {"DEL", KBD_delete},
	        {"HOME", KBD_home},
	        {"END", KBD_end},
	        {"PAGEUP", KBD_pageup},
	        {"PGUP", KBD_pageup},
	        {"PAGEDOWN", KBD_pagedown},
	        {"PGDN", KBD_pagedown},
	        {"LEFT", KBD_left},
	        {"LEFTARROW", KBD_left},
	        {"UP", KBD_up},
	        {"UPARROW", KBD_up},
	        {"DOWN", KBD_down},
	        {"DOWNARROW", KBD_down},
	        {"RIGHT", KBD_right},
	        {"RIGHTARROW", KBD_right},

	        {"KP0", KBD_kp0},
	        {"NUMPAD0", KBD_kp0},
	        {"KP1", KBD_kp1},
	        {"NUMPAD1", KBD_kp1},
	        {"KP2", KBD_kp2},
	        {"NUMPAD2", KBD_kp2},
	        {"KP3", KBD_kp3},
	        {"NUMPAD3", KBD_kp3},
	        {"KP4", KBD_kp4},
	        {"NUMPAD4", KBD_kp4},
	        {"KP5", KBD_kp5},
	        {"NUMPAD5", KBD_kp5},
	        {"KP6", KBD_kp6},
	        {"NUMPAD6", KBD_kp6},
	        {"KP7", KBD_kp7},
	        {"NUMPAD7", KBD_kp7},
	        {"KP8", KBD_kp8},
	        {"NUMPAD8", KBD_kp8},
	        {"KP9", KBD_kp9},
	        {"NUMPAD9", KBD_kp9},
	        {"KPDIVIDE", KBD_kpdivide},
	        {"NUMPADDIVIDE", KBD_kpdivide},
	        {"KPMULTIPLY", KBD_kpmultiply},
	        {"NUMPADMULTIPLY", KBD_kpmultiply},
	        {"KPMINUS", KBD_kpminus},
	        {"NUMPADMINUS", KBD_kpminus},
	        {"KPPLUS", KBD_kpplus},
	        {"NUMPADPLUS", KBD_kpplus},
	        {"KPENTER", KBD_kpenter},
	        {"NUMPADENTER", KBD_kpenter},
	        {"KPPERIOD", KBD_kpperiod},
	        {"NUMPADDECIMAL", KBD_kpperiod},
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
		return error_response("empty command");
	}

	const auto space_pos = trimmed.find(' ');
	const auto verb      = trimmed.substr(0, space_pos);
	const auto verb_upper = ToUpper(verb);
	const auto args = (space_pos == std::string::npos)
	                          ? std::string()
	                          : Trim(trimmed.substr(space_pos + 1));

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
	return response;
}

void KeyboardCommandProcessor::Reset()
{
	for (const auto key : m_pressed) {
		m_sink(key, false);
	}
	m_pressed.clear();
}

CommandResponse KeyboardCommandProcessor::HandlePress(const std::string& args)
{
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		return error_response("unknown key");
	}

	if (m_pressed.contains(*key)) {
		return error_response("key already down");
	}

	m_sink(*key, true);
	m_sink(*key, false);
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleDown(const std::string& args)
{
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		return error_response("unknown key");
	}

	if (m_pressed.contains(*key)) {
		return error_response("key already down");
	}

	m_sink(*key, true);
	m_pressed.insert(*key);
	return ok_response();
}

CommandResponse KeyboardCommandProcessor::HandleUp(const std::string& args)
{
	std::string remainder;
	const auto token = FirstToken(args, remainder);
	if (!token) {
		return error_response("missing key");
	}
	if (!remainder.empty()) {
		return error_response("unexpected arguments");
	}

	const auto key = ParseKeyName(*token);
	if (!key) {
		return error_response("unknown key");
	}

	const auto it = m_pressed.find(*key);
	if (it == m_pressed.end()) {
		return error_response("key not down");
	}

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
	const auto upper = ToUpper(name);
	if (upper.empty()) {
		return std::nullopt;
	}

	if (upper.size() == 1) {
		return map_single_character(upper[0]);
	}

	if (const auto f_key = map_f_key(upper); f_key) {
		return f_key;
	}

	if (const auto it = get_key_map().find(upper); it != get_key_map().end()) {
		return it->second;
	}

	return std::nullopt;
}

const std::vector<std::string>& KeyboardCommandProcessor::GetKeyNames()
{
	static const std::vector<std::string> names = [] {
		std::vector<std::string> result;
		const auto& map = get_key_map();
		result.reserve(map.size() + 26 + 10 + 24);
		for (const auto& entry : map) {
			result.push_back(entry.first);
		}
		for (int f = 1; f <= 24; ++f) {
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

} // namespace textmode
