// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/encoder.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "misc/unicode.h"

namespace textmode {

namespace {

constexpr uint16_t CodePage437 = 437;

struct Rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

constexpr std::array<Rgb, 16> DosPalette = {{
		{0x00, 0x00, 0x00}, // black
		{0x00, 0x00, 0xAA}, // blue
		{0x00, 0xAA, 0x00}, // green
		{0x00, 0xAA, 0xAA}, // cyan
		{0xAA, 0x00, 0x00}, // red
		{0xAA, 0x00, 0xAA}, // magenta
		{0xAA, 0x55, 0x00}, // brown/yellow
		{0xAA, 0xAA, 0xAA}, // light grey
		{0x55, 0x55, 0x55}, // dark grey
		{0x55, 0x55, 0xFF}, // light blue
		{0x55, 0xFF, 0x55}, // light green
		{0x55, 0xFF, 0xFF}, // light cyan
		{0xFF, 0x55, 0x55}, // light red
		{0xFF, 0x55, 0xFF}, // light magenta
		{0xFF, 0xFF, 0x55}, // yellow
		{0xFF, 0xFF, 0xFF}, // white
}};

std::string to_utf8_char(const uint8_t dos_char)
{
	const std::string dos_str(1, static_cast<char>(dos_char));
	const auto utf8 = dos_to_utf8(dos_str,
	                              DosStringConvertMode::WithControlCodes,
	                              CodePage437);
	return utf8.empty() ? std::string("?") : utf8;
}

std::string build_sgr(uint8_t attribute)
{
	const auto& fg = DosPalette[attribute & 0x0f];
	const auto& bg = DosPalette[(attribute >> 4) & 0x07];

	std::ostringstream oss;
	oss << "\x1b[0";
	if (attribute & 0x80) {
		oss << ";5";
	}
	oss << ";38;2;" << static_cast<int>(fg.r) << ';'
	    << static_cast<int>(fg.g) << ';' << static_cast<int>(fg.b);
	oss << ";48;2;" << static_cast<int>(bg.r) << ';'
	    << static_cast<int>(bg.g) << ';' << static_cast<int>(bg.b);
	oss << 'm';
	return oss.str();
}

const std::string& ensure_sentinel(const EncodingOptions& options)
{
	static const std::string default_sentinel{
	        "\xF0\x9F\x96\xB5"}; // 🖵
	return options.sentinel.empty() ? default_sentinel : options.sentinel;
}

} // namespace

std::string BuildAnsiFrame(const Snapshot& snapshot, const EncodingOptions& options)
{
	std::ostringstream oss;
	const auto& sentinel = ensure_sentinel(options);

	oss << sentinel << "META cols=" << snapshot.columns << '\n';
	oss << sentinel << "META rows=" << snapshot.rows << '\n';
	if (snapshot.cursor.enabled) {
		oss << sentinel << "META cursor=" << snapshot.cursor.row << ','
		    << snapshot.cursor.column << " visible="
		    << (snapshot.cursor.visible ? 1 : 0) << '\n';
	} else {
	oss << sentinel << "META cursor=disabled\n";
	}
	oss << sentinel << "META attributes="
	    << (options.show_attributes ? "show" : "hide") << '\n';
	oss << sentinel << "META keys_down=";
	for (size_t i = 0; i < options.keys_down.size(); ++i) {
		if (i > 0) {
			oss << ',';
		}
		oss << options.keys_down[i];
	}
	oss << '\n';
	oss << sentinel << "PAYLOAD\n";

	const auto cols = snapshot.columns;
	const auto rows = snapshot.rows;

	if (options.show_attributes) {
		oss << "\x1b[0m";
	}

	uint8_t previous_attribute = 0;
	bool has_previous_attr     = false;

	for (uint16_t row = 0; row < rows; ++row) {
		for (uint16_t col = 0; col < cols; ++col) {
			const auto& cell = snapshot.cells[row * cols + col];
			if (options.show_attributes) {
				if (!has_previous_attr || cell.attribute != previous_attribute) {
					oss << build_sgr(cell.attribute);
					previous_attribute = cell.attribute;
					has_previous_attr  = true;
				}
			}
			oss << to_utf8_char(cell.character);
		}
		if (options.show_attributes) {
			oss << "\x1b[0m";
		}
		oss << '\n';
		if (options.show_attributes && row + 1 < rows) {
			// When attributes are enabled, start the next line from a clean slate
			has_previous_attr = false;
			oss << "\x1b[0m";
		}
	}

	return oss.str();
}

} // namespace textmode
