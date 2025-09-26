// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/command_processor.h"
#include "textmode_server/keyboard_processor.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#if defined(ENABLE_TEXTMODE_QUEUE_TRACE)
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#endif
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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

struct TypeToken {
	std::string text;
	bool is_quoted = false;
};

std::vector<TypeToken> tokenize_type_arguments(const std::string& argument)
{
	std::vector<TypeToken> tokens;
	std::string current;
	bool in_quotes = false;

	const auto flush = [&](bool quoted) {
		if (!current.empty()) {
			tokens.push_back({current, quoted});
			current.clear();
		}
	};

	for (size_t i = 0; i < argument.size(); ++i) {
		const char ch = argument[i];
		if (in_quotes) {
			if (ch == '\\' && i + 1 < argument.size()) {
				const char next = argument[++i];
				current.push_back(next);
			} else if (ch == '"') {
				flush(true);
				in_quotes = false;
			} else {
				current.push_back(ch);
			}
		} else {
			if (std::isspace(static_cast<unsigned char>(ch))) {
				flush(false);
			} else if (ch == '"') {
				flush(false);
				in_quotes = true;
			} else {
				current.push_back(ch);
			}
		}
	}

	if (in_quotes) {
		flush(true);
	} else {
		flush(false);
	}

	return tokens;
}

void log_token_warning(const std::string& token, std::string_view reason)
{
	if (!token.empty()) {
		std::fprintf(stderr,
		            "TEXTMODE: TYPE token '%s' skipped: %.*s\n",
		            token.c_str(),
		            static_cast<int>(reason.size()), reason.data());
	} else {
		std::fprintf(stderr,
		            "TEXTMODE: TYPE token skipped: %.*s\n",
		            static_cast<int>(reason.size()), reason.data());
	}
}

void log_case_warning(const std::string& provided, const std::string& expected)
{
	std::fprintf(stderr,
	            "TEXTMODE: TYPE token '%s' skipped: case-sensitive token is '%s'\n",
	            provided.c_str(), expected.c_str());
}

void log_command_case_warning(const std::string& provided, const std::string& expected)
{
	std::fprintf(stderr,
	            "TEXTMODE: command '%s' rejected: use '%s'\n",
	            provided.c_str(), expected.c_str());
}

const std::unordered_map<std::string, std::string>& command_case_lookup()
{
	static const std::unordered_map<std::string, std::string> lookup = {
	        {"TYPE", "TYPE"}, {"GET", "GET"}, {"VIEW", "VIEW"},
	        {"STATS", "STATS"}, {"EXIT", "EXIT"}};
	return lookup;
}

std::optional<std::string> suggest_command(const std::string& verb)
{
	const auto upper = to_upper(verb);
	const auto& lookup = command_case_lookup();
	const auto it      = lookup.find(upper);
	if (it != lookup.end() && it->second != verb) {
		return it->second;
	}
	return std::nullopt;
}

const std::unordered_map<std::string, std::string>& key_case_lookup()
{
	static const std::unordered_map<std::string, std::string> lookup = [] {
		std::unordered_map<std::string, std::string> result;
		for (const auto& name : textmode::KeyboardCommandProcessor::GetKeyNames()) {
			const auto upper = to_upper(name);
			result.emplace(upper, name);
		}
		return result;
	}();
	return lookup;
}

std::optional<std::string> suggest_key_token(const std::string& token)
{
	const auto upper = to_upper(token);
	const auto& lookup = key_case_lookup();
	const auto it      = lookup.find(upper);
	if (it != lookup.end() && it->second != token) {
		return it->second;
	}
	return std::nullopt;
}

