// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "textmode_server/memory_access.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "cpu/paging.h"
#include "hardware/memory.h"

namespace textmode {

namespace {

constexpr uint32_t PageSize = 4096u;

uint64_t MaximumAddressableBytes()
{
	return static_cast<uint64_t>(MEM_TotalPages()) * PageSize;
}

bool IsRangeInBounds(uint32_t offset, uint32_t length)
{
	if (length == 0) {
		return false;
	}

	const uint64_t max_bytes = MaximumAddressableBytes();
	if (offset >= max_bytes) {
		return false;
	}

	const uint64_t end = static_cast<uint64_t>(offset) + length;
	return end <= max_bytes;
}

} // namespace

MemoryAccessResult PeekMemoryRegion(const uint32_t offset, const uint32_t length)
{
	MemoryAccessResult result{};

	if (!IsRangeInBounds(offset, length)) {
		result.error = "memory range out of bounds";
		return result;
	}

	result.bytes.resize(length);
	for (uint32_t i = 0; i < length; ++i) {
		uint8_t value = 0;
		if (mem_readb_checked(static_cast<PhysPt>(offset + i), &value)) {
			result.error.clear();
			result.error = "memory read failed";
			result.bytes.clear();
			return result;
		}
		result.bytes[i] = value;
	}

	result.success = true;
	return result;
}

MemoryWriteResult PokeMemoryRegion(const uint32_t offset, const std::vector<uint8_t>& data)
{
	MemoryWriteResult result{};

	if (data.empty()) {
		result.error = "no data provided";
		return result;
	}

	if (!IsRangeInBounds(offset, static_cast<uint32_t>(data.size()))) {
		result.error = "memory range out of bounds";
		return result;
	}

	for (size_t i = 0; i < data.size(); ++i) {
		const auto address = static_cast<PhysPt>(offset + static_cast<uint32_t>(i));
		if (mem_writeb_checked(address, data[i])) {
			result.error = "memory write failed";
			result.bytes_written = i;
			return result;
		}
	}

	result.success       = true;
	result.bytes_written = data.size();
	return result;
}

} // namespace textmode
