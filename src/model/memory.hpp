#pragma once
// Plain data of the memory optimizer: its thresholds and what each pass did (optimize/MemoryManager fills them).

#include <cstdint>
#include <string>

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
    uint8_t purge_level = 0;  // E2.3: level of the NT purge that ran (1 low-priority, 2 whole list), 0 when none
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

} // namespace vegaflow
