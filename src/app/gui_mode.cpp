#include "app/gui_mode.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "ui/gui_window.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "windows/safe_file_system.hpp"
#include "windows/sandbox_guard.hpp"
#include "windows/change_journal.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "bridge/rust_bridge.h"
#include "session/system_info.hpp"
#include "session/config_screen.hpp"
#include "session/history_screen.hpp"
#include "session/programs_screen.hpp"
#include "session/restore.hpp"
#include <filesystem>
#include "app/app_state.hpp"
#include "session/config.hpp"
#include "session/config_actions.hpp"
#include "session/game_actions.hpp"
#include "session/settings_actions.hpp"
#include "session/reports.hpp"
#include "session/run_cycle.hpp"
#include "windows/system_counters.hpp"
#include "windows/text_convert.hpp"

namespace vegaflow::app {

int RunGuiMode(
    AppConfig& config,
    ConfigStore& config_store,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    bool safe_mode,
    const std::string& journal_warning) {
    using namespace vegaflow;
    // Built once from the boot profile, the same way each optimizer used to rebuild it on every call.
    const ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw);
    SafeFileSystem file_system;
    WindowsRegistryPort reg;
    WindowsPowerPort power;
    GraphicsOptimizer gfx(hw, policy, reg, &power);
    DxvkManager dxvk(file_system);
    SteamEngineOptimizer steam(file_system, &reg);
    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::wstring active_game_full_path;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    if (!safe_mode) {
        gfx.Display().SetGameDvrAndFseOptimized(true, false);
        if (gfx.PowerMode()) {
            gfx.PowerMode()->SetApuLaptopPowerProfile(true, policy, false);
        }
    }
    uint32_t last_isolated_count = 0;
    uint64_t last_purge_before_free_mb = 0;
    uint64_t last_purge_after_free_mb = 0;
    uint32_t last_trimmed_processes = 0;
    std::string last_gui_status = safe_mode
        ? "MODO SEGURO: 2+ cierres anormales seguidos. Sin cambios automáticos."
        : "VegaFlow GUI Inicializada";

    auto AddAuditLog = [&](const std::string& msg) {
        if (ChangeJournal* j = GetActiveChangeJournal()) {
            j->RecordAuditEvent("GUI", msg, "");
        }
    };
    if (!journal_warning.empty()) {
        last_gui_status = journal_warning;
    }
    if (const std::string warning = config_store.Report().Warning(); !warning.empty()) {
        last_gui_status = warning;
        AddAuditLog(warning);
    }
    // Saves the setting the person just changed; when config.json must not be overwritten, says so instead.
    auto SaveSettings = [&](std::string& msg) {
        std::string why;
        if (!config_store.Save(config, false, &why)) msg += " (no se guardó en config.json: " + why + ")";
    };

