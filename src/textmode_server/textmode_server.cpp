// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/textmode_server.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <optional>
#include <vector>

#include "dosbox.h"
#include "misc/logging.h"
#include "textmode_server/keyboard_processor.h"
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
	g_processor.emplace(&ProvideFrame, keyboard_handler, exit_handler, keys_down_provider);
	if (g_processor) {
		g_processor->SetMacroInterkeyFrames(config.macro_interkey_frames);
	}

	EnsureServer();
	if (g_server) {
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
