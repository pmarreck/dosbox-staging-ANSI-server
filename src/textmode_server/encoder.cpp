// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/encoder.h"

#include <array>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "misc/unicode.h"

namespace textmode {

namespace {

constexpr uint16_t CodePage437 = 437;

constexpr std::array<int, 16> FgColorCodes = {
        30, // black
        34, // blue
        32, // green
        36, // cyan
        31, // red
        35, // magenta
        33, // brown/yellow
        37, // light grey
        90, // dark grey
        94, // light blue
        92, // light green
        96, // light cyan
        91, // light red
        95, // light magenta
        93, // yellow
        97, // white
};

constexpr std::array<bool, 16> FgIsBright = {
        false, false, false, false, false, false, false, false,
        true,  true,  true,  true,  true,  true,  true,  true,
};

constexpr std::array<int, 8> BgColorCodes = {
        40, // black
        44, // blue
        42, // green
        46, // cyan
        41, // red
        45, // magenta
        43, // brown/yellow
        47, // light grey
};

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
	std::vector<int> codes;
	codes.reserve(4);
	codes.push_back(0);

	const auto fg_index = attribute & 0x0f;
	if (FgIsBright[fg_index]) {
		codes.push_back(1);
	}
	codes.push_back(FgColorCodes[fg_index]);

	const auto bg_index = static_cast<size_t>((attribute >> 4) & 0x07);
	codes.push_back(BgColorCodes[bg_index]);

	if (attribute & 0x80) {
		codes.push_back(5);
	}

	std::ostringstream oss;
	oss << "\x1b[";
	for (size_t i = 0; i < codes.size(); ++i) {
		if (i > 0) {
			oss << ';';
		}
		oss << codes[i];
	}
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
