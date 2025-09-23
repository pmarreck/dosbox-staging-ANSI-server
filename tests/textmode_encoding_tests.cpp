// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/encoder.h"

#include <gtest/gtest.h>

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
	        "\xF0\x9F\x96\xB5PAYLOAD\n"
	        "\x1b[0m\x1b[0;1;93;44mA\x1b[0;37;40mB\x1b[0m\n";

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
	        "sPAYLOAD\nCD\n";

	EXPECT_EQ(frame, expected);
}

} // namespace
