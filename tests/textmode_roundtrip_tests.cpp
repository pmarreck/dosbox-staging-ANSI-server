// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#ifndef _WIN32

#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <spawn.h>
#include <map>
#include <sstream>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

constexpr uint16_t kTextPort     = 6200;
constexpr uint16_t kKeyboardPort = 6201;

struct SocketGuard {
	explicit SocketGuard(TCPsocket sock = nullptr) : socket(sock) {}

	~SocketGuard()
	{
		if (socket) {
			SDLNet_TCP_Close(socket);
		}
	}

	SocketGuard(const SocketGuard&)            = delete;
	SocketGuard& operator=(const SocketGuard&) = delete;

	SocketGuard(SocketGuard&& other) noexcept : socket(other.socket)
	{
		other.socket = nullptr;
	}

	SocketGuard& operator=(SocketGuard&& other) noexcept
	{
		if (this != &other) {
			if (socket) {
				SDLNet_TCP_Close(socket);
			}
			socket       = other.socket;
			other.socket = nullptr;
		}
		return *this;
	}

	TCPsocket socket = nullptr;
};

class PosixProcess {
public:
	explicit PosixProcess(const std::vector<std::string>& args)
	{
		EXPECT_FALSE(args.empty());
		argv.reserve(args.size() + 1);
		for (const auto& arg : args) {
			argv.push_back(const_cast<char*>(arg.c_str()));
		}
		argv.push_back(nullptr);

		const int rc = posix_spawn(&pid, argv.front(), nullptr, nullptr, argv.data(), environ);
		EXPECT_EQ(rc, 0) << "posix_spawn failed";
	}

	~PosixProcess()
	{
		if (pid > 0) {
			kill(pid, SIGTERM);
			waitpid(pid, nullptr, 0);
		}
	}

