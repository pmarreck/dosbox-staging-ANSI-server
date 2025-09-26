// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_ENCODER_H
#define DOSBOX_TEXTMODE_ENCODER_H

#include "textmode_server/snapshot.h"

#include <string>
#include <vector>

namespace textmode {

struct EncodingOptions {
	bool show_attributes = true;
	std::string sentinel = "\xF0\x9F\x96\xB5"; // 🖵
	std::vector<std::string> keys_down = {};
};

std::string BuildAnsiFrame(const Snapshot& snapshot, const EncodingOptions& options);

} // namespace textmode

#endif // DOSBOX_TEXTMODE_ENCODER_H
