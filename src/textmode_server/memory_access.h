// SPDX-FileCopyrightText:  2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEXTMODE_MEMORY_ACCESS_H
#define DOSBOX_TEXTMODE_MEMORY_ACCESS_H

#include <cstdint>
#include <string>
#include <vector>

namespace textmode {

struct MemoryAccessResult {
	bool success = false;
	std::vector<uint8_t> bytes = {};
	std::string error = {};
};

struct MemoryWriteResult {
	bool success       = false;
	size_t bytes_written = 0;
	std::string error  = {};
};

MemoryAccessResult PeekMemoryRegion(uint32_t offset, uint32_t length);
MemoryWriteResult PokeMemoryRegion(uint32_t offset, const std::vector<uint8_t>& data);

} // namespace textmode

#endif // DOSBOX_TEXTMODE_MEMORY_ACCESS_H
