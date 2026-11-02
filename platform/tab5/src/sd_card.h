// sd_card.h - mounts the Tab5's microSD card at /sdcard (FAT, read/write).
#pragma once
#include <stdint.h>

namespace tab5 {

constexpr const char* kSdRoot = "/sdcard";

// Mounts the card (4-bit, falling back to 1-bit). Never formats it. True when mounted.
bool mountSd();
// Card size and free space in bytes (0 when not mounted).
uint64_t sdTotalBytes();
uint64_t sdFreeBytes();

} // namespace tab5
