// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_SERVER_H
#define DOSBOX_TEXTMODE_SERVER_H

#include "config/config.h"

#include "textmode_server/command_processor.h"
#include "textmode_server/service.h"

void TEXTMODESERVER_AddConfigSection(const ConfigPtr& conf);

namespace textmode {

void Configure(const ServiceConfig& config);
CommandResponse HandleCommand(const std::string& command);
void Poll();
void Shutdown();

} // namespace textmode

std::string TEXTMODESERVER_HandleCommand(const std::string& command);

#endif // DOSBOX_TEXTMODE_SERVER_H
