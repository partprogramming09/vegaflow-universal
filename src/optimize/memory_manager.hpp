#pragma once

#include "model/hardware.hpp"
#include "ports/clock.hpp"
#include "ports/memory_control.hpp"
#include "ports/memory_probe.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

struct MemoryPurgeConfig {
    uint64_t min_free_ram_mb = 1200;
    uint64_t min_standby_mb = 1500;
    uint64_t max_game_disk_read_bytes_per_sec = 15ULL * 1024 * 1024; // 15 MB/s
    uint32_t cooldown_seconds = 90;
    bool is_uma_architecture = true;
};

struct MemoryActionReport {
    bool standby_purged = false;
    uint32_t background_processes_trimmed = 0;
    bool skipped_due_to_cooldown = false;
    bool skipped_due_to_disk_io = false;
    std::string reason;
};

struct ManualPurgeReport {
    uint64_t free_bytes_before = 0;
    uint64_t free_bytes_after = 0;
    uint64_t standby_bytes_before = 0;
    uint64_t standby_bytes_after = 0;
    uint32_t trimmed_processes = 0;
    bool standby_purge_succeeded = false;
    std::string status_message;
};

struct UmaGuardStatus {
    bool guard_triggered = false;
    double dxgi_budget_utilization = 0.0;
    uint64_t available_os_headroom_mb = 0;
    uint32_t total_interventions = 0;
    std::string active_reason;
};

class MemoryManager {
public:
    // Production constructor: defaults to real steady clock and Windows memory probe/control.
    explicit MemoryManager(const MemoryPurgeConfig& config = {}, Clock& clock = SteadyClock::Instance());
    // Test constructor: injects fake clock, probe, and control without touching Windows.
    MemoryManager(const MemoryPurgeConfig& config, Clock& clock, MemoryProbe& probe, MemoryControl& control);

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
    std::chrono::steady_clock::time_point last_purge_time_{};
    uint32_t last_io_pid_ = 0;
    uint64_t last_game_read_bytes_ = 0;
    std::chrono::steady_clock::time_point last_io_sample_time_{};
    uint64_t last_disk_read_rate_bytes_per_sec_ = 0;
    uint32_t guard_interventions_count_ = 0;
    double last_dxgi_pressure_ratio_ = 0.0;
    bool last_guard_triggered_ = false;
};

} // namespace vegaflow
