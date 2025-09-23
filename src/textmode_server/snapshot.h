// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_SNAPSHOT_H
#define DOSBOX_TEXTMODE_SNAPSHOT_H

#include "hardware/video/vga.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace textmode {

struct TextCell {
	uint8_t character = 0;
	uint8_t attribute = 0;
};

struct CursorState {
	bool enabled = false;
	bool visible = false;
	uint16_t row = 0;
	uint16_t column = 0;
};

struct Snapshot {
	uint16_t columns = 0;
	uint16_t rows    = 0;
	std::vector<TextCell> cells = {};
	CursorState cursor          = {};
};

std::optional<Snapshot> CaptureSnapshot(const VgaType& state);

} // namespace textmode

#endif // DOSBOX_TEXTMODE_SNAPSHOT_H
