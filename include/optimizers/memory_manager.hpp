#pragma once

#include "hardware_profiler.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

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
    uint64_t free_and_zero_bytes = 0;  // (ZeroPageCount + FreePageCount) * 4096
    uint64_t standby_bytes = 0;        // Suma de PageCountByPriority[0..7] * 4096
    uint64_t modified_bytes = 0;       // ModifiedPageCount * 4096
    VideoMemoryTelemetry vram;
};

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
    explicit MemoryManager(const MemoryPurgeConfig& config = {});
    SystemMemoryTelemetry SampleTelemetry() const;

    // Evalúa la histéresis, verifica que el juego no esté en pico de lectura de disco y ejecuta trim de procesos de fondo + purga de Standby List
    MemoryActionReport EvaluateAndOptimize(
        uint32_t active_game_pid,
        const std::vector<uint32_t>& background_pids,
        bool dry_run = false);

    ManualPurgeReport ForcePurgeNow(const std::vector<uint32_t>& background_pids, bool dry_run = false);

    // Guard de presión en tiempo real de presupuesto UMA (DXGI Budget y RAM libre)
    UmaGuardStatus EvaluateUmaPressureGuard(
        const SystemMemoryTelemetry& telem,
        const ApuOptimizationPolicy& policy,
        const std::vector<uint32_t>& background_pids = {},
        bool dry_run = false);

    uint32_t GetUmaGuardInterventionsCount() const noexcept { return guard_interventions_count_; }
    double GetLastDxgiPressureRatio() const noexcept { return last_dxgi_pressure_ratio_; }
    bool IsUmaGuardCurrentlyTriggered() const noexcept { return last_guard_triggered_; }

    static uint64_t GetCurrentProcessPrivateWorkingSetBytes();

    // Mejora 7: Memory Manager Standby List Purge & UMA Bus Prioritizer
    static bool PurgeStandbyList(bool dryRun = false);
    static bool QueryStandbyMemoryMib(uint64_t& standbyMib, uint64_t& freeMib);

private:
    MemoryPurgeConfig config_;
    std::chrono::steady_clock::time_point last_purge_time_{};
    mutable uint64_t last_game_read_bytes_ = 0;
    mutable std::chrono::steady_clock::time_point last_io_sample_time_{};
    uint32_t guard_interventions_count_ = 0;
    double last_dxgi_pressure_ratio_ = 0.0;
    bool last_guard_triggered_ = false;
};

} // namespace vegaflow
