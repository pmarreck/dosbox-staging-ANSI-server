// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/memory_access.h"

#include "dosbox_test_fixture.h"
#include "hardware/memory.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

class TextModeMemoryAccessTest : public DOSBoxTestFixture {};

TEST_F(TextModeMemoryAccessTest, PeekMemoryRegionReturnsData)
{
	const uint32_t offset = 0x5000;
	const std::vector<uint8_t> expected{0x11, 0x22, 0x33, 0x44};

	MEM_BlockWrite(offset, expected.data(), expected.size());

	const auto result = textmode::PeekMemoryRegion(offset,
	                                               static_cast<uint32_t>(expected.size()));

	ASSERT_TRUE(result.success);
	EXPECT_EQ(result.bytes, expected);
}

TEST_F(TextModeMemoryAccessTest, PokeMemoryRegionWritesBytes)
{
	const uint32_t offset = 0x6400;
	const std::vector<uint8_t> data{0xDE, 0xAD, 0xC0, 0xDE};

	const auto write_result = textmode::PokeMemoryRegion(offset, data);

	ASSERT_TRUE(write_result.success);
	EXPECT_EQ(write_result.bytes_written, data.size());

	std::vector<uint8_t> observed(data.size());
	for (size_t i = 0; i < data.size(); ++i) {
		observed[i] = mem_readb(offset + static_cast<uint32_t>(i));
	}
	EXPECT_EQ(observed, data);
}

TEST_F(TextModeMemoryAccessTest, RejectsOutOfRangePeek)
{
	constexpr uint32_t huge_offset = 0xFFFFFF00u;
	const auto result = textmode::PeekMemoryRegion(huge_offset, 0x100);

	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.empty());
}

TEST_F(TextModeMemoryAccessTest, RejectsOutOfRangePoke)
{
	constexpr uint32_t huge_offset = 0xFFFFFF00u;
	const std::vector<uint8_t> data{0xAA, 0xBB};

	const auto result = textmode::PokeMemoryRegion(huge_offset, data);

	EXPECT_FALSE(result.success);
	EXPECT_FALSE(result.error.empty());
}

} // namespace