	int WaitForExit(std::chrono::seconds timeout)
	{
		if (pid <= 0) {
			return -1;
		}

		const auto deadline = std::chrono::steady_clock::now() + timeout;
		int status         = 0;
		for (;;) {
			const pid_t result = waitpid(pid, &status, WNOHANG);
			if (result == pid) {
				pid = -1;
				return status;
			}
			if (std::chrono::steady_clock::now() >= deadline) {
				kill(pid, SIGKILL);
				waitpid(pid, &status, 0);
				pid = -1;
				return status;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}

private:
	pid_t pid = -1;
	std::vector<char*> argv = {};
};

void WriteConfig(const std::filesystem::path& path)
{
	std::ofstream cfg(path);
	ASSERT_TRUE(cfg.is_open());

	cfg << "[sdl]\n";
	cfg << "output=surface\n";
	cfg << "fullscreen=false\n";
	cfg << "mapperfile=mapper.txt\n";
	cfg << "[cpu]\n";
	cfg << "cycles=fixed 1000\n";
	cfg << "[textmode_server]\n";
	cfg << "enable=true\n";
	cfg << "port=" << kTextPort << "\n";
	cfg << "show_attributes=false\n";
	cfg << "sentinel=*\n";
	cfg << "keyboard_enable=true\n";
	cfg << "keyboard_port=" << kKeyboardPort << "\n";
	cfg << "[autoexec]\n";
	cfg << "@echo off\n";
	cfg << "cls\n";
	cfg << "echo TEXTMODE ROUNDTRIP READY\n";
}

std::filesystem::path FindDosboxExecutable()
{
	if (const char* env_path = std::getenv("DOSBOX_TEST_BIN")) {
		std::filesystem::path candidate(env_path);
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	const auto cwd = std::filesystem::current_path();
	const std::array<std::filesystem::path, 3> candidates = {
	        cwd / "dosbox",
	        cwd / "src" / "dosbox",
	        cwd.parent_path() / "dosbox",
	};

	for (const auto& candidate : candidates) {
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}
	}

	return {};
}

TCPsocket WaitForConnection(const uint16_t port,
                            const std::chrono::milliseconds timeout)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		IPaddress address = {};
		if (SDLNet_ResolveHost(&address, "127.0.0.1", port) == 0) {
			TCPsocket socket = SDLNet_TCP_Open(&address);
			if (socket) {
				return socket;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return nullptr;
}

bool SendAll(TCPsocket socket, const std::string& data)
{
	size_t sent_total = 0;
	while (sent_total < data.size()) {
		const auto chunk = static_cast<int>(data.size() - sent_total);
		const int sent   = SDLNet_TCP_Send(socket, data.data() + sent_total, chunk);
		if (sent <= 0) {
			return false;
		}
		sent_total += static_cast<size_t>(sent);
	}
	return true;
}

std::string ReceiveResponse(TCPsocket socket,
                            const std::chrono::milliseconds timeout)
{
	std::string buffer;
	SDLNet_SocketSet set = SDLNet_AllocSocketSet(1);
	SDLNet_TCP_AddSocket(set, socket);

	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (SDLNet_CheckSockets(set, 50) <= 0) {
			continue;
		}
		if (!SDLNet_SocketReady(socket)) {
			continue;
		}

		char temp[1024];
		const int received = SDLNet_TCP_Recv(socket, temp, sizeof(temp));
		if (received <= 0) {
			break;
		}
		buffer.append(temp, received);
		if (received < static_cast<int>(sizeof(temp))) {
			break;
		}
	}

	SDLNet_TCP_DelSocket(set, socket);
	SDLNet_FreeSocketSet(set);
	return buffer;
}

bool EnsureSend(SocketGuard& guard, const std::string& payload)
{
	if (guard.socket && SendAll(guard.socket, payload)) {
		return true;
	}

	if (guard.socket) {
		SDLNet_TCP_Close(guard.socket);
		guard.socket = nullptr;
	}

	guard.socket = WaitForConnection(kTextPort, std::chrono::seconds(2));
	if (!guard.socket) {
		return false;
	}
	return SendAll(guard.socket, payload);
}

std::string StripAnsiAndNull(const std::string& text)
{
	std::string result;
	result.reserve(text.size());

	for (size_t i = 0; i < text.size();) {
		const unsigned char ch = static_cast<unsigned char>(text[i]);
		if (ch == 0x1b) {
			++i;
			if (i < text.size() && text[i] == '[') {
				++i;
				while (i < text.size()) {
					const unsigned char code = static_cast<unsigned char>(text[i]);
					++i;
					if (code >= '@' && code <= '~') {
						break;
					}
				}
			} else if (i < text.size()) {
				++i;
			}
			continue;
		}
		if (ch == '\r') {
			++i;
			continue;
		}
		result.push_back(ch == '\0' ? ' ' : static_cast<char>(ch));
		++i;
	}

	return result;
}

struct ParsedFrame {
	std::map<std::string, std::string> metadata = {};
	std::string payload_raw;
	std::string payload_plain;
};

std::optional<ParsedFrame> ParseFrame(const std::string& frame)
{
	std::istringstream stream(frame);
	std::string line;
	std::string sentinel;
	bool in_payload = false;
	std::ostringstream payload;
	ParsedFrame result;
	bool first = true;

	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (sentinel.empty()) {
			const auto meta_pos     = line.find("META");
			const auto payload_pos  = line.find("PAYLOAD");
			const auto sentinel_pos = (meta_pos != std::string::npos) ? meta_pos : payload_pos;
			if (sentinel_pos != std::string::npos && sentinel_pos > 0) {
				sentinel = line.substr(0, sentinel_pos);
			}
		}

		if (!in_payload) {
			if (!sentinel.empty() &&
			    line.rfind(sentinel + "PAYLOAD", 0) == 0) {
				in_payload = true;
			}
			if (!sentinel.empty() &&
			    line.rfind(sentinel + "META ", 0) == 0) {
				auto rest = line.substr(sentinel.size() + 5);
				const auto eq_pos = rest.find('=');
				if (eq_pos != std::string::npos) {
					auto key   = rest.substr(0, eq_pos);
					auto value = rest.substr(eq_pos + 1);
					result.metadata.emplace(std::move(key), std::move(value));
				}
			}
			continue;
		}

		if (!first) {
			payload << '\n';
		}
		payload << line;
		first = false;
	}

	if (!in_payload) {
		return std::nullopt;
	}

	result.payload_raw   = payload.str();
	result.payload_plain = StripAnsiAndNull(result.payload_raw);
	return result;
}

std::optional<std::pair<int, int>> ExtractCursor(const ParsedFrame& frame)
{
	const auto it = frame.metadata.find("cursor");
	if (it == frame.metadata.end()) {
		return std::nullopt;
	}

	const auto& value = it->second;
	const auto comma = value.find(',');
	if (comma == std::string::npos) {
		return std::nullopt;
	}

	const auto row_str = value.substr(0, comma);
	const auto rest    = value.substr(comma + 1);
	const auto space   = rest.find(' ');
	const auto col_str = (space == std::string::npos) ? rest : rest.substr(0, space);

	try {
		const int row = std::stoi(row_str);
		const int col = std::stoi(col_str);
		return std::make_pair(row, col);
	} catch (const std::exception&) {
		return std::nullopt;
	}
}

std::optional<std::map<std::string, int>> ParseStatsLine(const std::string& stats)
{
	std::map<std::string, int> values;
	std::istringstream stream(stats);
	std::string token;
	while (stream >> token) {
		if (!token.empty() && token.back() == '\n') {
			token.pop_back();
		}
		const auto eq_pos = token.find('=');
		if (eq_pos == std::string::npos) {
			continue;
		}
		const auto key   = token.substr(0, eq_pos);
		const auto value = token.substr(eq_pos + 1);
		try {
			values[key] = std::stoi(value);
		} catch (const std::exception&) {
			return std::nullopt;
		}
	}

	if (values.empty()) {
		return std::nullopt;
	}
	return values;
}

} // namespace

