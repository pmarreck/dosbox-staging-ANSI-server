// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/snapshot.h"

#include "hardware/video/vga.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <vector>

namespace {

using textmode::CaptureSnapshot;
using textmode::Snapshot;

class TextModeSnapshotTest : public ::testing::Test {};

TEST_F(TextModeSnapshotTest, CapturesBasicGrid)
{
	constexpr uint16_t columns = 4;
	constexpr uint16_t rows    = 3;
	constexpr uint32_t char_height = 16;
	constexpr uint32_t bytes_per_row = columns * 2;

	std::vector<uint8_t> vram(4096);
	for (uint16_t row = 0; row < rows; ++row) {
		for (uint16_t col = 0; col < columns; ++col) {
			const uint32_t cell_index = row * columns + col;
			const uint32_t byte_index = cell_index * 2;
			vram[byte_index]     = static_cast<uint8_t>('A' + cell_index);
			vram[byte_index + 1] = static_cast<uint8_t>(0x10 + cell_index);
		}
	}

VgaType state{};
state.mode                   = M_TEXT;
state.mem.linear             = vram.data();
state.tandy.draw_base        = vram.data();
state.vmemwrap               = static_cast<uint32_t>(vram.size());
state.draw.linear_mask       = state.vmemwrap - 1;
state.draw.blocks            = columns;
state.draw.address_line_total = char_height;
state.draw.lines_total       = rows * char_height;
state.draw.address_add       = bytes_per_row;
state.draw.byte_panning_shift = 2;
state.draw.bytes_skip        = 0;
state.draw.panning           = 0;
state.config.display_start   = 0;
state.config.real_start      = 0;

state.draw.cursor.enabled = true;
state.draw.cursor.address = 1 * bytes_per_row + 2 * 2; // row 1, column 2
state.draw.blinking       = 1;
state.draw.blink          = true;

const auto snapshot = CaptureSnapshot(state);
	ASSERT_TRUE(snapshot.has_value());

	EXPECT_EQ(snapshot->columns, columns);
	EXPECT_EQ(snapshot->rows, rows);
	ASSERT_EQ(snapshot->cells.size(), columns * rows);

	for (uint16_t row = 0; row < rows; ++row) {
		for (uint16_t col = 0; col < columns; ++col) {
			const uint32_t cell_index = row * columns + col;
			EXPECT_EQ(snapshot->cells[cell_index].character,
			          static_cast<uint8_t>('A' + cell_index));
			EXPECT_EQ(snapshot->cells[cell_index].attribute,
			          static_cast<uint8_t>(0x10 + cell_index));
		}
	}

	EXPECT_TRUE(snapshot->cursor.enabled);
	EXPECT_TRUE(snapshot->cursor.visible);
	EXPECT_EQ(snapshot->cursor.row, 1);
	EXPECT_EQ(snapshot->cursor.column, 2);
}

TEST_F(TextModeSnapshotTest, HandlesWrapAround)
{
	constexpr uint16_t columns           = 2;
	constexpr uint16_t rows              = 1;
	constexpr uint32_t char_height       = 16;
	constexpr uint32_t bytes_per_row     = columns * 2;
	constexpr uint32_t buffer_size_bytes = 32;

	std::vector<uint8_t> vram(buffer_size_bytes);
	// Start near the end so the row wraps across the buffer boundary
	const uint32_t start_byte = buffer_size_bytes - 2;
	vram[start_byte & (buffer_size_bytes - 1)] = 'X';
	vram[(start_byte + 1) & (buffer_size_bytes - 1)] = 0xAA;
	vram[(start_byte + 2) & (buffer_size_bytes - 1)] = 'Y';
	vram[(start_byte + 3) & (buffer_size_bytes - 1)] = 0xBB;

VgaType state{};
state.mode                    = M_TEXT;
state.mem.linear              = vram.data();
state.tandy.draw_base         = vram.data();
state.vmemwrap                = buffer_size_bytes;
state.draw.linear_mask        = buffer_size_bytes - 1;
state.draw.blocks             = columns;
state.draw.address_line_total = char_height;
state.draw.lines_total        = rows * char_height;
state.draw.address_add        = bytes_per_row;
state.draw.byte_panning_shift = 2;
state.draw.bytes_skip         = 0;
state.draw.panning            = 0;
state.config.display_start    = start_byte / 2;
state.config.real_start       = state.config.display_start & (state.vmemwrap - 1);

state.draw.cursor.enabled = false;

const auto snapshot = CaptureSnapshot(state);
	ASSERT_TRUE(snapshot.has_value());
	ASSERT_EQ(snapshot->cells.size(), columns * rows);
	EXPECT_EQ(snapshot->cells[0].character, 'X');
	EXPECT_EQ(snapshot->cells[0].attribute, 0xAA);
	EXPECT_EQ(snapshot->cells[1].character, 'Y');
	EXPECT_EQ(snapshot->cells[1].attribute, 0xBB);
}

} // namespace
