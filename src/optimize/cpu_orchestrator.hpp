#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vegaflow {

struct CpuOptimizationResult {
    bool game_handle_full_access = false; // true if PROCESS_SET_INFORMATION was allowed, false if Anti-Cheat degraded it
    bool power_throttling_disabled = false;
    bool io_priority_elevated = false;
    bool memory_priority_elevated = false;
    bool game_cpu_sets_applied = false;
    bool presenter_thread_boosted = false;
    uint32_t background_processes_isolated = 0;
    uint32_t background_processes_throttled = 0;
    bool background_io_deprioritized = false;
    bool background_memory_deprioritized = false;
};

class CpuOrchestrator {
public:
    CpuOrchestrator() = default;
    ~CpuOrchestrator();

    CpuOrchestrator(const CpuOrchestrator&) = delete;
    CpuOrchestrator& operator=(const CpuOrchestrator&) = delete;
    CpuOrchestrator(CpuOrchestrator&&) noexcept = default;
    CpuOrchestrator& operator=(CpuOrchestrator&&) noexcept = default;

    // Applies optimizations to the game process (with a safe fallback under Anti-Cheat) and confines background processes
    CpuOptimizationResult ApplyOptimizations(
        uint32_t game_pid,
        uint32_t presenter_thread_id,
        bool is_dx12_or_vulkan,
        const std::vector<ULONG>& game_cpu_sets,
        const std::vector<ULONG>& background_cpu_sets,
        const std::vector<uint32_t>& background_pids,
        bool dry_run = false);

    // Restores the CPU Sets of background processes when the game exits
    void RestoreAll(bool dry_run = false);

    uint32_t GetIsolatedBackgroundCount() const noexcept { return static_cast<uint32_t>(modified_background_pids_.size()); }
    uint32_t GetThrottledBackgroundCount() const noexcept { return static_cast<uint32_t>(modified_background_pids_.size()); }

    // Whether the background is confined to the background CPU Sets (only when its CPU usage justifies it).
    bool IsBackgroundConfined() const noexcept { return bg_confine_active_; }

private:
    // Measures background CPU usage since the previous call and updates bg_confine_active_ with hysteresis.
    // Returns true if the state changed.
    bool UpdateBackgroundLoad(const std::vector<uint32_t>& background_pids, uint32_t game_pid);
    // Applies (non-empty sets) or removes (empty sets) the CPU Sets of the already-modified background processes.
    void SetModifiedBackgroundCpuSets(const std::vector<ULONG>& sets);

    std::vector<uint32_t> modified_background_pids_;
    std::unordered_map<uint32_t, ULONGLONG> bg_cpu_prev_;
    ULONGLONG bg_prev_tick_ms_ = 0;
    bool bg_confine_active_ = false;
    uint32_t last_optimized_game_pid_ = 0;
    uint32_t last_presenter_tid_ = 0;
    bool last_game_had_full_access_ = false;
    bool last_power_throttling_disabled_ = false;
    bool last_io_priority_elevated_ = false;
    bool last_memory_priority_elevated_ = false;
    bool last_game_cpu_sets_applied_ = false;
    bool last_presenter_boosted_ = false;
};

} // namespace vegaflow