bool send_keyboard_command(const std::function<CommandResponse(const std::string&)>& handler,
	                      std::string_view verb,
	                      const std::string& key)
{
	if (!handler) {
		return false;
	}
	std::string command;
	command.reserve(verb.size() + 1 + key.size());
	command.append(verb);
	command.push_back(' ');
	command.append(key);
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

std::optional<std::chrono::milliseconds> parse_delay_token(const std::string& token,
                                                          bool& case_error)
{
	case_error = false;
	constexpr std::string_view suffix = "ms";
	if (token.size() > suffix.size() &&
	    token.compare(token.size() - suffix.size(), suffix.size(), suffix) == 0) {
		const auto digits = token.substr(0, token.size() - suffix.size());
		if (!digits.empty() &&
		    std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
		        return std::isdigit(c) != 0;
		    })) {
			try {
				const auto value = std::stoul(digits);
				if (value == 0) {
					return std::nullopt;
				}
				return std::chrono::milliseconds(value);
			} catch (const std::exception&) {
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

	const auto upper = to_upper(token);
	if (upper.size() > suffix.size() &&
	    upper.compare(upper.size() - suffix.size(), suffix.size(), "MS") == 0) {
		case_error = true;
	}
	return std::nullopt;
}

std::optional<uint32_t> parse_frames_token(const std::string& token,
                                           bool& case_error,
                                           std::string& expected)
{
	case_error = false;
	expected.clear();
	if (token.empty()) {
		return std::nullopt;
	}

	auto parse_digits = [](const std::string& digits) -> std::optional<uint32_t> {
		if (digits.empty() ||
		    !std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
		        return std::isdigit(c) != 0;
		    })) {
			return std::nullopt;
		}
		try {
			const auto value = std::stoul(digits);
			if (value == 0 || value > std::numeric_limits<uint32_t>::max()) {
				return std::nullopt;
			}
			return static_cast<uint32_t>(value);
		} catch (const std::exception&) {
			return std::nullopt;
		}
	};

	constexpr std::string_view suffix_plural = "frames";
	constexpr std::string_view suffix_single = "frame";

	if (token.size() > suffix_plural.size() &&
	    token.compare(token.size() - suffix_plural.size(),
	                 suffix_plural.size(),
	                 suffix_plural) == 0) {
		const auto digits = token.substr(0, token.size() - suffix_plural.size());
		return parse_digits(digits);
	}
	if (token.size() > suffix_single.size() &&
	    token.compare(token.size() - suffix_single.size(),
	                 suffix_single.size(),
	                 suffix_single) == 0) {
		const auto digits = token.substr(0, token.size() - suffix_single.size());
		return parse_digits(digits);
	}

	const auto upper = to_upper(token);
	if (upper.size() > suffix_plural.size() &&
	    upper.compare(upper.size() - suffix_plural.size(),
	                 suffix_plural.size(),
	                 to_upper(std::string(suffix_plural))) == 0) {
		case_error = true;
		expected    = token.substr(0, token.size() - suffix_plural.size()) + "frames";
	}
	if (upper.size() > suffix_single.size() &&
	    upper.compare(upper.size() - suffix_single.size(),
	                 suffix_single.size(),
	                 to_upper(std::string(suffix_single))) == 0) {
		case_error = true;
		expected    = token.substr(0, token.size() - suffix_single.size()) + "frame";
	}
	return std::nullopt;
}

struct CharacterMapping {
	std::string key;
	bool requires_shift = false;
};

CharacterMapping make_letter_mapping(char ch)
{
	const bool is_upper = std::isupper(static_cast<unsigned char>(ch)) != 0;
	const char key_char = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
	return CharacterMapping{std::string(1, key_char), is_upper};
}

