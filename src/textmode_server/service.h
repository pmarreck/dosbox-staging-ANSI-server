// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_SERVICE_H
#define DOSBOX_TEXTMODE_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "textmode_server/snapshot.h"

namespace textmode {

struct ServiceConfig {
	bool enable          = false;
	uint16_t port        = 6000;
	bool show_attributes = true;
	std::string sentinel = {};
	bool close_after_response = false;
	uint32_t macro_interkey_frames = 1;
	uint32_t inter_token_frame_delay = 1;
};

struct ServiceResult {
	bool success      = false;
	std::string frame = {};
	std::string error = {};
};

class TextModeService {
public:
	TextModeService(ServiceConfig config, std::vector<std::string> keys_down = {})
	        : m_config(std::move(config)), m_keys_down(std::move(keys_down)) {}

	ServiceResult GetFrame() const;

private:
	ServiceConfig m_config;
	std::vector<std::string> m_keys_down;
};

} // namespace textmode

#endif // DOSBOX_TEXTMODE_SERVICE_H
