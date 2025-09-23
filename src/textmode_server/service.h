// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_SERVICE_H
#define DOSBOX_TEXTMODE_SERVICE_H

#include <cstdint>
#include <string>
#include <utility>

#include "textmode_server/snapshot.h"

namespace textmode {

struct ServiceConfig {
	bool enable          = false;
	uint16_t port        = 6000;
	bool show_attributes = true;
	std::string sentinel = {};
	bool keyboard_enable = false;
	uint16_t keyboard_port = 6001;
};

struct ServiceResult {
	bool success      = false;
	std::string frame = {};
	std::string error = {};
};

class TextModeService {
public:
	explicit TextModeService(ServiceConfig config) : m_config(std::move(config)) {}

	ServiceResult GetFrame() const;

private:
	ServiceConfig m_config;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_SERVICE_H
