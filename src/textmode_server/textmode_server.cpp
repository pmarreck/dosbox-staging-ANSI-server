// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/textmode_server.h"

#include <cassert>
#include <memory>
#include <optional>

#include "dosbox.h"
#include "misc/logging.h"
#include "textmode_server/keyboard_processor.h"
#include "textmode_server/server.h"
#include "hardware/input/keyboard.h"

namespace {

std::optional<textmode::ServiceConfig> g_active_config = std::nullopt;
std::optional<textmode::CommandProcessor> g_processor  = std::nullopt;
std::unique_ptr<textmode::TextModeServer> g_server = nullptr;
std::unique_ptr<textmode::KeyboardCommandProcessor> g_keyboard_processor = nullptr;
std::unique_ptr<textmode::TextModeServer> g_keyboard_server              = nullptr;

void EnsureServer()
{
	if (!g_server) {
		g_server = std::make_unique<textmode::TextModeServer>(textmode::MakeSdlNetBackend());
	}
}

void EnsureKeyboard()
{
	if (!g_keyboard_processor) {
		g_keyboard_processor = std::make_unique<textmode::KeyboardCommandProcessor>(
	        [](KBD_KEYS key, bool pressed) { KEYBOARD_AddKey(key, pressed); });
	}
	if (!g_keyboard_server) {
		g_keyboard_server = std::make_unique<textmode::TextModeServer>(textmode::MakeSdlNetBackend());
	}
}

textmode::ServiceResult ProvideFrame()
{
	const auto config = g_active_config.value_or(textmode::ServiceConfig{});
	textmode::TextModeService service(config);
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
	config.show_attributes = props->GetBool("show_attributes");
	config.sentinel        = props->GetString("sentinel");
	config.keyboard_enable = props->GetBool("keyboard_enable");
	config.keyboard_port   = static_cast<uint16_t>(props->GetInt("keyboard_port"));

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

	auto* keyboard_enable = section->AddBool("keyboard_enable", only_at_start, false);
	keyboard_enable->SetHelp(
	        "Enable the simulated keyboard TCP server (off by default). The server accepts\n"
	        "commands such as PRESS, DOWN, and UP to generate scancodes inside the guest.");

	auto* keyboard_port = section->AddInt("keyboard_port", only_at_start, 6001);
	keyboard_port->SetMinMax(1024, 65535);
	keyboard_port->SetHelp(
	        "TCP port used by the keyboard server (6001 by default). Valid range is 1024-65535.");
}

namespace textmode {

void Configure(const ServiceConfig& config)
{
	g_active_config = config;
	auto keyboard_handler = [](const std::string& command) -> CommandResponse {
		if (!g_keyboard_processor) {
			return {false, "ERR keyboard unavailable\n"};
		}
		return g_keyboard_processor->HandleCommand(command);
	};

	auto exit_handler = [] { shutdown_requested = true; };
	g_processor.emplace(&ProvideFrame, keyboard_handler, exit_handler);

	EnsureServer();

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

	EnsureKeyboard();

	if (!g_keyboard_server || !g_keyboard_processor) {
		return;
	}

	if (config.keyboard_enable) {
		if (!g_keyboard_server->IsRunning() ||
		    g_keyboard_server->Port() != config.keyboard_port) {
			g_keyboard_processor->Reset();
			if (!g_keyboard_server->Start(config.keyboard_port, *g_keyboard_processor)) {
				LOG_WARNING("TEXTMODE: Unable to start keyboard listener on port %u",
				            static_cast<unsigned>(config.keyboard_port));
			}
		}
	} else if (g_keyboard_server->IsRunning()) {
		g_keyboard_processor->Reset();
		g_keyboard_server->Stop();
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
	if (g_keyboard_server) {
		g_keyboard_server->Poll();
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
	if (g_keyboard_server) {
		g_keyboard_server->Stop();
	}
	g_server.reset();
	g_keyboard_server.reset();
	g_processor.reset();
	g_keyboard_processor.reset();
	g_active_config.reset();
}

} // namespace textmode

std::string TEXTMODESERVER_HandleCommand(const std::string& command)
{
	const auto response = textmode::HandleCommand(command);
	return response.payload;
}
