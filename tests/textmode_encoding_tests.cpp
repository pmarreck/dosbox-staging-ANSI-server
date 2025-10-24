// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/encoder.h"
#include "misc/unicode.h"

#include <gtest/gtest.h>

#include <array>
#include <iomanip>
#include <string>

namespace {

using textmode::EncodingOptions;
using textmode::Snapshot;
using textmode::TextCell;
using textmode::TextCell;

Snapshot make_snapshot(uint16_t cols, uint16_t rows)
{
	Snapshot snapshot{};
	snapshot.columns = cols;
	snapshot.rows    = rows;
	snapshot.cells.resize(static_cast<size_t>(cols) * rows);
	return snapshot;
}

TEST(TextModeEncodingTest, EncodesWithAnsiAttributes)
{
	auto snapshot = make_snapshot(2, 1);
	snapshot.cells[0] = TextCell{static_cast<uint8_t>('A'), 0x1E};
	snapshot.cells[1] = TextCell{static_cast<uint8_t>('B'), 0x07};
	snapshot.cursor.enabled = false;

	EncodingOptions options{};
	options.show_attributes = true;
	options.sentinel        = "\xF0\x9F\x96\xB5"; // 🖵

	const auto frame = textmode::BuildAnsiFrame(snapshot, options);

	const std::string expected =
	        "\xF0\x9F\x96\xB5META cols=2\n"
	        "\xF0\x9F\x96\xB5META rows=1\n"
	        "\xF0\x9F\x96\xB5META cursor=disabled\n"
	        "\xF0\x9F\x96\xB5META attributes=show\n"
	        "\xF0\x9F\x96\xB5META keys_down=\n"
	        "\xF0\x9F\x96\xB5PAYLOAD\n"
	        "\x1b[0m\x1b[0;38;2;255;255;85;48;2;0;0;170mA"
	        "\x1b[0;38;2;170;170;170;48;2;0;0;0mB\x1b[0m\n";

	EXPECT_EQ(frame, expected);
}

TEST(TextModeEncodingTest, EncodesWithoutAttributes)
{
	auto snapshot = make_snapshot(2, 1);
	snapshot.cells[0] = TextCell{static_cast<uint8_t>('C'), 0x4F};
	snapshot.cells[1] = TextCell{static_cast<uint8_t>('D'), 0x70};
	snapshot.cursor.enabled = true;
	snapshot.cursor.visible = true;
	snapshot.cursor.row     = 0;
	snapshot.cursor.column  = 1;

	EncodingOptions options{};
	options.show_attributes = false;
	options.sentinel        = "s";

	const auto frame = textmode::BuildAnsiFrame(snapshot, options);

	const std::string expected =
	        "sMETA cols=2\n"
	        "sMETA rows=1\n"
	        "sMETA cursor=0,1 visible=1\n"
	        "sMETA attributes=hide\n"
	        "sMETA keys_down=\n"
	        "sPAYLOAD\nCD\n";

	EXPECT_EQ(frame, expected);
}

TEST(TextModeEncodingTest, ConvertsControlGlyphsToUnicode)
{
	auto snapshot = make_snapshot(2, 1);
	snapshot.cells[0] = TextCell{0x12, 0x40}; // ↕
	snapshot.cells[1] = TextCell{0x17, 0x40}; // ↨
	snapshot.cursor.enabled = false;

	EncodingOptions options{};
	options.show_attributes = false;
	options.sentinel        = "*";

	const auto frame = textmode::BuildAnsiFrame(snapshot, options);

	EXPECT_NE(frame.find("↕↨"), std::string::npos)
	        << "Frame payload should contain the up/down glyphs\n"
	        << frame;
}

std::string EncodeCodePoint(uint32_t code_point)
{
	std::string output;
	if (code_point <= 0x7f) {
		output.push_back(static_cast<char>(code_point));
	} else if (code_point <= 0x7ff) {
		output.push_back(static_cast<char>(0xc0 | ((code_point >> 6) & 0x1f)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
	} else if (code_point <= 0xffff) {
		output.push_back(static_cast<char>(0xe0 | ((code_point >> 12) & 0x0f)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
	} else {
		output.push_back(static_cast<char>(0xf0 | ((code_point >> 18) & 0x07)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
		output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
	}
	return output;
}

TEST(TextModeEncodingTest, Cp437ControlGlyphsMatchMapping)
{
	constexpr std::array<uint16_t, 32> expected = {
	        0x0020, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
	        0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
	        0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8,
	        0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc};
	constexpr uint16_t expected_delete = 0x2302;

	for (uint16_t code = 0; code < expected.size(); ++code) {
		const std::string dos_str(1, static_cast<char>(code));
		const auto utf8 = dos_to_utf8(dos_str,
		                              DosStringConvertMode::ScreenCodesOnly,
		                              437);
		EXPECT_EQ(utf8, EncodeCodePoint(expected[code]))
		        << "Mismatch at code page 437 byte 0x" << std::hex << code;
	}

	const std::string delete_str(1, static_cast<char>(0x7f));
	const auto delete_utf8 = dos_to_utf8(delete_str,
	                                     DosStringConvertMode::ScreenCodesOnly,
	                                     437);
	EXPECT_EQ(delete_utf8, EncodeCodePoint(expected_delete))
	        << "Mismatch at code page 437 byte 0x7f";
}

} // namespace
