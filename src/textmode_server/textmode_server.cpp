// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/textmode_server.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "dosbox.h"
#include "misc/logging.h"
#include "textmode_server/keyboard_processor.h"
#include "textmode_server/memory_access.h"
#include "textmode_server/queued_type_action_sink.h"
#include "textmode_server/server.h"
#include "hardware/input/keyboard.h"

namespace {

std::optional<textmode::ServiceConfig> g_active_config = std::nullopt;
std::optional<textmode::CommandProcessor> g_processor  = std::nullopt;
bool g_close_after_response = false;
std::unique_ptr<textmode::TextModeServer> g_server = nullptr;
std::unique_ptr<textmode::KeyboardCommandProcessor> g_keyboard_processor = nullptr;
std::shared_ptr<textmode::QueuedTypeActionSink> g_queued_sink = nullptr;

std::string ExpandEnv(const std::string& value)
{
	std::string result;
	result.reserve(value.size());

	size_t pos = 0;
	while (pos < value.size()) {
		const auto start = value.find("${", pos);
		if (start == std::string::npos) {
			result.append(value.substr(pos));
			break;
		}
		result.append(value.substr(pos, start - pos));
		const auto end = value.find('}', start + 2);
		if (end == std::string::npos) {
			result.append(value.substr(start));
			break;
		}
		const auto name = value.substr(start + 2, end - (start + 2));
		if (const char* env_value = std::getenv(name.c_str())) {
			result.append(env_value);
		}
		pos = end + 1;
	}

	return result;
}

uint32_t CombineSegmentOffset(const uint32_t segment, const uint32_t offset)
{
	const uint64_t address =
	        (static_cast<uint64_t>(segment) << 4) + static_cast<uint64_t>(offset);
	if (address > std::numeric_limits<uint32_t>::max()) {
		return std::numeric_limits<uint32_t>::max();
	}
	return static_cast<uint32_t>(address);
}

void EnsureServer()
{
	if (!g_server) {
		g_server = std::make_unique<textmode::TextModeServer>(textmode::MakeSdlNetBackend());
		if (g_server) {
			g_server->SetClientCloseCallback([](textmode::ClientHandle client) {
				if (g_queued_sink) {
					g_queued_sink->CancelClient(client);
				}
			});
		}
	}
}

void EnsureKeyboard()
{
	if (!g_keyboard_processor) {
		g_keyboard_processor = std::make_unique<textmode::KeyboardCommandProcessor>(
		        [](KBD_KEYS key, bool pressed) { KEYBOARD_AddKey(key, pressed); });
	}
}

textmode::ServiceResult ProvideFrame()
{
	const auto config = g_active_config.value_or(textmode::ServiceConfig{});
	std::vector<std::string> keys_down;
	if (g_keyboard_processor) {
		keys_down = g_keyboard_processor->ActiveKeys();
	}
	textmode::TextModeService service(config, std::move(keys_down));
	return service.GetFrame();
}

void ApplyConfigSection(Section* section)
{
	const auto* props = dynamic_cast<SectionProp*>(section);
	if (!props) {
		return;
	}

	textmode::ServiceConfig config{};
	config.enable          = props->GetBool("enable");
	config.port            = static_cast<uint16_t>(props->GetInt("port"));
	config.show_attributes     = props->GetBool("show_attributes");
	config.sentinel            = props->GetString("sentinel");
	config.close_after_response = props->GetBool("close_after_response");
	config.macro_interkey_frames = static_cast<uint32_t>(
	        std::max(0, props->GetInt("macro_interkey_frames")));
	config.inter_token_frame_delay = static_cast<uint32_t>(
	        std::max(0, props->GetInt("inter_token_frame_delay")));
	config.debug_segment = static_cast<uint32_t>(props->GetHex("debug_segment"));
	config.debug_offset  = static_cast<uint32_t>(props->GetHex("debug_offset"));
	config.debug_length  = static_cast<uint32_t>(std::max(0, props->GetInt("debug_length")));

	std::string auth_token = ExpandEnv(props->GetString("auth_token"));
	if (auth_token.empty()) {
		if (const char* env_token = std::getenv("DOSBOX_ANSI_AUTH_TOKEN")) {
			if (*env_token) {
				auth_token = env_token;
			}
		}
	}
	config.auth_token = std::move(auth_token);

	textmode::Configure(config);
}

} // namespace

