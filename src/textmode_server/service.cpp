// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/service.h"

#include <algorithm>
#include <utility>

#include "hardware/video/vga.h"
#include "textmode_server/encoder.h"

namespace textmode {

namespace {

ServiceResult Failure(std::string message)
{
	ServiceResult result{};
	result.success = false;
	result.error   = std::move(message);
	return result;
}

ServiceResult Success(std::string frame)
{
	ServiceResult result{};
	result.success = true;
	result.frame   = std::move(frame);
	return result;
}

} // namespace

ServiceResult TextModeService::GetFrame() const
{
	if (!m_config.enable) {
		return Failure("text-mode server disabled");
	}

	if (vga.mode != M_TEXT) {
		return Failure("video adapter not in text mode");
	}

	const auto snapshot = CaptureSnapshot(vga);
	if (!snapshot.has_value()) {
		return Failure("unable to capture text snapshot");
	}

	EncodingOptions encoding{};
	encoding.show_attributes = m_config.show_attributes;
	encoding.sentinel        = m_config.sentinel;
	encoding.keys_down       = m_keys_down;
	std::sort(encoding.keys_down.begin(), encoding.keys_down.end());

	return Success(BuildAnsiFrame(*snapshot, encoding));
}

} // namespace textmode
