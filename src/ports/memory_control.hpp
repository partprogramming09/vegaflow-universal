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
    virtual StandbyPurgeOutcome PurgeStandbyList(bool dry_run) = 0;
};

} // namespace vegaflow