std::optional<CharacterMapping> map_character_to_key(char ch)
{
	if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
		return make_letter_mapping(ch);
	}
	if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
		return CharacterMapping{std::string(1, ch), false};
	}

	static const std::unordered_map<char, CharacterMapping> symbol_map = {
	        {' ', {"Space", false}},
	        {'\n', {"Enter", false}},
	        {'\r', {"Enter", false}},
	        {'\t', {"Tab", false}},
	        {'`', {"Grave", false}},
	        {'~', {"Grave", true}},
	        {'-', {"Minus", false}},
	        {'_', {"Minus", true}},
	        {'=', {"Equals", false}},
	        {'+', {"Equals", true}},
	        {'[', {"LeftBracket", false}},
	        {'{', {"LeftBracket", true}},
	        {']', {"RightBracket", false}},
	        {'}', {"RightBracket", true}},
	        {'\\', {"Backslash", false}},
	        {'|', {"Backslash", true}},
	        {';', {"Semicolon", false}},
	        {':', {"Semicolon", true}},
	        {'\'', {"Quote", false}},
	        {'"', {"Quote", true}},
	        {',', {"Comma", false}},
	        {'<', {"Comma", true}},
	        {'.', {"Period", false}},
	        {'>', {"Period", true}},
	        {'/', {"Slash", false}},
	        {'?', {"Slash", true}},
	        {'!', {"1", true}},
	        {'@', {"2", true}},
	        {'#', {"3", true}},
	        {'$', {"4", true}},
	        {'%', {"5", true}},
	        {'^', {"6", true}},
	        {'&', {"7", true}},
	        {'*', {"8", true}},
	        {'(', {"9", true}},
	        {')', {"0", true}},
	};

	const auto it = symbol_map.find(ch);
	if (it != symbol_map.end()) {
		return it->second;
	}

	return std::nullopt;
}

std::string describe_character(char ch)
{
	if (std::isprint(static_cast<unsigned char>(ch))) {
		return std::string(1, ch);
	}
	char buffer[8] = {};
	std::snprintf(buffer, sizeof(buffer), "0x%02X", static_cast<unsigned char>(ch));
	return std::string(buffer);
}

TypeAction make_key_action(TypeAction::Kind kind, std::string key)
{
	TypeAction action{};
	action.kind = kind;
	action.key  = std::move(key);
	return action;
}

TypeAction make_delay_ms_action(const std::chrono::milliseconds delay)
{
	TypeAction action{};
	action.kind     = TypeAction::Kind::DelayMs;
	action.delay_ms = delay;
	return action;
}

TypeAction make_delay_frames_action(const uint32_t frames)
{
	TypeAction action{};
	action.kind   = TypeAction::Kind::DelayFrames;
	action.frames = frames;
	return action;
}

void append_character_actions(const CharacterMapping& mapping,
	                        std::vector<TypeAction>& actions)
{
	if (mapping.requires_shift) {
		actions.push_back(make_key_action(TypeAction::Kind::Down, "Shift"));
	}
	actions.push_back(make_key_action(TypeAction::Kind::Press, mapping.key));
	if (mapping.requires_shift) {
		actions.push_back(make_key_action(TypeAction::Kind::Up, "Shift"));
	}
}

void append_string_actions(const std::string& text,
	                     const uint32_t interkey_frames,
	                     std::vector<TypeAction>& actions)
{
	for (size_t i = 0; i < text.size(); ++i) {
		const char ch = text[i];
		const auto mapping = map_character_to_key(ch);
		if (!mapping) {
			log_token_warning(describe_character(ch), "unsupported character");
			continue;
		}
		append_character_actions(*mapping, actions);
		const bool have_more = (i + 1) < text.size();
		if (have_more && interkey_frames > 0) {
			actions.push_back(make_delay_frames_action(interkey_frames));
		}
	}
}