void TEXTMODESERVER_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	constexpr auto only_at_start = Property::Changeable::OnlyAtStart;

	auto* section = conf->AddSectionProp("textmode_server", &ApplyConfigSection);
	assert(section);

	auto* enable = section->AddBool("enable", only_at_start, false);
	enable->SetHelp("Enable the text-mode frame server (off by default).");

	auto* port = section->AddInt("port", only_at_start, 6000);
	port->SetMinMax(1024, 65535);
	port->SetHelp(
	        "TCP port used by the server (6000 by default). Valid range is 1024-65535.");

	auto* show_attributes = section->AddBool("show_attributes", only_at_start, true);
	show_attributes->SetHelp(
	        "Emit ANSI colour escape sequences when true; emit plain text when false.");

	static constexpr char sentinel_default[] = "\xF0\x9F\x96\xB5";
	auto* sentinel = section->AddString("sentinel", only_at_start, sentinel_default);
	sentinel->SetHelp(
	        "UTF-8 sentinel glyph used to delimit metadata and payload lines (default 🖵).");

	auto* close_after_response = section->AddBool("close_after_response",
	                                             only_at_start,
	                                             false);
	close_after_response->SetHelp(
	        "Close the TCP connection after each command response (off by default).");

	auto* macro_interkey_frames = section->AddInt("macro_interkey_frames",
	                                             only_at_start,
	                                             1);
	macro_interkey_frames->SetMinMax(0, 60);
	macro_interkey_frames->SetHelp(
	        "Frames inserted between characters when expanding quoted TYPE strings (default 1).");

	auto* inter_token_frames = section->AddInt("inter_token_frame_delay",
	                                         only_at_start,
	                                         1);
	inter_token_frames->SetMinMax(0, 60);
	inter_token_frames->SetHelp(
	        "Frames to wait between TYPE tokens when processing queued actions (default 1).");

	auto* debug_segment = section->AddHex("debug_segment", only_at_start, 0);
	debug_segment->SetHelp(
	        "Real-mode segment used as the base for DEBUG responses (default 0).");

	auto* debug_offset = section->AddHex("debug_offset", only_at_start, 0);
	debug_offset->SetHelp(
	        "Offset added to the segment base for DEBUG responses (default 0).");

	auto* debug_length = section->AddInt("debug_length", only_at_start, 0);
	debug_length->SetMinMax(0, 4096);
	debug_length->SetHelp(
	        "Number of bytes returned by DEBUG (default 0 disables the region).");

	auto* auth_token = section->AddString("auth_token", only_at_start, "");
	auth_token->SetHelp(
	        "Shared secret required by AUTH. Supports ${ENV} expansion. Leave empty to disable.");

}

namespace textmode {

void Configure(const ServiceConfig& config)
{
	g_active_config = config;
	g_close_after_response = config.close_after_response;
	EnsureKeyboard();
	auto keyboard_handler = [](const std::string& command) -> CommandResponse {
		if (!g_keyboard_processor) {
			return {false, "ERR keyboard unavailable\n"};
		}
		return g_keyboard_processor->HandleCommand(command);
	};

	auto exit_handler = [] { shutdown_requested = true; };
	auto keys_down_provider = [] {
		if (!g_keyboard_processor) {
			return std::vector<std::string>{};
		}
		return g_keyboard_processor->ActiveKeys();
	};
	const auto debug_address = CombineSegmentOffset(config.debug_segment,
	                                               config.debug_offset);
	auto memory_reader = [](uint32_t offset, uint32_t length) {
		return textmode::PeekMemoryRegion(offset, length);
	};
	auto memory_writer = [](uint32_t offset, const std::vector<uint8_t>& data) {
		return textmode::PokeMemoryRegion(offset, data);
	};
	g_processor.emplace(&ProvideFrame,
	                   keyboard_handler,
	                   exit_handler,
	                   keys_down_provider,
	                   memory_reader,
	                   memory_writer);
	if (g_processor) {
		g_processor->SetMacroInterkeyFrames(config.macro_interkey_frames);
		g_processor->SetDebugRegion(debug_address, config.debug_length);
	}

	EnsureServer();
	if (g_server) {
		g_server->SetAuthToken(config.auth_token);
		g_server->SetCloseAfterResponse(g_close_after_response);
	}

	if (!g_queued_sink) {
		g_queued_sink = std::make_shared<textmode::QueuedTypeActionSink>(
		        [](textmode::ClientHandle client, const std::string& payload) {
			if (!g_server) {
				return false;
			}
			return g_server->Send(client, payload);
		},
		        [](textmode::ClientHandle client) {
			if (g_server) {
				g_server->Close(client);
			}
			return;
		});
	}

	if (g_queued_sink) {
		g_queued_sink->SetCloseAfterResponse(g_close_after_response);
		g_queued_sink->SetInterTokenFrameDelay(config.inter_token_frame_delay);
	}

	if (g_processor) {
		g_processor->SetTypeActionSink(g_queued_sink);
		g_processor->SetTypeSinkRequiresClient(true);
		g_processor->SetQueueNonFrameCommands(true);
		g_processor->SetAllowDeferredFrames(true);
	}

	if (!g_server) {
		return;
	}

	if (config.enable) {
		if (!g_server->IsRunning() || g_server->Port() != config.port) {
			if (!g_server->Start(config.port, *g_processor)) {
				LOG_WARNING("TEXTMODE: Unable to start listener on port %u",
				            static_cast<unsigned>(config.port));
			}
		}
	} else if (g_server->IsRunning()) {
		g_server->Stop();
	}

}

CommandResponse HandleCommand(const std::string& command)
{
	if (!g_processor) {
		return {false, "ERR service unavailable\n"};
	}

	return g_processor->HandleCommand(command);
}

void Poll()
{
	if (g_server) {
		g_server->Poll();
	}
	if (g_queued_sink) {
		g_queued_sink->Poll();
	}
}

void Shutdown()
{
	if (g_server) {
		g_server->Stop();
	}
	if (g_keyboard_processor) {
		g_keyboard_processor->Reset();
	}
	g_server.reset();
	g_processor.reset();
	g_keyboard_processor.reset();
	g_active_config.reset();
	g_queued_sink.reset();
}

} // namespace textmode

std::string TEXTMODESERVER_HandleCommand(const std::string& command)
{
	const auto response = textmode::HandleCommand(command);
	return response.payload;
}
