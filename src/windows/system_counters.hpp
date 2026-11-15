#pragma once

// Windows reads that the console report and the GUI need: how many processes are throttled, how many games
// have Direct Flip configured, the L3 cache size, DIMM slots and window ids. Use cases in session/ call these instead of Win32.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace vegaflow {

uint32_t CountEcoQoSProcesses();
uint32_t CountDirectFlipConfiguredGames();

// Populated DIMMs in an SMBIOS structure table (type 17 entries with a size). Stops at a truncated entry instead
// of reading past `len`. Pure: no Windows calls, so tests feed it synthetic tables.
uint32_t CountDimmsInSmbiosTable(const uint8_t* table, size_t len);

// Number of populated DIMM slots from the SMBIOS table (type 17 entries with a size). 0 if the table cannot be read.
uint32_t CountPopulatedDimmSlots();

bool IsWindowHandleValid(HWND hwnd);
uint32_t WindowThreadId(HWND hwnd);

// Total L3 bytes from the cache topology; falls back to CPUID leaf 0x80000006. 0 if neither works.
uint64_t ReadL3CacheBytes();

} // namespace vegaflow
