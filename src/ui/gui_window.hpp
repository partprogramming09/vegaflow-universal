#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "session/system_info.hpp"

namespace vegaflow {

struct GuiSteamGameItem {
    uint32_t app_id = 0;
    std::string name;
    std::string exe_name;
    std::string engine_name;
    bool has_anticheat = false;
    bool flip_model_applied = false;
    bool engine_preset_applied = false;
    bool dxvk_installed = false;
};

struct GuiDashboardData {
    bool is_elevated = false;
    std::string cpu_summary;
    std::string gpu_summary;
    uint32_t physical_cores = 0;
    uint32_t logical_processors = 0;
    std::vector<uint32_t> game_cpu_sets;
    std::vector<uint32_t> background_cpu_sets;

    uint64_t total_ram_mb = 0;
    uint64_t free_ram_mb = 0;
    uint64_t standby_ram_mb = 0;
    uint64_t modified_ram_mb = 0;
    uint64_t vram_local_used_mb = 0;
    uint64_t vram_local_budget_mb = 0;

    // Results of the last purge
    uint64_t last_purge_before_free_mb = 0;
    uint64_t last_purge_after_free_mb = 0;
    uint32_t last_trimmed_processes = 0;
    uint32_t noisy_bg_processes_count = 0;
    uint32_t isolated_bg_processes_count = 0;

    double timer_resolution_ms = 15.625;
    bool timer_forced_manual = false;
    bool mpo_disabled = false;
    bool mem_compression_enabled = false;
    bool amd_shader_cache_forced = false;

    bool game_active = false;
    bool game_is_foreground = false;
    std::string active_game_name;
    std::string active_game_path;
    std::string active_game_api;
    uint32_t active_game_pid = 0;

    // Modern telemetry for 3D games (EcoQoS, I/O hierarchy and DirectFlip)
    uint32_t ecoqos_processes_count = 0;
    bool io_priority_active = false;
    bool directflip_active = false;

    double avg_fps = 0.0;
    double fps_1_low = 0.0;
    double fps_0_1_low = 0.0;
    double frametime_stddev_ms = 0.0;

    uint64_t daemon_private_ws_kb = 0;
    std::string last_action_status;
    std::vector<std::string> audit_log;

    uint32_t running_steam_app_id = 0;
    std::string active_game_engine;
    std::vector<GuiSteamGameItem> steam_games;

    bool mmcss_gaming_optimized = false;
    bool amd_dx11_uma_boost = false;
    uint32_t win32_priority_separation = 2;
    bool apu_gpu_priority_active = false;

    // APU Silicon Suite & NT Kernel Memory Hardening
    bool kernel_paging_shield_active = false;
    uint32_t amd_tessellation_max = 16;

    // Round 2 and Group 2B optimizations
    bool mmcss_task_active = false;          // Tasks\Games GPU Priority == 8 && Clock Rate == 10000
    bool apu_shadows_active = false;         // APU shadow profile loaded
    bool amd_vram_override_active = false;   // UMA VRAM override (4 GB DXGI/AMD)
    uint32_t amd_vram_reported_mb = 0;       // Reported budget in MB (e.g. 4096)

    // SystemInfo & SandboxGuard security subtopic
    SystemInfoSnapshot system_info{};
    size_t sandbox_violations = 0;
    bool sandbox_guard_active = true;

    // Real-time ApuCapabilityGuard telemetry
    double dxgi_pressure_ratio = 0.0;
    uint32_t uma_guard_interventions = 0;
    bool uma_guard_triggered_live = false;
};

struct GuiCallbacks {
    std::function<GuiDashboardData()> on_PollTelemetry;
    std::function<std::string()> on_PurgeNow;
    std::function<std::string()> on_ToggleMpo;
    std::function<bool()> on_RequestElevate;
    std::function<std::string()> on_ToggleMemCompression;
    // New UXTU-style callbacks:
    std::function<std::string()> on_ToggleTimerForce;
    std::function<std::string()> on_ToggleShaderCache;
    std::function<std::string()> on_IsolateBackgroundNow;
    std::function<std::string()> on_RestoreCpuAffinity;
    std::function<std::string()> on_RestoreAllDefaults;
    std::function<std::string()> on_RescanSteamLibrary;
    std::function<std::string(uint32_t)> on_ApplyGameEnginePreset;
    std::function<std::string(uint32_t)> on_RestoreGameEngineConfig;
    std::function<std::string(uint32_t)> on_ToggleGameDxvk;
    std::function<std::string()> on_ToggleMmcssGaming;
    std::function<std::string()> on_ToggleApuPowerMode;
    std::function<std::string()> on_ApplyAllGamingTweaks;
    std::function<std::string()> on_ToggleVramOverride;
};

class GuiWindow {
public:
    static int RunGuiLoop(const GuiCallbacks& callbacks);
};

} // namespace vegaflow
