#pragma once

// Port for read-only memory, VRAM budget, disk read counters, and process image queries (phase 4).

#include <cstdint>
#include <string>

namespace vegaflow {

struct VideoMemoryTelemetry {
    uint64_t local_usage_bytes = 0;
    uint64_t local_budget_bytes = 0;
    uint64_t non_local_usage_bytes = 0;
    uint64_t non_local_budget_bytes = 0;
    bool valid = false;
};

struct SystemMemoryTelemetry {
    uint64_t total_physical_bytes = 0;
    uint64_t free_and_zero_bytes = 0;  // (ZeroPageCount + FreePageCount) * 4096 when memory_lists_valid
    uint64_t standby_bytes = 0;        // Sum of PageCountByPriority[0..7] * 4096 when memory_lists_valid
    uint64_t modified_bytes = 0;       // ModifiedPageCount * 4096 when memory_lists_valid
    bool memory_lists_valid = false;   // False if NtQuerySystemInformation(80) failed (never fall back to ullAvailPhys)
    VideoMemoryTelemetry vram;
};

class MemoryProbe {
public:
    virtual ~MemoryProbe() = default;

    virtual SystemMemoryTelemetry SampleSystemMemory() const = 0;
    // Cumulative bytes read from disk by `pid` (IO_COUNTERS::ReadTransferCount).
    virtual bool SampleProcessDiskReadBytes(uint32_t pid, uint64_t& out_read_bytes) const = 0;
    // UTF-8 executable path or basename for `pid` so factory Do-Not-Trim exclusions can be checked.
    virtual std::string QueryProcessImageName(uint32_t pid) const = 0;
    virtual uint32_t CurrentProcessId() const = 0;
};

} // namespace vegaflow
