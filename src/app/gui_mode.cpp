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
#include "session/restore.hpp"
#include <filesystem>
#include "app/app_state.hpp"
#include "session/config.hpp"
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

    std::vector<std::string> audit_log;
    auto AddAuditLog = [&](const std::string& msg) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        audit_log.push_back(std::string(buf) + msg);
        if (audit_log.size() > 200) {
            audit_log.erase(audit_log.begin());
        }
        if (ChangeJournal* j = GetActiveChangeJournal()) {
            j->RecordAuditEvent("GUI", msg, "");
        }
    };
    if (!journal_warning.empty()) {
        last_gui_status = journal_warning;
        audit_log.push_back(journal_warning);
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
        data.ecoqos_processes_count = game_active
            ? std::max(last_isolated_count, cpu_orchestrator.GetThrottledBackgroundCount())
            : CountEcoQoSProcesses();
        data.io_priority_active = game_active;
        data.directflip_active = game_active
            ? (gfx.DxgiFlip().HasDirectFlipUpgrade(active_game_full_path) || gfx.DxgiFlip().HasDirectFlipUpgrade(active_game_exe))
            : (CountDirectFlipConfiguredGames() > 0);

        data.daemon_private_ws_kb = daemon_ws / 1024;
        data.last_action_status = last_gui_status;
        if (ChangeJournal* j = GetActiveChangeJournal()) {
            data.audit_log = vegaflow::session::FormatJournalHistory(*j, 150);
            if (data.audit_log.empty()) {
                data.audit_log = audit_log;
            }
        } else {
            data.audit_log = audit_log;
        }

        data.running_steam_app_id = running_steam_appid;
        data.active_game_engine = active_game_engine;
        data.steam_games.clear();
        data.steam_games.reserve(cached_steam_games.size());
        for (const auto& sg : cached_steam_games) {
            GuiSteamGameItem item;
            item.app_id = sg.app_id;
            item.name = sg.name;
            item.exe_name = WideToUtf8(std::filesystem::path(sg.primary_exe_path).filename().wstring());
            if (item.exe_name.empty()) {
                item.exe_name = "N/A";
            }
            item.engine_name = SteamEngineOptimizer::EngineTypeToString(sg.engine);
            item.has_anticheat = sg.has_anticheat;
            item.engine_preset_applied = sg.engine_preset_applied;
            item.flip_model_applied = sg.flip_model_applied;
            item.dxvk_installed = sg.dxvk_installed;
            data.steam_games.push_back(std::move(item));
        }

        data.system_info = SystemInfo::FromHardware(hw, true);
        data.sandbox_violations = SandboxGuard::GetViolationCount();
        data.sandbox_guard_active = true;

        data.dxgi_pressure_ratio = mem_mgr.GetLastDxgiPressureRatio();
        data.dxgi_pressure_threshold = vf_rust_vram_pressure_threshold();
        data.uma_guard_interventions = mem_mgr.GetUmaGuardInterventionsCount();
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

    callbacks.on_ToggleMpo = [&]() -> std::string {
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target = !cur.mpo_disabled;
        bool ok = gfx.Display().SetMpoDisabled(target, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "MPO Desactivado (OverlayTestMode=5, no documentado)" : "MPO Restaurado a Default (OverlayTestMode=0)") : "Fallo al modificar MPO (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste DWM MPO: " + msg);
        return msg;
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
        in.memory_guard_active = policy.apply_uma_memory_guard;
        return in;
    };

    // One switch of the Ajustes screen: sets the row to `turn_on` and returns a status line. Rows with a toggle callback
    // reuse it only when the current state differs, so a second click never undoes by accident.
    callbacks.on_SetSetting = [&](const std::string& id, bool turn_on) -> std::string {
        const auto cur = gfx.QuerySystemGraphicsTweaks();
        auto toggle_if = [&](bool is_on, const std::function<std::string()>& fn) -> std::string {
            return is_on == turn_on ? std::string("Ya estaba así: sin cambios") : fn();
        };
        std::string msg;
        if (id == "vram_reporting") {
            msg = toggle_if(cur.amd_vram_override_active, callbacks.on_ToggleVramOverride);
        } else if (id == "amd_driver_profile") {
            msg = toggle_if(cur.amd_shader_cache_forced, callbacks.on_ToggleShaderCache);
        } else if (id == "mpo_disabled") {
            msg = toggle_if(cur.mpo_disabled, callbacks.on_ToggleMpo);
        } else if (id == "memory_compression") {
            msg = toggle_if(cur.memory_compression_enabled, callbacks.on_ToggleMemCompression);
        } else if (id == "power_profile") {
            msg = toggle_if(cur.apu_power_optimized, callbacks.on_ToggleApuPowerMode);
        } else if (id == "mmcss_gaming") {
            const bool ok = gfx.Mmcss().SetGamingProfileOptimized(turn_on, false);
            msg = ok ? (turn_on ? "Prioridad de multimedia para juegos activada" : "Prioridad de multimedia devuelta al valor de Windows")
                     : "No se pudo cambiar la prioridad de multimedia (hace falta ser administrador)";
        } else if (id == "priority_separation") {
            const bool ok = gfx.KernelScheduler().SetDynamicPrioritySeparation(turn_on ? 0x26 : 0x02, false);
            msg = ok ? (turn_on ? "Tiempo de CPU para la ventana activa: 0x26" : "Tiempo de CPU devuelto al valor de Windows (2)")
                     : "No se pudo cambiar Win32PrioritySeparation (hace falta ser administrador)";
        } else if (id == "paging_shield") {
            const bool ok = gfx.KernelScheduler().SetKernelMemoryPagingShield(turn_on, false);
            msg = ok ? (turn_on ? "Núcleo de Windows fijado en la RAM" : "Núcleo de Windows devuelto al comportamiento normal")
                     : "No se pudo cambiar el blindaje del núcleo (hace falta ser administrador)";
        } else if (id == "game_dvr") {
            const bool ok = gfx.Display().SetGameDvrAndFseOptimized(turn_on, false);
            msg = ok ? (turn_on ? "Grabación de Xbox Game Bar apagada" : "Grabación de Xbox Game Bar devuelta a Windows")
                     : "No se pudo cambiar la grabación de Game Bar";
        } else {
            msg = "Este ajuste no tiene interruptor";
        }
        tweaks_query_countdown = 0;
        last_gui_status = msg;
        AddAuditLog("Ajustes: " + id + (turn_on ? " activar: " : " desactivar: ") + msg);
        return msg;
    };

    callbacks.on_RequestElevate = [&]() -> bool {
        AddAuditLog("Solicitud de elevación UAC enviada...");
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        bool ok = PrivilegeManager::RelaunchAsAdmin(L"--gui");
        if (!ok) {
            g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
        }
        return ok;
    };

    callbacks.on_ToggleMemCompression = [&]() -> std::string {
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target = !cur.memory_compression_enabled;
        bool ok = gfx.KernelScheduler().SetMemoryCompressionEnabled(target, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "Compresión de Memoria Habilitada" : "Compresión de Memoria Deshabilitada") : "Fallo al cambiar compresión (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste Windows MMAgent: " + msg);
        return msg;
    };

    callbacks.on_ToggleShaderCache = [&]() -> std::string {
        if (!gfx.AmdDriver()) {
            std::string msg = "Ajuste de ShaderCache AMD no disponible en esta GPU";
            last_gui_status = msg;
            return msg;
        }
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target = !cur.amd_shader_cache_forced;
        bool ok = gfx.AmdDriver()->SetShaderCacheForced(target, policy, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "ShaderCache AMD 0x3200 forzada (SurfaceFormat + FlipQueue=1)" : "AMD ShaderCache restaurada al valor del driver") : "Fallo al cambiar ShaderCache (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste AMD ShaderCache: " + msg);
        return msg;
    };

    callbacks.on_ToggleVramOverride = [&]() -> std::string {
        if (!gfx.AmdDriver()) {
            std::string msg = "Ajuste VRAM Reporting AMD no disponible en esta GPU";
            last_gui_status = msg;
            return msg;
        }
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target = !cur.amd_vram_override_active;
        bool ok = target ? gfx.AmdDriver()->ApplyVramReportingOverride(pol.safe_vram_pool_mb, false)
                         : gfx.AmdDriver()->RestoreVramReporting(false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? ("Sobrescritura VRAM UMA (" + std::to_string(pol.safe_vram_pool_mb) + " MB DXGI/AMD) Activada (Pool Size = " + std::to_string(pol.safe_vram_pool_mb) + " MB)")
                                       : "Reporte de VRAM restaurado a valores por defecto (512 MB)")
                             : "Fallo al modificar reporte de VRAM (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste VRAM Reporting: " + msg);
        return msg;
    };

    callbacks.on_ToggleMmcssGaming = [&]() -> std::string {
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target = !cur.mmcss_gaming_optimized;
        bool ok = gfx.Mmcss().SetGamingProfileOptimized(target, false);
        if (ok) {
            gfx.KernelScheduler().SetDynamicPrioritySeparation(target ? 0x26 : 0x02, false);
        }
        tweaks_query_countdown = 0;
        std::string msg = ok
            ? (target ? "Planificador MMCSS Gaming + Quantum CPU (0x26) Activado" : "Planificador MMCSS y Quantum CPU restaurados a Stock Windows")
            : "Fallo al optimizar MMCSS/Quantum (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste Kernel MMCSS/Quantum: " + msg);
        return msg;
    };

    callbacks.on_ToggleApuPowerMode = [&]() -> std::string {
        if (!gfx.PowerMode()) {
            std::string msg = "Modo de energía APU no disponible para la política actual";
            last_gui_status = msg;
            return msg;
        }
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        auto cur = gfx.QuerySystemGraphicsTweaks();
        bool target_prio = !cur.apu_power_optimized;
        bool ok = gfx.PowerMode()->SetApuPowerMode(
            target_prio ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced,
            policy,
            false
        );
        tweaks_query_countdown = 0;
        std::string ceiling_str = (pol.cpu_stapm_cap_mhz == 0)
            ? "Control Térmico APU: perfil sin tope activado (Efficient-Aggressive + EPP 25)"
            : ("Control Térmico APU: Modo GPU-Priority Activado (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz de techo)");
        std::string msg = ok
            ? (target_prio
                ? ceiling_str
                : "Control Térmico APU: Perfil Balanceado Estándar Restaurado")
            : "Fallo al alternar modo de energía APU";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_ApplyAllGamingTweaks = [&]() -> std::string {
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        bool ok_mmcss = gfx.Mmcss().ApplyTaskTweaks(false);
        bool ok_ram = MemoryManager::PurgeStandbyList(false);
        bool ok_vram = (pol.apply_uma_vram_reg_override && gfx.AmdDriver())
            ? gfx.AmdDriver()->ApplyVramReportingOverride(pol.safe_vram_pool_mb, false)
            : false;
        tweaks_query_countdown = 0;

        (void)ok_mmcss; (void)ok_ram; (void)ok_vram;

        std::string msg = "Combo Gaming aplicado: MMCSS, Purga RAM" +
            std::string(ok_vram ? (", VRAM " + std::to_string(pol.safe_vram_pool_mb) + "MB.") : ".");
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

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
        gfx.Display().SetMpoDisabled(false, false);
        if (gfx.AmdDriver()) {
            gfx.AmdDriver()->SetShaderCacheForced(false, policy, false);
            gfx.AmdDriver()->SetTessellationCap(64, false);
            gfx.AmdDriver()->RestoreVramReporting(false);
        }
        gfx.KernelScheduler().SetMemoryCompressionEnabled(true, false);
        gfx.Mmcss().SetGamingProfileOptimized(false, false);
        gfx.Display().SetGameDvrAndFseOptimized(false, false);
        if (gfx.PowerMode()) {
            gfx.PowerMode()->SetApuLaptopPowerProfile(false, policy, false);
        }
        gfx.KernelScheduler().SetDynamicPrioritySeparation(0x02, false);
        gfx.KernelScheduler().SetKernelMemoryPagingShield(false, false);
        size_t reg_rev = 0, reg_tot = 0;
        gfx.RestoreAllRegistryBackups(&reg_rev, &reg_tot);
        tweaks_query_countdown = 0;
        cpu_orchestrator.RestoreAll(false);
        last_isolated_count = 0;
        safe_mode = false;

        std::string msg;
        if (rep.total_count > 0) {
            msg = rep.summary_message;
        } else if (reg_tot > 0) {
            msg = "Se devolvieron " + std::to_string(reg_rev) + " de " + std::to_string(reg_tot) + ".";
        } else {
            msg = "Se devolvieron 0 de 0 (ajustes en valores originales de Windows).";
        }
        SaveSettings(msg);
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RescanSteamLibrary = [&]() -> std::string {
        cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
        SandboxGuard::ReloadSteamLibraryFolders(); // a library added in Steam becomes writable without a restart
        SyncSteamGamesToCustomExes(cached_steam_games, config);
        std::string msg = "Biblioteca Steam re-escaneada: " + std::to_string(cached_steam_games.size()) + " juegos detectados";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_ApplyGameEnginePreset = [&](uint32_t app_id) -> std::string {
        const ApuOptimizationPolicy static_pol = HardwareProfiler::BuildOptimizationPolicy(hw);
        const uint32_t pool_mb = static_pol.safe_vram_pool_mb;
        for (auto& sg : cached_steam_games) {
            if (sg.app_id == app_id) {
                auto rep = steam.ApplyEngineUmaPreset(sg, static_pol, pool_mb, false);
                if (rep.success) {
                    sg.engine_preset_applied = true;
                    sg.flip_model_applied = true;
                }
                last_gui_status = rep.status_message;
                AddAuditLog(rep.status_message);
                return rep.status_message;
            }
        }
        std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RestoreGameEngineConfig = [&](uint32_t app_id) -> std::string {
        for (auto& sg : cached_steam_games) {
            if (sg.app_id == app_id) {
                auto rep = steam.RestoreEngineOriginalConfig(sg, false);
                if (rep.success) {
                    sg.engine_preset_applied = false;
                }
                last_gui_status = rep.status_message;
                AddAuditLog(rep.status_message);
                return rep.status_message;
            }
        }
        std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_ToggleGameDxvk = [&](uint32_t app_id) -> std::string {
        const ApuOptimizationPolicy static_pol = HardwareProfiler::BuildOptimizationPolicy(hw);
        for (auto& sg : cached_steam_games) {
            if (sg.app_id == app_id) {
                std::string msg;
                if (sg.dxvk_installed) {
                    bool ok = dxvk.RemoveDxvk(sg.primary_exe_path, false, &msg);
                    if (ok) {
                        sg.dxvk_installed = false;
                    }
                } else {
                    bool ok = dxvk.DeployDxvk(sg.primary_exe_path, static_pol, sg.has_anticheat, true, false, &msg);
                    if (ok) {
                        sg.dxvk_installed = true;
                    }
                }
                last_gui_status = msg;
                AddAuditLog(msg);
                return msg;
            }
        }
        std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

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
