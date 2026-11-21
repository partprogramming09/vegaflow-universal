#pragma once

// Port for memory control actions (background working set trims and standby list purge) (phase 4).

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

struct StandbyPurgeOutcome {
    bool succeeded = false;
    bool privilege_missing = false;
    int32_t nt_status = 0;
    std::string detail;
};

class MemoryControl {
public:
    virtual ~MemoryControl() = default;

    virtual uint32_t TrimProcessWorkingSets(const std::vector<uint32_t>& pids) = 0;
    // level 1 = low-priority standby only (NT command 5), 2 = whole standby list (NT command 4); 0 does nothing.
    virtual StandbyPurgeOutcome PurgeStandbyList(uint8_t level, bool dry_run) = 0;
};

} // namespace vegaflow
