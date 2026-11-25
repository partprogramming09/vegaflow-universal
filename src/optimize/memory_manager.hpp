#pragma once

#include "model/hardware.hpp"
#include "model/memory.hpp"
#include "ports/clock.hpp"
#include "ports/memory_control.hpp"
#include "ports/memory_probe.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

class MemoryManager {
public:
    // Production constructor: defaults to real steady clock and Windows memory probe/control.
    explicit MemoryManager(const MemoryPurgeConfig& config = {}, Clock& clock = SteadyClock::Instance());
    // Test constructor: injects fake clock, probe, and control without touching Windows.
    MemoryManager(const MemoryPurgeConfig& config, Clock& clock, MemoryProbe& probe, MemoryControl& control);

    // The GPU whose VRAM is sampled: hw.primary_gpu.luid, refreshed whenever the hardware profile changes (0 = first adapter).
    void SetTargetLuid(uint64_t luid) { target_luid_ = luid; }
    uint64_t TargetLuid() const { return target_luid_; }

    SystemMemoryTelemetry SampleTelemetry() const;

    // Evaluates memory thresholds, shared cooldown, and per-tick disk read rate via Rust,
    // then trims allowed background processes (respecting factory Do-Not-Trim exclusions) and purges Standby List.
    MemoryActionReport EvaluateAndOptimize(
        uint32_t active_game_pid,
        const std::vector<uint32_t>& background_pids,
        bool dry_run = false);

    ManualPurgeReport ForcePurgeNow(const std::vector<uint32_t>& background_pids, bool dry_run = false);

    // Real-time pressure guard for the UMA budget (integrated GPU only, sharing the 90s cooldown).
    UmaGuardStatus EvaluateUmaPressureGuard(
        const SystemMemoryTelemetry& telem,
        const ApuOptimizationPolicy& policy,
        const std::vector<uint32_t>& background_pids = {},
        bool dry_run = false);

    uint32_t GetUmaGuardInterventionsCount() const noexcept { return guard_interventions_count_; }
    double GetLastDxgiPressureRatio() const noexcept { return last_dxgi_pressure_ratio_; }
    bool IsUmaGuardCurrentlyTriggered() const noexcept { return last_guard_triggered_; }

    static uint64_t GetCurrentProcessPrivateWorkingSetBytes();
    static bool PurgeStandbyList(bool dryRun = false);
    static bool QueryStandbyMemoryMib(uint64_t& standbyMib, uint64_t& freeMib);

private:
    int64_t ElapsedSinceLastPurgeMs(std::chrono::steady_clock::time_point now) const;
    uint64_t SampleDiskReadRateBytesPerSec(uint32_t active_game_pid, std::chrono::steady_clock::time_point now);
    std::vector<uint32_t> FilterTrimmableBackgroundPids(
        uint32_t active_game_pid,
        const std::vector<uint32_t>& background_pids) const;

    MemoryPurgeConfig config_;
    Clock& clock_;
    MemoryProbe& probe_;
    MemoryControl& control_;
    uint64_t target_luid_ = 0;
    std::chrono::steady_clock::time_point last_purge_time_{};
    uint32_t last_io_pid_ = 0;
    uint64_t last_game_read_bytes_ = 0;
    std::chrono::steady_clock::time_point last_io_sample_time_{};
    uint64_t last_disk_read_rate_bytes_per_sec_ = 0;
    uint32_t guard_interventions_count_ = 0;
    double last_dxgi_pressure_ratio_ = 0.0;
    bool last_guard_triggered_ = false;
    // E2.3: level of the last successful purge in this pressure episode; back to 0 when a tick sees no pressure.
    uint8_t last_purge_level_ = 0;
};

} // namespace vegaflow
