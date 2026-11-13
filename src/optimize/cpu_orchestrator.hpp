#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "model/game.hpp"
#include "ports/process_control.hpp"

#include <cstdint>
#include <memory>
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

struct TrackedBackgroundProcess {
    uint32_t pid = 0;
    uint64_t creation_time = 0;
    ProcessAction action = ProcessAction::Throttle;
};

class CpuOrchestrator {
public:
    CpuOrchestrator();
    explicit CpuOrchestrator(ProcessControl& process_control);
    ~CpuOrchestrator();

    CpuOrchestrator(const CpuOrchestrator&) = delete;
    CpuOrchestrator& operator=(const CpuOrchestrator&) = delete;
    CpuOrchestrator(CpuOrchestrator&&) noexcept = delete;
    CpuOrchestrator& operator=(CpuOrchestrator&&) noexcept = delete;

    // Applies optimizations to the game process (restoring any previous game first) and classified background targets.
    CpuOptimizationResult ApplyOptimizations(
        uint32_t game_pid,
        uint64_t game_creation_time,
        uint32_t presenter_thread_id,
        bool is_dx12_or_vulkan,
        const std::vector<ULONG>& game_cpu_sets,
        const std::vector<ULONG>& background_cpu_sets,
        const std::vector<BackgroundProcessTarget>& background_targets,
        bool dry_run = false);

    // Compatibility overload taking raw background PIDs (treated as ProcessAction::Throttle).
    CpuOptimizationResult ApplyOptimizations(
        uint32_t game_pid,
        uint32_t presenter_thread_id,
        bool is_dx12_or_vulkan,
        const std::vector<ULONG>& game_cpu_sets,
        const std::vector<ULONG>& background_cpu_sets,
        const std::vector<uint32_t>& background_pids,
        bool dry_run = false);

    // Restores only the processes that were actually modified and still match their (pid, creation_time).
    void RestoreAll(bool dry_run = false);

    uint32_t GetIsolatedBackgroundCount() const noexcept {
        uint32_t count = 0;
        for (const auto& e : modified_background_) {
            if (e.action == ProcessAction::Throttle) ++count;
        }
        return count;
    }
    uint32_t GetThrottledBackgroundCount() const noexcept { return static_cast<uint32_t>(modified_background_.size()); }

    bool IsBackgroundConfined() const noexcept { return bg_confine_active_; }
    uint32_t ActiveGamePid() const noexcept { return last_optimized_game_pid_; }
    uint64_t ActiveGameCreationTime() const noexcept { return last_optimized_game_creation_time_; }

private:
    void RestoreTrackedGame();
    bool UpdateBackgroundLoad(const std::vector<BackgroundProcessTarget>& targets, uint32_t game_pid);
    void SetModifiedBackgroundCpuSets(const std::vector<uint32_t>& sets);

    std::unique_ptr<ProcessControl> owned_pc_;
    ProcessControl* pc_ = nullptr;

    std::vector<TrackedBackgroundProcess> modified_background_;
    std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> bg_cpu_prev_; // pid -> {creation_time, cpu_100ns}
    uint64_t bg_prev_tick_ms_ = 0;
    bool bg_confine_active_ = false;

    uint32_t last_optimized_game_pid_ = 0;
    uint64_t last_optimized_game_creation_time_ = 0;
    uint32_t last_game_orig_priority_class_ = NORMAL_PRIORITY_CLASS;
    uint32_t last_presenter_tid_ = 0;
    bool last_game_had_full_access_ = false;
    bool last_power_throttling_disabled_ = false;
    bool last_io_priority_elevated_ = false;
    bool last_memory_priority_elevated_ = false;
    bool last_game_cpu_sets_applied_ = false;
    bool last_presenter_boosted_ = false;
};

} // namespace vegaflow