bool append_key_token(const std::string& token, std::vector<TypeAction>& actions)
{
	if (token.empty()) {
		return false;
	}

	const auto canonical_backslash = [](const std::string& name) {
		return name == "\\" ? std::string("Backslash") : name;
	};

	const auto direct_candidate = canonical_backslash(token);
	if (textmode::KeyboardCommandProcessor::ParseKeyName(direct_candidate)) {
		actions.push_back(make_key_action(TypeAction::Kind::Press, direct_candidate));
		return true;
	}

	const auto ends_with = [](const std::string& value, std::string_view suffix) {
		return value.size() >= suffix.size() &&
		       value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
	};

	std::string base = token;
	bool request_down = false;
	bool request_up   = false;
	bool suffix_case_error = false;

	if (token.size() > 4 && ends_with(token, "Down")) {
		request_down = true;
		base.resize(base.size() - 4);
	} else if (token.size() > 2 && ends_with(token, "Up")) {
		request_up = true;
		base.resize(base.size() - 2);
	} else {
		const auto upper = to_upper(token);
		if (upper.size() > 4 && ends_with(upper, "DOWN")) {
			request_down      = true;
			suffix_case_error = true;
			base.resize(base.size() - 4);
		} else if (upper.size() > 2 && ends_with(upper, "UP")) {
			request_up        = true;
			suffix_case_error = true;
			base.resize(base.size() - 2);
		}
	}

	if (base.empty()) {
		return false;
	}

	base = canonical_backslash(base);

	if (const auto suggestion = suggest_key_token(base)) {
		std::string expected = *suggestion;
		if (request_down) {
			expected += "Down";
		} else if (request_up) {
			expected += "Up";
		}
		log_case_warning(token, expected);
		return false;
	}

	if (suffix_case_error) {
		std::string expected = base;
		if (request_down) {
			expected += "Down";
		} else if (request_up) {
			expected += "Up";
		}
		log_case_warning(token, expected);
		return false;
	}

	if (!textmode::KeyboardCommandProcessor::ParseKeyName(base)) {
		log_token_warning(token, "unrecognised token");
		return false;
	}

	const auto kind = request_down ? TypeAction::Kind::Down
	                              : request_up   ? TypeAction::Kind::Up
	                                             : TypeAction::Kind::Press;
	actions.push_back(make_key_action(kind, base));
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

class ImmediateTypeActionSink final : public ITypeActionSink {
public:
	CommandResponse Execute(const TypeCommandPlan& plan,
	                        const CommandOrigin& /*origin*/,
	                        const KeyboardHandler& keyboard_handler,
	                        const FrameProvider& frame_provider,
	                        CompletionCallback /*on_complete*/) override
	{
		for (const auto& action : plan.actions) {
			switch (action.kind) {
			case TypeAction::Kind::Press:
				send_keyboard_command(keyboard_handler, "PRESS", action.key);
				break;
			case TypeAction::Kind::Down:
				send_keyboard_command(keyboard_handler, "DOWN", action.key);
				break;
			case TypeAction::Kind::Up:
				send_keyboard_command(keyboard_handler, "UP", action.key);
				break;
			case TypeAction::Kind::DelayMs:
				if (action.delay_ms.count() > 0) {
					std::this_thread::sleep_for(action.delay_ms);
				}
				break;
			case TypeAction::Kind::DelayFrames:
				if (action.frames > 0) {
					// Approximate a 60Hz frame rate for immediate execution.
					constexpr auto frame_duration = std::chrono::milliseconds(16);
					std::this_thread::sleep_for(frame_duration * action.frames);
				}
				break;
			}
		}

	if (!plan.request_frame) {
		return {true, "OK\n"};
	}

	if (!frame_provider) {
		return {false, "ERR service unavailable\n"};
	}
	const auto result = frame_provider();
	if (!result.success) {
		return {false, "ERR " + result.error + "\n"};
	}
	return {true, result.frame};
	}
};

} // namespace

CommandProcessor::CommandProcessor(std::function<ServiceResult()> provider,
	                             std::function<CommandResponse(const std::string&)> keyboard_handler,
	                             std::function<void()> exit_handler,
	                             std::function<std::vector<std::string>()> keys_down_provider)
        : m_provider(std::move(provider)),
          m_keyboard_handler(std::move(keyboard_handler)),
          m_exit_handler(std::move(exit_handler)),
          m_keys_down_provider(std::move(keys_down_provider)),
          m_type_sink(std::make_shared<ImmediateTypeActionSink>()),
          m_active_origin(),
          m_requests(0),
          m_success(0),
          m_failures(0),
          m_exit_requested(false),
          m_macro_interkey_frames(0)
{}

CommandResponse CommandProcessor::HandleCommand(const std::string& command)
{
	const auto origin = m_active_origin.value_or(CommandOrigin{});
	return HandleCommandInternal(command, origin);
}

