// Unit tests: the SMBIOS table walk behind the DIMM slot count, fed with synthetic tables (no firmware read).
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#include <cstdint>
#include <string>
#include <vector>

#include "windows/system_counters.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

// One type-17 (Memory Device) structure: 0x14 bytes formatted area, size field at 0x0C, then the empty string set.
std::vector<uint8_t> MemoryDevice(uint16_t size_field) {
    std::vector<uint8_t> e(0x14, 0);
    e[0] = 17;
    e[1] = 0x14;
    e[0x0C] = static_cast<uint8_t>(size_field & 0xFF);
    e[0x0D] = static_cast<uint8_t>(size_field >> 8);
    e.push_back(0);
    e.push_back(0);
    return e;
}

void Append(std::vector<uint8_t>& table, const std::vector<uint8_t>& entry) {
    table.insert(table.end(), entry.begin(), entry.end());
}

uint32_t Count(const std::vector<uint8_t>& t) { return vegaflow::CountDimmsInSmbiosTable(t.data(), t.size()); }

} // namespace

void Test60() {
    // -------------------------------------------------------------------------
    // Test 60: the SMBIOS walk counts populated DIMMs and never reads past a truncated table
    // -------------------------------------------------------------------------
    std::vector<uint8_t> two;
    Append(two, MemoryDevice(8192));   // 8 GB
    Append(two, MemoryDevice(0));      // empty slot
    Append(two, MemoryDevice(0xFFFF)); // size unknown
    Append(two, MemoryDevice(16384));  // 16 GB
    Append(two, {127, 4, 0, 0, 0, 0}); // end-of-table
    const bool counts_ok = Count(two) == 2;

    // Cut the last DIMM in the middle of its formatted area: it must stop, not read 0x0C/0x0D beyond the buffer.
    std::vector<uint8_t> truncated;
    Append(truncated, MemoryDevice(8192));
    std::vector<uint8_t> half = MemoryDevice(8192);
    half.resize(0x08);
    truncated.insert(truncated.end(), half.begin(), half.end());
    const bool truncated_ok = Count(truncated) == 1;

    const bool empty_ok = vegaflow::CountDimmsInSmbiosTable(nullptr, 0) == 0;

    // A structure that claims a length of 3 would loop forever or go backwards: it ends the walk.
    std::vector<uint8_t> bad;
    Append(bad, MemoryDevice(8192));
    Append(bad, {17, 3, 0, 0, 0, 0});
    Append(bad, MemoryDevice(8192));
    const bool bad_length_ok = Count(bad) == 1;

    auto pass = [](bool b) { return std::string(b ? "PASS" : "FAIL"); };
    RecordResult(counts_ok && truncated_ok && empty_ok && bad_length_ok,
                 "Test 60: the SMBIOS walk counts populated DIMMs and never reads past a truncated table",
                 "2 of 4 slots: " + pass(counts_ok) + ", truncated entry: " + pass(truncated_ok) +
                     ", empty table: " + pass(empty_ok) + ", length under 4: " + pass(bad_length_ok));
}

} // namespace vftest
