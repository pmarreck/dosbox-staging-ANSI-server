// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/snapshot.h"

#include <algorithm>

namespace textmode {

namespace {

constexpr bool is_power_of_two(uint32_t value)
{
	return value && ((value & (value - 1)) == 0);
}

uint32_t determine_memory_size(const VgaType& state)
{
	if (state.vmemwrap) {
		return state.vmemwrap;
	}
	if (state.draw.linear_mask) {
		return state.draw.linear_mask + 1;
	}
	return 0;
}

uint32_t wrap_address(uint32_t address, uint32_t memory_size)
{
	if (memory_size == 0) {
		return address;
	}
	if (is_power_of_two(memory_size)) {
		return address & (memory_size - 1);
	}
	return address % memory_size;
}

uint32_t subtract_mod(uint32_t minuend, uint32_t subtrahend, uint32_t modulus)
{
	if (modulus == 0) {
		return minuend >= subtrahend ? minuend - subtrahend : 0;
	}
	const auto diff = (minuend + modulus) - (subtrahend % modulus);
	return diff % modulus;
}

const uint8_t* resolve_text_memory(const VgaType& state)
{
	if (state.tandy.draw_base) {
		return state.tandy.draw_base;
	}
	return state.mem.linear;
}

} // namespace

std::optional<Snapshot> CaptureSnapshot(const VgaType& state)
{
	if (state.mode != M_TEXT) {
		return std::nullopt;
	}

	const auto* text_mem = resolve_text_memory(state);
	if (!text_mem) {
		return std::nullopt;
	}

	const uint16_t columns = static_cast<uint16_t>(state.draw.blocks);
	if (columns == 0) {
		return std::nullopt;
	}

	const uint32_t char_height = state.draw.address_line_total ? state.draw.address_line_total : 16;
	const uint32_t total_lines = state.draw.lines_total;
	const uint16_t rows = (char_height > 0 && total_lines >= char_height)
	                             ? static_cast<uint16_t>(total_lines / char_height)
	                             : 25;
	if (rows == 0) {
		return std::nullopt;
	}

	Snapshot snapshot{};
	snapshot.columns = columns;
	snapshot.rows    = rows;
	snapshot.cells.resize(static_cast<size_t>(columns) * rows);

	const uint32_t memory_size = determine_memory_size(state);

	const uint32_t byte_panning_shift = state.draw.byte_panning_shift ? state.draw.byte_panning_shift : 2;
	const uint32_t start_word         = state.config.real_start;
	uint32_t start_byte               = start_word * byte_panning_shift;
	start_byte                        = wrap_address(start_byte, memory_size);

	const uint32_t row_stride = state.draw.address_add ? state.draw.address_add : static_cast<uint32_t>(columns) * 2;

	for (uint16_t row = 0; row < rows; ++row) {
		const uint32_t row_base = wrap_address(start_byte + row * row_stride, memory_size);
		for (uint16_t col = 0; col < columns; ++col) {
			const uint32_t char_addr = wrap_address(row_base + col * 2, memory_size);
			const uint32_t attr_addr = wrap_address(char_addr + 1, memory_size);
			const size_t cell_index  = static_cast<size_t>(row) * columns + col;
			snapshot.cells[cell_index].character = text_mem[char_addr];
			snapshot.cells[cell_index].attribute = text_mem[attr_addr];
		}
	}

	CursorState cursor{};
	cursor.enabled = state.draw.cursor.enabled;

	if (cursor.enabled) {
		const uint32_t cursor_addr_bytes = wrap_address(state.draw.cursor.address, memory_size);
		const uint32_t start_mod          = wrap_address(start_byte, memory_size);
		const uint32_t difference         = subtract_mod(cursor_addr_bytes, start_mod, memory_size);
		const uint32_t char_offset        = difference / 2;

		if (char_offset < snapshot.cells.size()) {
			cursor.row    = static_cast<uint16_t>(char_offset / columns);
			cursor.column = static_cast<uint16_t>(char_offset % columns);
			cursor.visible = !state.draw.blinking || state.draw.blink;
		} else {
			cursor.visible = false;
		}
	}

	snapshot.cursor = cursor;
	return snapshot;
}

} // namespace textmode