CommandResponse CommandProcessor::HandleCommand(const std::string& command,
	                                              const CommandOrigin& origin)
{
	const auto previous = m_active_origin;
	m_active_origin     = origin;
	const auto response = HandleCommand(command);
	m_active_origin     = previous;
	return response;
}

CommandResponse CommandProcessor::HandleCommandInternal(const std::string& raw_command,
	                                                     const CommandOrigin& origin)
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

	if (const auto suggestion = suggest_command(verb); suggestion) {
		log_command_case_warning(verb, *suggestion);
		return {false, "ERR commands are case-sensitive\n"};
	}

	if (verb_upper == "STATS") {
		std::ostringstream oss;
		oss << "requests=" << m_requests << ' '
		    << "success=" << m_success << ' '
		    << "failures=" << m_failures << ' ';
		std::string joined;
		if (m_keys_down_provider) {
			auto keys = m_keys_down_provider();
			std::sort(keys.begin(), keys.end());
			for (size_t i = 0; i < keys.size(); ++i) {
				if (i > 0) {
					joined.push_back(',');
				}
				joined.append(keys[i]);
			}
		}
		oss << "keys_down=" << joined << "\n";
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

	if (verb_upper == "GET" || verb_upper == "VIEW") {
		if (!m_provider) {
			return {false, "ERR service unavailable\n"};
		}

		++m_requests;
		bool showspc = false;
		if (!argument.empty()) {
			if (argument == "SHOWSPC") {
				showspc = true;
			} else if (to_upper(argument) == "SHOWSPC") {
				log_case_warning(argument, "SHOWSPC");
				showspc = true;
			}
		}
		const auto result  = m_provider();
		if (!result.success) {
			++m_failures;
			return {false, "ERR " + result.error + "\n"};
		}

		++m_success;
		return {true, showspc ? show_spaces(result.frame) : result.frame};
	}

	if (verb_upper == "TYPE") {
		return HandleTypeCommand(argument, origin);
	}

	return {false, "ERR unknown command\n"};
}