TEST(TextModeRoundTripTest, EchoesInjectedKeys)
{
	if (SDLNet_Init() != 0) {
		FAIL() << "SDL_net initialisation failed";
	}
	struct NetCleanup {
		~NetCleanup() { SDLNet_Quit(); }
	} net_cleanup;

	setenv("SDL_VIDEODRIVER", "dummy", 1);
	setenv("SDL_AUDIODRIVER", "dummy", 1);

	const auto temp_dir = std::filesystem::temp_directory_path() /
	                     ("dosbox-roundtrip-" + std::to_string(getpid()) + "-" +
	                      std::to_string(std::chrono::steady_clock::now()
	                                               .time_since_epoch()
	                                               .count()));
	std::error_code ec;
	std::filesystem::create_directories(temp_dir, ec);
	ASSERT_FALSE(ec) << ec.message();

	const auto config_path = temp_dir / "roundtrip.conf";

	WriteConfig(config_path);

	const auto dosbox_path = FindDosboxExecutable();
	ASSERT_FALSE(dosbox_path.empty()) << "Unable to locate dosbox executable";

	std::vector<std::string> args = {
	        dosbox_path.string(), "-conf", config_path.string(), "-noconsole"};

	PosixProcess process(args);

	SocketGuard text_socket(WaitForConnection(kTextPort, std::chrono::seconds(5)));
	ASSERT_NE(text_socket.socket, nullptr) << "Failed to connect to frame server";

	SocketGuard keyboard_socket(WaitForConnection(kKeyboardPort, std::chrono::seconds(5)));
	ASSERT_NE(keyboard_socket.socket, nullptr) << "Failed to connect to keyboard server";

	constexpr int kMaxFrameAttempts = 60;
	std::optional<std::pair<int, int>> initial_cursor;
	std::string last_frame;
	bool prompt_seen = false;

	for (int attempt = 0; attempt < kMaxFrameAttempts && !prompt_seen; ++attempt) {
		if (!EnsureSend(text_socket, std::string("GET\n"))) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}
		last_frame = ReceiveResponse(text_socket.socket, std::chrono::milliseconds(500));
		if (last_frame.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		const auto parsed = ParseFrame(last_frame);
		if (parsed) {
			if (!initial_cursor) {
				initial_cursor = ExtractCursor(*parsed);
			}
			const bool has_prompt = parsed->payload_plain.find("Z:\\>") != std::string::npos;
			const bool has_banner =
			        parsed->payload_plain.find("TEXTMODE ROUNDTRIP READY") != std::string::npos;
			if (has_prompt && has_banner) {
				prompt_seen = true;
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	ASSERT_TRUE(prompt_seen) << "Did not observe command prompt\n" << last_frame;
	ASSERT_TRUE(initial_cursor.has_value()) << "Missing cursor metadata before typing";

	ASSERT_TRUE(SendAll(keyboard_socket.socket, std::string("STATS\n")));
	const auto stats_baseline_str =
	        ReceiveResponse(keyboard_socket.socket, std::chrono::milliseconds(500));
	std::ofstream(temp_dir / "stats_baseline.txt") << stats_baseline_str;
	const auto stats_baseline = ParseStatsLine(stats_baseline_str);
	ASSERT_TRUE(stats_baseline.has_value()) << "Unable to parse baseline stats\n"
	                                       << stats_baseline_str;
	const auto get_value = [](const std::map<std::string, int>& values,
	                         const std::string& key) {
		if (const auto it = values.find(key); it != values.end()) {
			return it->second;
		}
		return 0;
	};
	const int baseline_success = get_value(*stats_baseline, "success");
	const int baseline_failures = get_value(*stats_baseline, "failures");

	constexpr std::string_view text_to_type = "hello";
	for (const char ch : text_to_type) {
		std::string command = "APPLY keys=";
		command.push_back(ch);
		command += "\n";
		ASSERT_TRUE(EnsureSend(text_socket, command));
		(void)ReceiveResponse(text_socket.socket, std::chrono::milliseconds(500));
	}

	ASSERT_TRUE(SendAll(keyboard_socket.socket, std::string("STATS\n")));
	const auto stats_after_apply =
	        ReceiveResponse(keyboard_socket.socket, std::chrono::milliseconds(500));
	std::ofstream(temp_dir / "stats_after_apply.txt") << stats_after_apply;
	const auto stats_after = ParseStatsLine(stats_after_apply);
	ASSERT_TRUE(stats_after.has_value()) << "Unable to parse stats after typing\n"
	                                   << stats_after_apply;
	const int success_after  = get_value(*stats_after, "success");
	const int failures_after = get_value(*stats_after, "failures");
	EXPECT_GE(success_after - baseline_success,
	          static_cast<int>(text_to_type.size()));
	EXPECT_EQ(failures_after, baseline_failures);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	const int baseline_row = initial_cursor->first;
	const int baseline_col = initial_cursor->second;
	const int expected_col = baseline_col + static_cast<int>(text_to_type.size());
	bool text_rendered     = false;
	bool cursor_advanced   = false;

	for (int attempt = 0; attempt < kMaxFrameAttempts; ++attempt) {
		if (!EnsureSend(text_socket, std::string("GET\n"))) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}
		last_frame = ReceiveResponse(text_socket.socket, std::chrono::milliseconds(500));
		if (last_frame.empty()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		const auto parsed = ParseFrame(last_frame);
		if (parsed) {
			const std::string text_raw(text_to_type);
			std::string text_upper(text_to_type);
			std::transform(text_upper.begin(), text_upper.end(), text_upper.begin(), [](unsigned char ch) {
				return static_cast<char>(std::toupper(ch));
			});

			if (const auto cursor = ExtractCursor(*parsed)) {
				if (cursor->first > baseline_row ||
				    (cursor->first == baseline_row && cursor->second >= expected_col)) {
					cursor_advanced = true;
				}
			}
			const auto plain_hit = parsed->payload_plain.find(text_to_type) != std::string::npos;
			const auto raw_hit   = parsed->payload_raw.find(text_raw) != std::string::npos;
			const auto raw_upper = parsed->payload_raw.find(text_upper) != std::string::npos;
			if (plain_hit || raw_hit || raw_upper) {
				text_rendered = true;
			}
		}

		if (cursor_advanced || text_rendered) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	EXPECT_TRUE(cursor_advanced || text_rendered)
	        << "Keyboard input not reflected on screen\n" << last_frame;

	ASSERT_TRUE(EnsureSend(text_socket, "EXIT\n"));
	std::string exit_ack;
	for (int attempt = 0; attempt < 5; ++attempt) {
		const auto chunk =
		        ReceiveResponse(text_socket.socket, std::chrono::milliseconds(500));
		exit_ack += chunk;
		if (exit_ack.find("OK\n") != std::string::npos || chunk.empty()) {
			break;
		}
	}
	EXPECT_NE(exit_ack.find("OK\n"), std::string::npos)
	        << "Unexpected EXIT acknowledgement\n" << exit_ack;

	const int status = process.WaitForExit(std::chrono::seconds(10));
	EXPECT_TRUE(WIFEXITED(status)) << "dosbox did not exit cleanly";
	if (WIFEXITED(status)) {
		EXPECT_EQ(WEXITSTATUS(status), 0);
	}

	// std::filesystem::remove_all(temp_dir, ec);
}

#else

TEST(TextModeRoundTripTest, EchoesInjectedKeys)
{
	GTEST_SKIP() << "Round-trip integration test not implemented on this platform.";
}

#endif
