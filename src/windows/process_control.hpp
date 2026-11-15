#pragma once

#include "ports/process_control.hpp"

namespace vegaflow {

class WindowsProcessControl final : public ProcessControl {
public:
    uint32_t CurrentPid() const override;
    uint64_t TickCountMs() const override;
    uint64_t QueryCreationTime(uint32_t pid) const override;
    bool QueryCpuTime100ns(uint32_t pid, uint64_t expected_creation_time, uint64_t& out_total_100ns) const override;
    bool VerifyProcessIdentity(uint32_t pid, uint64_t expected_creation_time) const override;

    bool BoostGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        bool is_dx12_or_vulkan,
        const std::vector<uint32_t>& game_cpu_sets,
        GameBoostOutcome& out,
        uint32_t& out_orig_priority_class,
        bool dry_run) override;

    bool BoostPresenterThread(uint32_t thread_id, bool dry_run) override;

    bool RestoreGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        uint32_t orig_priority_class) override;

    BackgroundApplyOutcome ApplyBackgroundPolicy(
        uint32_t pid,
        uint64_t expected_creation_time,
        ProcessAction action,
        const std::vector<uint32_t>& bg_cpu_sets,
        bool dry_run) override;

    bool UpdateBackgroundCpuSets(
        uint32_t pid,
        uint64_t expected_creation_time,
        const std::vector<uint32_t>& bg_cpu_sets) override;

    bool RestoreBackgroundProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        ProcessAction applied_action) override;
};

} // namespace vegaflow