CommandResponse CommandProcessor::HandleTypeCommand(const std::string& argument,
	                                              const CommandOrigin& origin)
{
	++m_requests;
	if (!m_keyboard_handler) {
		++m_failures;
		return {false, "ERR keyboard unavailable\n"};
	}

	TypeCommandPlan plan;
	const auto tokens = tokenize_type_arguments(argument);
trace_log("type command argument='%s' tokens=%zu\n", argument.c_str(), tokens.size());

	for (const auto& token : tokens) {
		if (token.text.empty() && !token.is_quoted) {
			continue;
		}

		if (token.is_quoted) {
trace_log("type token string='%s'\n", token.text.c_str());
			append_string_actions(token.text, m_macro_interkey_frames, plan.actions);
			continue;
		}

		auto token_upper = to_upper(token.text);
trace_log("type token='%s' upper='%s'\n", token.text.c_str(), token_upper.c_str());
		if (token.text == "GET" || token.text == "VIEW") {
			plan.request_frame = true;
trace_log("type request_frame enabled by token='%s'\n", token.text.c_str());
			continue;
		}
		if (token_upper == "GET" || token_upper == "VIEW") {
			log_case_warning(token.text, token_upper == "GET" ? "GET" : "VIEW");
			plan.request_frame = true;
			continue;
		}

		bool delay_case_error = false;
		if (const auto delay = parse_delay_token(token.text, delay_case_error)) {
			plan.actions.push_back(make_delay_ms_action(*delay));
trace_log("type delay_ms=%lld\n", static_cast<long long>(delay->count()));
			continue;
		}
		if (delay_case_error) {
			const auto digits = token.text.substr(0, token.text.size() - 2);
			log_case_warning(token.text, digits + "ms");
			continue;
		}

		bool frames_case_error = false;
		std::string frames_expected;
		if (const auto frames = parse_frames_token(token.text, frames_case_error, frames_expected)) {
			plan.actions.push_back(make_delay_frames_action(*frames));
trace_log("type delay_frames=%u\n", *frames);
			continue;
		}
		if (frames_case_error && !frames_expected.empty()) {
			log_case_warning(token.text, frames_expected);
			continue;
		}

		if (append_key_token(token.text, plan.actions)) {
trace_log("type key token accepted='%s'\n", token.text.c_str());
			continue;
		}

		log_token_warning(token.text, "unrecognised token");
	}

	if (plan.request_frame && !plan.actions.empty()) {
trace_log("type request_frame with actions=%zu\n", plan.actions.size());
		const auto last_kind = plan.actions.back().kind;
		if (last_kind != TypeAction::Kind::DelayMs &&
		    last_kind != TypeAction::Kind::DelayFrames) {
			const uint32_t frames_to_wait = (m_macro_interkey_frames > 0)
			                                        ? m_macro_interkey_frames
			                                        : 1;
			plan.actions.push_back(make_delay_frames_action(frames_to_wait));
trace_log("type appended trailing delay_frames=%u\n", frames_to_wait);
		}
	}

	if (plan.actions.empty()) {
trace_log("type actions empty request_frame=%s\n", plan.request_frame ? "yes" : "no");
		if (!plan.request_frame) {
			++m_success;
			return {true, "OK\n"};
		}

		if (!m_provider) {
			++m_failures;
			return {false, "ERR service unavailable\n"};
		}

		const auto result = m_provider();
		if (!result.success) {
			++m_failures;
			return {false, "ERR " + result.error + "\n"};
		}

		++m_success;
		return {true, result.frame};
	}

	if (!m_type_sink) {
		m_type_sink = std::make_shared<ImmediateTypeActionSink>();
	}

	auto completion = [this](const bool success) {
		if (success) {
			++m_success;
		} else {
			++m_failures;
		}
	};

trace_log("type pre-exec sink=%s origin=%p queue_non_frame=%s allow_deferred=%s request_frame=%s\n",
          m_type_sink ? "set" : "null",
          reinterpret_cast<void*>(origin.client),
          m_queue_non_frame_commands ? "yes" : "no",
          m_allow_deferred_frames ? "yes" : "no",
          plan.request_frame ? "yes" : "no");

	bool use_queue = static_cast<bool>(m_type_sink);
	if (use_queue && origin.client == 0 && m_type_sink_requires_client) {
		use_queue = false;
	}
	if (use_queue && !plan.request_frame && !m_queue_non_frame_commands) {
		use_queue = false;
	}
	if (use_queue && !m_allow_deferred_frames) {
		use_queue = false;
	}
	trace_log("type execution mode queue=%s actions=%zu request_frame=%s\n",
	          use_queue ? "yes" : "no",
	          plan.actions.size(),
	          plan.request_frame ? "yes" : "no");

	auto sink = use_queue ? m_type_sink : std::make_shared<ImmediateTypeActionSink>();

	const auto response = sink->Execute(
	        plan, origin, m_keyboard_handler, m_provider, completion);

	if (!response.deferred) {
		if (response.ok) {
			++m_success;
		} else {
			++m_failures;
		}
	}

	return response;
}

bool CommandProcessor::ConsumeExitRequest()
{
	if (!m_exit_requested) {
		return false;
	}
	m_exit_requested = false;
	return true;
}

void CommandProcessor::SetTypeActionSink(std::shared_ptr<ITypeActionSink> sink)
{
	m_type_sink = std::move(sink);
	if (!m_type_sink) {
		m_type_sink = std::make_shared<ImmediateTypeActionSink>();
	}
	m_type_sink_requires_client = false;
}

void CommandProcessor::SetMacroInterkeyFrames(const uint32_t frames)
{
	m_macro_interkey_frames = frames;
}

void CommandProcessor::SetTypeSinkRequiresClient(const bool requires_client)
{
	m_type_sink_requires_client = requires_client;
}

void CommandProcessor::SetQueueNonFrameCommands(const bool enable)
{
	m_queue_non_frame_commands = enable;
}

void CommandProcessor::SetAllowDeferredFrames(const bool enable)
{
	m_allow_deferred_frames = enable;
}

} // namespace textmode