    AddAuditLog("Sesión iniciada. Hardware: " + hw.cpu_topology.brand_string + " (" +
                std::to_string(hw.cpu_topology.physical_cores) + "P/" +
                std::to_string(hw.cpu_topology.logical_processors) + "L)");
    AddAuditLog("GPU Principal: " + WideToUtf8(hw.primary_gpu.description) + " [" +
                HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) + "]");
    AddAuditLog("Estado de Privilegios: " + std::string(PrivilegeManager::IsProcessElevated() ? "ADMINISTRADOR (Acceso Total)" : "ESTANDAR (Sin elevación)"));

    SystemGraphicsTweaksState cached_tweaks = gfx.QuerySystemGraphicsTweaks();
    int tweaks_query_countdown = 10;
    uint32_t cached_noisy_count = 0;
    int noisy_query_countdown = 0;

    std::vector<SteamGameEntry> cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    SyncSteamGamesToCustomExes(cached_steam_games, config);

    // What the Juegos buttons do lives in session/game_actions; the scan and the folder read are handed in because they touch Windows.
    session::GameActions game_actions(session::GameActionsDeps{
        steam, dxvk, config, config_store, cached_steam_games, policy,
        []() {
            auto games = SteamEngineOptimizer::ScanSteamLibrary();
            SandboxGuard::ReloadSteamLibraryFolders(); // a library added in Steam becomes writable without a restart
            return games;
        },
        [](const SteamGameEntry& g) { return SteamEngineOptimizer::InspectGameDirectory(g.install_dir, g.app_id, g.name); },
        [&](const std::string& line) { AddAuditLog(line); }});
    session::SettingsActions settings_actions(session::SettingsActionsDeps{
        gfx, policy,
        [&]() { return HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes); },
        [&](const std::string& line) { AddAuditLog(line); }});
    bool flags_seen_game_active = false;

    GuiCallbacks callbacks;
    callbacks.on_PollTelemetry = [&]() -> GuiDashboardData {
        std::function<void(const std::string&)> audit_fn = AddAuditLog;
        if (!safe_mode) {
            RunSingleIteration(
                config, file_system, hw, cpu_orchestrator, mem_mgr,
                game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd,
                &active_game_full_path, &cached_noisy_count, &audit_fn);
        }

        if (!game_active) {
            if (--noisy_query_countdown <= 0) {
                noisy_query_countdown = 3;
                cached_noisy_count = static_cast<uint32_t>(ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid).size());
            }
        }

        uint32_t running_steam_appid = SteamEngineOptimizer::GetRunningSteamAppId();
        std::string active_game_engine;
        for (const auto& sg : cached_steam_games) {
            if (sg.app_id == running_steam_appid && running_steam_appid != 0) {
                active_game_engine = SteamEngineOptimizer::EngineTypeToString(sg.engine);
                break;
            }
        }
        if (game_active && active_game_engine.empty()) {
            for (const auto& sg : cached_steam_games) {
                if (!sg.primary_exe_path.empty()) {
                    std::filesystem::path p1(sg.primary_exe_path);
                    std::filesystem::path p2(active_game_full_path);
                    if (p1.filename() == p2.filename() || (!active_game_exe.empty() && p1.filename() == std::filesystem::path(active_game_exe))) {
                        active_game_engine = SteamEngineOptimizer::EngineTypeToString(sg.engine);
                        if (running_steam_appid == 0) running_steam_appid = sg.app_id;
                        break;
                    }
                }
            }
        }

        // The daemon part of this process writes game files when a game opens, so the flags of that one game are read again
        // then (closing writes nothing, and reading the whole library on the GUI thread would freeze it).
        if (game_active != flags_seen_game_active) {
            flags_seen_game_active = game_active;
            if (game_active && running_steam_appid != 0) game_actions.RefreshFlags(running_steam_appid);
        }

        auto mem_sample = mem_mgr.SampleTelemetry();
        if (!game_active) {
            const ApuOptimizationPolicy idle_policy = HardwareProfiler::BuildOptimizationPolicy(hw, mem_sample.vram.local_budget_bytes);
            if (idle_policy.apply_uma_memory_guard) {
                mem_mgr.EvaluateUmaPressureGuard(mem_sample, idle_policy, {}, true /* dry_run */);
            }
        }
        uint64_t daemon_ws = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();

        if (--tweaks_query_countdown <= 0) {
            tweaks_query_countdown = 10;
            cached_tweaks = gfx.QuerySystemGraphicsTweaks();
        }

        GuiDashboardData data;
        data.is_elevated = PrivilegeManager::IsProcessElevated();
        data.cpu_summary = hw.cpu_topology.brand_string.empty() ? "No se pudo leer" : hw.cpu_topology.brand_string;
        data.gpu_summary = hw.gpu_probe_ok && !hw.primary_gpu.description.empty()
            ? WideToUtf8(hw.primary_gpu.description)
            : "No se pudo leer";
        data.physical_cores = hw.cpu_topology.physical_cores;
        data.logical_processors = hw.cpu_topology.logical_processors;
        data.restrict_game_cpu_sets = config.restrict_game_cpu_sets;
        {
            auto ordered = hw.cpu_topology.all_cpu_sets;
            std::sort(ordered.begin(), ordered.end(), [](const CpuCoreSetInfo& x, const CpuCoreSetInfo& y) { return x.group != y.group ? x.group < y.group : x.logical_processor_index < y.logical_processor_index; });
            for (const auto& c : ordered) data.cpu_sets.push_back({c.cpu_set_id, c.core_index, c.group});
        }
        data.game_cpu_sets.assign(hw.cpu_topology.game_optimal_cpu_set_ids.begin(), hw.cpu_topology.game_optimal_cpu_set_ids.end());
        data.background_cpu_sets.assign(hw.cpu_topology.background_cpu_set_ids.begin(), hw.cpu_topology.background_cpu_set_ids.end());

        data.total_ram_mb = mem_sample.total_physical_bytes / (1024 * 1024);
        data.free_ram_mb = mem_sample.free_and_zero_bytes / (1024 * 1024);
        data.standby_ram_mb = mem_sample.standby_bytes / (1024 * 1024);
        data.modified_ram_mb = mem_sample.modified_bytes / (1024 * 1024);
        data.vram_local_used_mb = mem_sample.vram.local_usage_bytes / (1024 * 1024);
        data.vram_local_budget_mb = mem_sample.vram.local_budget_bytes / (1024 * 1024);

        data.last_purge_before_free_mb = last_purge_before_free_mb;
        data.last_purge_after_free_mb = last_purge_after_free_mb;
        data.last_trimmed_processes = last_trimmed_processes;
        data.noisy_bg_processes_count = cached_noisy_count;
        data.isolated_bg_processes_count = std::max(last_isolated_count, cpu_orchestrator.GetIsolatedBackgroundCount());

        data.mpo_disabled = cached_tweaks.mpo_disabled;
        data.mem_compression_enabled = cached_tweaks.memory_compression_enabled;
        data.has_amd_driver = (gfx.AmdDriver() != nullptr);
        data.has_power_mode = (gfx.PowerMode() != nullptr);
        data.amd_shader_cache_forced = cached_tweaks.amd_shader_cache_forced;
        data.mmcss_gaming_optimized = cached_tweaks.mmcss_gaming_optimized;
        data.amd_dx11_uma_boost = cached_tweaks.amd_dx11_uma_boost;
        data.win32_priority_separation = cached_tweaks.win32_priority_separation;
        data.apu_gpu_priority_active = cached_tweaks.apu_power_optimized;

        // APU Silicon Suite Sync & NT Kernel Hardening
        data.kernel_paging_shield_active = cached_tweaks.kernel_paging_shield_active;
        data.amd_tessellation_max = (cached_tweaks.amd_tessellation_max > 0) ? cached_tweaks.amd_tessellation_max : 16;
        data.amd_vram_override_active = cached_tweaks.amd_vram_override_active;
        data.amd_vram_reported_mb = cached_tweaks.amd_vram_reported_mb;

        // Query live state of MMCSS task tweaks
        std::string mmcss_cat;
        std::string mmcss_sfio;
        uint32_t mmcss_clock = 0;
        gfx.Mmcss().QueryTaskTweaks(mmcss_cat, mmcss_sfio, mmcss_clock);
        data.mmcss_task_active = (mmcss_cat == "High" && mmcss_clock == 10000);

        data.apu_shadows_active = true;

        data.game_active = game_active;
        data.game_is_foreground = (game_active && (GetForegroundWindow() == active_game_hwnd));
        data.active_game_name = WideToUtf8(active_game_exe);
        data.active_game_path = WideToUtf8(active_game_full_path);
        data.active_game_api = active_game_api;
        data.active_game_pid = active_game_pid;

        // Modern suite for 3D games (EcoQoS, I/O hierarchy and DirectFlip)
        data.ecoqos_processes_count = game_active ? cpu_orchestrator.GetThrottledBackgroundCount() : CountEcoQoSProcesses();
        data.io_priority_active = game_active;
        data.directflip_active = game_active
            ? (gfx.DxgiFlip().HasDirectFlipUpgrade(active_game_full_path) || gfx.DxgiFlip().HasDirectFlipUpgrade(active_game_exe))
            : (CountDirectFlipConfiguredGames() > 0);

        data.daemon_private_ws_kb = daemon_ws / 1024;
        data.last_action_status = last_gui_status;

        data.running_steam_app_id = running_steam_appid;
        data.active_game_engine = active_game_engine;
        data.has_steam_games = !cached_steam_games.empty();

        data.system_info = SystemInfo::FromHardware(hw, true);
        data.sandbox_violations = SandboxGuard::GetViolationCount();
        data.sandbox_guard_active = true;

        data.dxgi_pressure_ratio = mem_mgr.GetLastDxgiPressureRatio();
        data.dxgi_pressure_threshold = vf_rust_vram_pressure_threshold();
        data.uma_guard_interventions = mem_mgr.GetUmaGuardInterventionsCount();
        data.automatic = !safe_mode;
        data.uma_guard_enabled = policy.apply_uma_memory_guard && !safe_mode;
        data.uma_guard_triggered_live = mem_mgr.IsUmaGuardCurrentlyTriggered();

        return data;
    };

    callbacks.on_PurgeNow = [&]() -> std::string {
        auto noisy = ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid);
        auto rep = mem_mgr.ForcePurgeNow(noisy, false);
        last_purge_before_free_mb = rep.free_bytes_before / (1024 * 1024);
        last_purge_after_free_mb = rep.free_bytes_after / (1024 * 1024);
        last_trimmed_processes = rep.trimmed_processes;
        last_gui_status = rep.status_message;
        AddAuditLog("Purga de Standby List y recorte de WS ejecutada: " + rep.status_message);
        return rep.status_message;
    };

    callbacks.on_PollSettings = [&]() -> SettingsScreenInputs {
        SettingsScreenInputs in;
        in.device.vendor = hw.primary_gpu.vendor;
        in.device.connection = hw.primary_gpu.connection;
        in.device.driver_tweaks_allowed = !policy.block_driver_tweaks;
        if (tweaks_query_countdown <= 0) { // a switch click sets this to 0 so the next read is fresh; otherwise the telemetry cadence rules
            cached_tweaks = gfx.QuerySystemGraphicsTweaks();
            tweaks_query_countdown = 10;
        }
        in.tweaks = cached_tweaks;
        in.memory_guard_active = policy.apply_uma_memory_guard && !safe_mode;
        return in;
    };

    auto say = [&](const std::string& msg) {
        last_gui_status = msg; // the telemetry poll copies this into the status bar, so every answer goes through it
        return msg;
    };
    // The switches live in session/settings_actions; the screen re-reads the system state after each one.
    auto setting_said = [&](const std::string& msg) {
        tweaks_query_countdown = 0;
        return say(msg);
    };
    callbacks.on_SetSetting = [&](const std::string& id, bool turn_on) { return setting_said(settings_actions.Set(id, turn_on)); };
    callbacks.on_ToggleMpo = [&]() { return setting_said(settings_actions.ToggleMpo()); };
    callbacks.on_ToggleMemCompression = [&]() { return setting_said(settings_actions.ToggleMemCompression()); };
    callbacks.on_ToggleShaderCache = [&]() { return setting_said(settings_actions.ToggleShaderCache()); };
    callbacks.on_ToggleVramOverride = [&]() { return setting_said(settings_actions.ToggleVramOverride()); };
    callbacks.on_ToggleMmcssGaming = [&]() { return setting_said(settings_actions.ToggleMmcssGaming()); };
    callbacks.on_ToggleApuPowerMode = [&]() { return setting_said(settings_actions.ToggleApuPowerMode()); };

    callbacks.on_PollHistory = [&]() -> session::HistoryScreenModel {
        ChangeJournal* j = GetActiveChangeJournal();
        return j ? session::BuildHistoryScreen(j->Entries()) : session::BuildHistoryScreen({});
    };

    callbacks.on_UndoChange = [&](uint64_t journal_id) -> std::string {
        ChangeJournal* j = GetActiveChangeJournal();
        if (!j) return "No hay historial de cambios en esta sesión.";
        WindowsSystemUndo undo(file_system);
        const auto res = session::UndoOneChange(*j, undo, journal_id);
        tweaks_query_countdown = 0;
        last_gui_status = res.message;
        AddAuditLog("Historial: " + res.message);
        return res.message;
    };

    callbacks.on_PollConfig = [&]() -> session::ConfigScreenModel { return session::BuildConfigScreen(config); };

    // Configuración and Programas edits live in session/config_actions: `config` follows only when config.json was written.
    session::ConfigActions config_actions(config, config_store, cached_steam_games, [&](const std::string& line) { AddAuditLog(line); });
    callbacks.on_SetConfigNumber = [&](const std::string& id, const std::string& text) { return say(config_actions.SetNumber(id, text)); };
    callbacks.on_SetConfigToggle = [&](const std::string& id, bool on) { return say(config_actions.SetToggle(id, on)); };
    callbacks.on_ResetConfig = [&]() { return say(config_actions.Reset()); };
    callbacks.on_PollPrograms = [&]() -> session::ProgramsScreenModel { return session::BuildProgramsScreen(config); };
    callbacks.on_SetCategoryAction = [&](const std::string& id, int action) { return say(config_actions.SetCategoryAction(id, action)); };
    callbacks.on_AddException = [&](const std::string& exe) { return say(config_actions.AddException(exe)); };
    callbacks.on_SetExceptionAction = [&](const std::string& exe, int action) { return say(config_actions.SetExceptionAction(exe, action)); };
    callbacks.on_RemoveException = [&](const std::string& exe) { return say(config_actions.RemoveException(exe)); };
    callbacks.on_AddManualGame = [&](const std::string& exe) { return say(config_actions.AddManualGame(exe)); };
    callbacks.on_RemoveManualGame = [&](const std::string& exe) { return say(config_actions.RemoveManualGame(exe)); };

    callbacks.on_IsolateBackgroundNow = [&]() -> std::string {
        auto noisy = ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid);
        std::vector<ULONG> empty_game_sets;
        auto res = cpu_orchestrator.ApplyOptimizations(
            active_game_pid, 0, false,
            empty_game_sets,
            hw.cpu_topology.background_cpu_set_ids,
            noisy,
            false);
        last_isolated_count = res.background_processes_isolated;
        std::string msg = "Aislamiento de procesos de fondo ejecutado: " + std::to_string(res.background_processes_isolated) + " procesos confinados en núcleos secundarios";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RestoreCpuAffinity = [&]() -> std::string {
        cpu_orchestrator.RestoreAll(false);
        last_isolated_count = 0;
        std::string msg = "Afinidad de procesos y CPU Sets restaurados a valores originales de Windows";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RestoreAllDefaults = [&]() -> std::string {
        RestoreReport rep{};
        if (ChangeJournal* j = GetActiveChangeJournal()) {
            WindowsSystemUndo undo(file_system);
            rep = vegaflow::session::RestoreAllRecordedChanges(*j, undo, false);
        }
        size_t reg_rev = 0, reg_tot = 0;
        settings_actions.ResetAll(&reg_rev, &reg_tot);
        tweaks_query_countdown = 0;
        cpu_orchestrator.RestoreAll(false);
        last_isolated_count = 0;
        safe_mode = false;

        std::string msg = session::RestoreAllMessage(rep, reg_rev, reg_tot);
        SaveSettings(msg);
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RescanSteamLibrary = [&]() { return say(game_actions.Rescan()); };
    callbacks.on_ApplyGameEnginePreset = [&](uint32_t app_id) { return say(game_actions.Apply(app_id)); };
    callbacks.on_RestoreGameEngineConfig = [&](uint32_t app_id) { return say(game_actions.Restore(app_id)); };
    callbacks.on_PollGames = [&]() { return game_actions.Poll(); };
    callbacks.on_ExcludeGame = [&](uint32_t app_id) { return say(game_actions.Exclude(app_id)); };
    callbacks.on_IncludeGame = [&](uint32_t app_id) { return say(game_actions.Include(app_id)); };
    callbacks.on_ToggleGameDxvk = [&](uint32_t app_id) { return say(game_actions.ToggleDxvk(app_id)); };

    int exit_code = GuiWindow::RunGuiLoop(callbacks);
    if (!safe_mode) {
        if (game_active) {
            SaveSessionReportToFile(WideToUtf8(active_game_exe), active_game_api, active_game_pid, "logs/session_reports.log");
        }
        cpu_orchestrator.RestoreAll(false);
        gfx.KernelScheduler().SetDynamicPrioritySeparation(0x02, false);
        if (gfx.PowerMode() && gfx.PowerMode()->IsApuPowerModeApplied()) {
            gfx.PowerMode()->SetApuLaptopPowerProfile(false, policy, false);
        }
        gfx.Display().SetGameDvrAndFseOptimized(false, false);
        gfx.RestoreAllRegistryBackups();
    }
    if (g_single_instance_mutex) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = nullptr;
    }
    return exit_code;
}

} // namespace vegaflow::app
