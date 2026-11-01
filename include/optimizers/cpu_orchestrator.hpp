#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <vector>

namespace vegaflow {

struct CpuOptimizationResult {
    bool game_handle_full_access = false; // true si PROCESS_SET_INFORMATION fue permitido, false si Anti-Cheat lo degradó
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

    // Aplica optimizaciones al proceso del juego (con fallback seguro ante Anti-Cheat) y confina procesos de fondo
    CpuOptimizationResult ApplyOptimizations(
        uint32_t game_pid,
        uint32_t presenter_thread_id,
        bool is_dx12_or_vulkan,
        const std::vector<ULONG>& game_cpu_sets,
        const std::vector<ULONG>& background_cpu_sets,
        const std::vector<uint32_t>& background_pids,
        bool dry_run = false);

    // Restaura los CPU Sets de los procesos de fondo al salir del juego
    void RestoreAll(bool dry_run = false);

    uint32_t GetIsolatedBackgroundCount() const noexcept { return static_cast<uint32_t>(modified_background_pids_.size()); }
    uint32_t GetThrottledBackgroundCount() const noexcept { return static_cast<uint32_t>(modified_background_pids_.size()); }

private:
    std::vector<uint32_t> modified_background_pids_;
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
