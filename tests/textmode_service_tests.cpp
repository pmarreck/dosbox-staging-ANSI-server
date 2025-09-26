// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/service.h"

#include "hardware/video/vga.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

class TextModeServiceTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		saved.mode                = vga.mode;
		saved.mem_linear          = vga.mem.linear;
		saved.draw_base           = vga.tandy.draw_base;
		saved.vmemwrap            = vga.vmemwrap;
		saved.linear_mask         = vga.draw.linear_mask;
		saved.blocks              = vga.draw.blocks;
		saved.address_line_total  = vga.draw.address_line_total;
		saved.lines_total         = vga.draw.lines_total;
		saved.address_add         = vga.draw.address_add;
		saved.byte_panning_shift  = vga.draw.byte_panning_shift;
		saved.cursor_enabled      = vga.draw.cursor.enabled;
		saved.cursor_address      = vga.draw.cursor.address;
		saved.blinking            = vga.draw.blinking;
		saved.blink               = vga.draw.blink;
	}

	void TearDown() override
	{
		vga.mode                    = saved.mode;
		vga.mem.linear              = saved.mem_linear;
		vga.tandy.draw_base         = saved.draw_base;
		vga.vmemwrap                = saved.vmemwrap;
		vga.draw.linear_mask        = saved.linear_mask;
		vga.draw.blocks             = saved.blocks;
		vga.draw.address_line_total = saved.address_line_total;
		vga.draw.lines_total        = saved.lines_total;
		vga.draw.address_add        = saved.address_add;
		vga.draw.byte_panning_shift = saved.byte_panning_shift;
		vga.draw.cursor.enabled     = saved.cursor_enabled;
		vga.draw.cursor.address     = saved.cursor_address;
		vga.draw.blinking           = saved.blinking;
		vga.draw.blink              = saved.blink;
	}

	struct {
		VGAModes mode;
		uint8_t* mem_linear = nullptr;
		uint8_t* draw_base  = nullptr;
		uint32_t vmemwrap   = 0;
		uint32_t linear_mask = 0;
		uint32_t blocks      = 0;
		uint32_t address_line_total = 0;
		uint32_t lines_total        = 0;
		uint32_t address_add        = 0;
		uint32_t byte_panning_shift = 0;
		bool cursor_enabled         = false;
		uint32_t cursor_address     = 0;
		uint8_t blinking            = 0;
		bool blink                  = false;
	} saved{};
};

TEST_F(TextModeServiceTest, ReturnsErrorWhenDisabled)
{
	const textmode::ServiceConfig config{
	        .enable          = false,
	        .port            = 6000,
	        .show_attributes = true,
	        .sentinel        = "#",
	};

	textmode::TextModeService service(config);
	const auto result = service.GetFrame();

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.error, "text-mode server disabled");
}

TEST_F(TextModeServiceTest, ReturnsErrorWhenNotInTextMode)
{
	vga.mode = M_VGA;

	const textmode::ServiceConfig config{
	        .enable          = true,
	        .port            = 6000,
	        .show_attributes = true,
	        .sentinel        = "#",
	};

	textmode::TextModeService service(config);
	const auto result = service.GetFrame();

	EXPECT_FALSE(result.success);
	EXPECT_EQ(result.error, "video adapter not in text mode");
}

TEST_F(TextModeServiceTest, BuildsFrameWhenEnabled)
{
	constexpr uint16_t columns     = 2;
	constexpr uint16_t rows        = 1;
	constexpr uint32_t char_height = 16;
	constexpr uint32_t bytes_per_row = columns * 2;

	static uint8_t vram[64] = {};
	vram[0] = 'E';
	vram[1] = 0x1E;
	vram[2] = 'F';
	vram[3] = 0x07;

	vga.mode                    = M_TEXT;
	vga.mem.linear              = vram;
	vga.tandy.draw_base         = vram;
	vga.vmemwrap                = sizeof(vram);
	vga.draw.linear_mask        = vga.vmemwrap - 1;
	vga.draw.blocks             = columns;
	vga.draw.address_line_total = char_height;
	vga.draw.lines_total        = rows * char_height;
	vga.draw.address_add        = bytes_per_row;
	vga.draw.byte_panning_shift = 2;
	vga.draw.cursor.enabled     = false;
	vga.draw.blinking           = 0;
	vga.draw.blink              = false;

	const textmode::ServiceConfig config{
	        .enable          = true,
	        .port            = 6000,
	        .show_attributes = false,
	        .sentinel        = "*",
	};

	textmode::TextModeService service(config);
	const auto result = service.GetFrame();

	ASSERT_TRUE(result.success) << result.error;
	EXPECT_EQ(result.frame,
	          "*META cols=2\n"
	          "*META rows=1\n"
	          "*META cursor=disabled\n"
	          "*META attributes=hide\n"
	          "*META keys_down=\n"
	          "*PAYLOAD\nEF\n");
}

} // namespace
