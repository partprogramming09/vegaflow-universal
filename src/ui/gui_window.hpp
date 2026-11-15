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

#include "session/config_screen.hpp"
#include "session/equipment_screen.hpp"
#include "session/history_screen.hpp"
#include "session/games_screen.hpp"
#include "session/programs_screen.hpp"
#include "session/settings_screen.hpp"
#include "session/system_info.hpp"

namespace vegaflow {

struct GuiDashboardData {
    bool is_elevated = false;
    std::string cpu_summary;
    std::string gpu_summary;
    uint32_t physical_cores = 0;
    uint32_t logical_processors = 0;
    std::vector<session::CpuSetRef> cpu_sets; // every logical processor in order, with its CPU set id and core
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

    bool mpo_disabled = false;
    bool mem_compression_enabled = false;
    bool has_amd_driver = false;
    bool has_power_mode = false;
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

    uint64_t daemon_private_ws_kb = 0;
    std::string last_action_status;

    uint32_t running_steam_app_id = 0;
    std::string active_game_engine;
    bool has_steam_games = false; // Requisitos: whether a Steam library was found

    bool restrict_game_cpu_sets = false; // Configuración: the game is limited to its own cores
    bool mmcss_gaming_optimized = false;
    bool amd_dx11_uma_boost = false;
    uint32_t win32_priority_separation = 2;
    bool apu_gpu_priority_active = false;

    // APU Silicon Suite & NT Kernel Memory Hardening
    bool kernel_paging_shield_active = false;
    uint32_t amd_tessellation_max = 16;

    // Round 2 and Group 2B optimizations
    bool mmcss_task_active = false;          // Tasks\Games Scheduling Category == High && Clock Rate == 10000
    bool apu_shadows_active = false;         // APU shadow profile loaded
    bool amd_vram_override_active = false;   // UMA VRAM override (4 GB DXGI/AMD)
    uint32_t amd_vram_reported_mb = 0;       // Reported budget in MB (e.g. 4096)

    // SystemInfo & SandboxGuard security subtopic
    SystemInfoSnapshot system_info{};
    size_t sandbox_violations = 0;
    bool sandbox_guard_active = true;

    // Real-time ApuCapabilityGuard telemetry
    double dxgi_pressure_ratio = 0.0;
    double dxgi_pressure_threshold = 0.0;
    uint32_t uma_guard_interventions = 0;
    bool uma_guard_triggered_live = false;
    bool automatic = true;           // false in safe mode: nothing is detected or changed by VegaFlow itself
    bool uma_guard_enabled = true;   // the guardian runs: policy on and not safe mode
};

struct GuiCallbacks {
    std::function<GuiDashboardData()> on_PollTelemetry;
    std::function<std::string()> on_PurgeNow;
    std::function<std::string()> on_ToggleMpo;
    std::function<bool()> on_RequestElevate;
    std::function<std::string()> on_ToggleMemCompression;
    std::function<std::string()> on_ToggleShaderCache;
    std::function<std::string()> on_IsolateBackgroundNow;
    std::function<std::string()> on_RestoreCpuAffinity;
    std::function<std::string()> on_RestoreAllDefaults;
    std::function<std::string()> on_RescanSteamLibrary;
    std::function<std::string(uint32_t)> on_ApplyGameEnginePreset;
    std::function<std::string(uint32_t)> on_RestoreGameEngineConfig;
    std::function<std::string(uint32_t)> on_ToggleGameDxvk;
    // Juegos screen: the rows now, "Me fue mal en este juego" (restore if applied, then exclude) and "Volver a incluir".
    std::function<session::GamesScreenModel()> on_PollGames;
    std::function<std::string(uint32_t)> on_ExcludeGame;
    std::function<std::string(uint32_t)> on_IncludeGame;
    std::function<std::string()> on_ToggleMmcssGaming;
    std::function<std::string()> on_ToggleApuPowerMode;
    std::function<std::string()> on_ToggleVramOverride;
    // Ajustes del sistema screen: what the machine and the settings look like now, and turning one on or off by id.
    std::function<SettingsScreenInputs()> on_PollSettings;
    std::function<std::string(const std::string& setting_id, bool turn_on)> on_SetSetting;
    std::function<session::HistoryScreenModel()> on_PollHistory;
    std::function<std::string(uint64_t journal_id)> on_UndoChange; // undoes one row of Historial
    // Configuración: each edit is checked, saved to config.json and answered with a sentence; Poll returns what is saved.
    std::function<session::ConfigScreenModel()> on_PollConfig;
    std::function<std::string(const std::string& id, const std::string& text)> on_SetConfigNumber;
    std::function<std::string(const std::string& id, bool on)> on_SetConfigToggle;
    std::function<std::string()> on_ResetConfig;
    // Programas: same pattern; `action` is a vegaflow::ProcessAction value.
    std::function<session::ProgramsScreenModel()> on_PollPrograms;
    std::function<std::string(const std::string& id, int action)> on_SetCategoryAction;
    std::function<std::string(const std::string& exe)> on_AddException;
    std::function<std::string(const std::string& exe, int action)> on_SetExceptionAction;
    std::function<std::string(const std::string& exe)> on_RemoveException;
    std::function<std::string(const std::string& exe)> on_AddManualGame;
    std::function<std::string(const std::string& exe)> on_RemoveManualGame;
};

class GuiWindow {
public:
    static int RunGuiLoop(const GuiCallbacks& callbacks);
};

} // namespace vegaflow
