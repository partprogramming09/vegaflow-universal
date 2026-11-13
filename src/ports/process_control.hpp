#pragma once
// Port for OS process/thread QoS, priority, memory priority, I/O priority, CPU Sets, and PID+creation_time identity checks.

#include "model/game.hpp"

#include <cstdint>
#include <vector>

namespace vegaflow {

struct GameBoostOutcome {
    bool full_access = false;
    bool power_throttling_disabled = false;
    bool io_priority_elevated = false;
    bool memory_priority_elevated = false;
    bool cpu_sets_applied = false;
};

struct BackgroundApplyOutcome {
    bool modified = false;
    bool power_throttling_applied = false;
    bool memory_deprioritized = false;
    bool io_deprioritized = false;
    bool cpu_sets_applied = false;
};

class ProcessControl {
public:
    virtual ~ProcessControl() = default;

    virtual uint32_t CurrentPid() const = 0;
    virtual uint64_t TickCountMs() const = 0;

    // Returns process creation timestamp in 100ns units, or 0 if unavailable.
    virtual uint64_t QueryCreationTime(uint32_t pid) const = 0;

    // Verifies PID + creation_time identity (if expected_creation_time != 0) and returns total kernel+user CPU time in 100ns units.
    virtual bool QueryCpuTime100ns(uint32_t pid, uint64_t expected_creation_time, uint64_t& out_total_100ns) const = 0;

    // Returns true only if PID exists and (when expected_creation_time != 0) its live creation_time matches.
    virtual bool VerifyProcessIdentity(uint32_t pid, uint64_t expected_creation_time) const = 0;

    virtual bool BoostGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        bool is_dx12_or_vulkan,
        const std::vector<uint32_t>& game_cpu_sets,
        GameBoostOutcome& out,
        uint32_t& out_orig_priority_class,
        bool dry_run) = 0;

    virtual bool BoostPresenterThread(uint32_t thread_id, bool dry_run) = 0;

    virtual bool RestoreGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        uint32_t orig_priority_class) = 0;

    virtual BackgroundApplyOutcome ApplyBackgroundPolicy(
        uint32_t pid,
        uint64_t expected_creation_time,
        ProcessAction action,
        const std::vector<uint32_t>& bg_cpu_sets,
        bool dry_run) = 0;

    virtual bool UpdateBackgroundCpuSets(
        uint32_t pid,
        uint64_t expected_creation_time,
        const std::vector<uint32_t>& bg_cpu_sets) = 0;

    virtual bool RestoreBackgroundProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        ProcessAction applied_action) = 0;
};

} // namespace vegaflow
