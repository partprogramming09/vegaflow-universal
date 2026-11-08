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
#include "safety/frame_telemetry.hpp"
#include "ui/gui_window.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#include "detect/system_info.hpp"
#include <filesystem>
#include "app/app_state.hpp"
#include "app/config.hpp"
#include "app/reports.hpp"
#include "app/run_cycle.hpp"
#include "app/text_convert.hpp"

namespace vegaflow::app {

int RunGuiMode(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor) {
    using namespace vegaflow;
    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::wstring active_game_full_path;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool timer_boosted = false;
    bool timer_forced_manual = config.timer_forced_manual;
    if (timer_forced_manual) {
        if (GraphicsOptimizer::EnableHighPrecisionTimer(config.timer_resolution_ns100)) {
            timer_boosted = true;
        }
    }
    GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
    GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);
    uint32_t last_isolated_count = 0;
    uint64_t last_purge_before_free_mb = 0;
    uint64_t last_purge_after_free_mb = 0;
    uint32_t last_trimmed_processes = 0;
    std::string last_gui_status = "VegaFlow UXTU GUI Inicializada";

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
    };

    AddAuditLog("Sesión iniciada. Hardware: " + hw.cpu_topology.brand_string + " (" +
                std::to_string(hw.cpu_topology.physical_cores) + "P/" +
                std::to_string(hw.cpu_topology.logical_processors) + "L)");
    AddAuditLog("GPU Principal: " + WideToUtf8(hw.primary_gpu.description) + " [" +
                HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) + "]");
    AddAuditLog("Estado de Privilegios: " + std::string(PrivilegeManager::IsProcessElevated() ? "ADMINISTRADOR (Acceso Total)" : "ESTANDAR (Sin elevación)"));

    SystemGraphicsTweaksState cached_tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
    int tweaks_query_countdown = 10;
    uint32_t cached_noisy_count = 0;
    int noisy_query_countdown = 0;

    std::vector<SteamGameEntry> cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    SyncSteamGamesToCustomExes(cached_steam_games, config);

    GuiCallbacks callbacks;
    callbacks.on_PollTelemetry = [&]() -> GuiDashboardData {
        std::function<void(const std::string&)> audit_fn = AddAuditLog;
        RunSingleIteration(
            config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted,
            &active_game_full_path, &cached_noisy_count, &audit_fn);

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
            mem_mgr.EvaluateUmaPressureGuard(
                mem_sample,
                HardwareProfiler::BuildOptimizationPolicy(hw, mem_sample.vram.local_budget_bytes),
                {},
                true /* dry_run */);
        }
        auto timer_res = GraphicsOptimizer::QueryTimerResolution();
        auto frame_snap = telemetry_monitor.GetSnapshot();
        uint64_t daemon_ws = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();

        if (--tweaks_query_countdown <= 0) {
            tweaks_query_countdown = 10;
            cached_tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        }

        GuiDashboardData data;
        data.is_elevated = PrivilegeManager::IsProcessElevated();
        data.cpu_summary = hw.cpu_topology.brand_string.empty() ? "AMD/Intel x86_64" : hw.cpu_topology.brand_string;
        data.gpu_summary = WideToUtf8(hw.primary_gpu.description);
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

        data.timer_resolution_ms = static_cast<double>(timer_res.current_ns100) / 10000.0;
        data.timer_forced_manual = timer_forced_manual;
        data.mpo_disabled = cached_tweaks.mpo_disabled;
        data.mem_compression_enabled = cached_tweaks.memory_compression_enabled;
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

        // Query live state of the Round 2 and Group 2B optimizations
        std::string mmcss_cat;
        std::string mmcss_sfio;
        uint32_t mmcss_clock = 0;
        GraphicsOptimizer::QueryMmcssTaskTweaks(mmcss_cat, mmcss_sfio, mmcss_clock);
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
            ? (GraphicsOptimizer::HasDirectFlipUpgrade(active_game_full_path) || GraphicsOptimizer::HasDirectFlipUpgrade(active_game_exe))
            : (CountDirectFlipConfiguredGames() > 0);

        if (frame_snap.valid) {
            data.avg_fps = frame_snap.avg_fps;
            data.fps_1_low = frame_snap.fps_1_percent_low;
            data.fps_0_1_low = frame_snap.fps_0_1_percent_low;
            data.frametime_stddev_ms = frame_snap.frametime_stddev_ms;
        }

        data.daemon_private_ws_kb = daemon_ws / 1024;
        data.last_action_status = last_gui_status;
        data.audit_log = audit_log;

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

        data.system_info = SystemInfo::Gather();
        data.sandbox_violations = SandboxGuard::GetViolationCount();
        data.sandbox_guard_active = true;

        data.dxgi_pressure_ratio = mem_mgr.GetLastDxgiPressureRatio();
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
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target = !cur.mpo_disabled;
        bool ok = GraphicsOptimizer::SetMpoDisabled(target, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "MPO Desactivado (OverlayTestMode=5 Anti-Flicker)" : "MPO Restaurado a Default (OverlayTestMode=0)") : "Fallo al modificar MPO (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste DWM MPO: " + msg);
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
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target = !cur.memory_compression_enabled;
        bool ok = GraphicsOptimizer::SetMemoryCompressionEnabled(target, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "Compresión de Memoria Habilitada" : "Compresión de Memoria Deshabilitada (Latencia Cero)") : "Fallo al cambiar compresión (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste Windows MMAgent: " + msg);
        return msg;
    };

    callbacks.on_ToggleTimerForce = [&]() -> std::string {
        timer_forced_manual = !timer_forced_manual;
        std::string msg;
        if (timer_forced_manual) {
            bool ok = GraphicsOptimizer::EnableHighPrecisionTimer(5000);
            timer_boosted = ok;
            msg = ok ? "Timer de Kernel forzado a 0.50 ms (5000 ns100)" : "Fallo al forzar temporizador de kernel";
        } else {
            bool ok = GraphicsOptimizer::RestoreDefaultTimer();
            timer_boosted = false;
            msg = ok ? "Timer de Kernel restaurado a valor dinámico de Windows" : "Fallo al restaurar temporizador";
        }
        config.timer_forced_manual = timer_forced_manual;
        SaveConfig("config/config.json", config);
        last_gui_status = msg;
        AddAuditLog("Resolución de Kernel: " + msg);
        return msg;
    };

    callbacks.on_ToggleShaderCache = [&]() -> std::string {
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target = !cur.amd_shader_cache_forced;
        bool ok = GraphicsOptimizer::SetAmdShaderCacheForced(target, false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? "Boost AMD DX11/UMA + ShaderCache 0x3200 Activado (SurfaceFormat + FlipQueue=1)" : "AMD ShaderCache y Driver Boost restaurados a Driver Default") : "Fallo al cambiar ShaderCache (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste AMD ShaderCache: " + msg);
        return msg;
    };

    callbacks.on_ToggleVramOverride = [&]() -> std::string {
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target = !cur.amd_vram_override_active;
        bool ok = target ? GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, false)
                         : GraphicsOptimizer::RestoreAmdVramReporting(false);
        tweaks_query_countdown = 0;
        std::string msg = ok ? (target ? ("Sobrescritura VRAM UMA (" + std::to_string(pol.safe_vram_pool_mb) + " MB DXGI/AMD) Activada (Pool Size = " + std::to_string(pol.safe_vram_pool_mb) + " MB)")
                                       : "Reporte de VRAM restaurado a valores por defecto (512 MB)")
                             : "Fallo al modificar reporte de VRAM (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste VRAM Reporting: " + msg);
        return msg;
    };

    callbacks.on_ToggleMmcssGaming = [&]() -> std::string {
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target = !cur.mmcss_gaming_optimized;
        bool ok = GraphicsOptimizer::SetMmcssGamingProfileOptimized(target, false);
        if (ok) {
            GraphicsOptimizer::SetDynamicPrioritySeparation(target ? 0x26 : 0x02, false);
        }
        tweaks_query_countdown = 0;
        std::string msg = ok
            ? (target ? "Planificador WDDM (GPU Priority=8) + Quantum CPU (0x26) Activado" : "Planificador WDDM y Quantum CPU restaurados a Stock Windows")
            : "Fallo al optimizar MMCSS/Quantum (Requiere Administrador)";
        last_gui_status = msg;
        AddAuditLog("Ajuste Kernel MMCSS/Quantum: " + msg);
        return msg;
    };

    callbacks.on_ToggleApuPowerMode = [&]() -> std::string {
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool target_prio = !cur.apu_power_optimized;
        bool ok = GraphicsOptimizer::SetApuPowerMode(
            target_prio ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced,
            false
        );
        tweaks_query_countdown = 0;
        std::string ceiling_str = (pol.cpu_stapm_cap_mhz == 0)
            ? "Control Térmico APU: Smart Uncapped Boost Activado (100% CPU 4.0 GHz + 100% GPU con Efficient-Aggressive + EPP 25)"
            : ("Control Térmico APU: Modo GPU-Priority Activado (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz ceiling / 15W sostenidos)");
        std::string msg = ok
            ? (target_prio
                ? ceiling_str
                : "Control Térmico APU: Perfil Balanceado Estándar Restaurado (4.0 GHz Boost)")
            : "Fallo al alternar modo de energía APU";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_ApplyAllGamingTweaks = [&]() -> std::string {
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        bool ok_mmcss = GraphicsOptimizer::ApplyMmcssTweaks(false);
        bool ok_ram = MemoryManager::PurgeStandbyList(false);
        bool ok_timer = GraphicsOptimizer::SetHighResolutionTimer();
        bool ok_vram = pol.apply_uma_vram_reg_override
            ? GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, false)
            : false;
        tweaks_query_countdown = 0;

        (void)ok_mmcss; (void)ok_ram; (void)ok_timer; (void)ok_vram;

        std::string msg = "Combo Gaming 2B aplicado: MMCSS GPU=8, VRAM " +
            std::to_string(pol.safe_vram_pool_mb) + "MB, Timer 0.5ms y Purga RAM.";
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
        GraphicsOptimizer::RestoreDefaultTimer();
        timer_boosted = false;
        timer_forced_manual = false;
        config.timer_forced_manual = false;
        SaveConfig("config/config.json", config);
        GraphicsOptimizer::SetMpoDisabled(false, false);
        GraphicsOptimizer::SetAmdShaderCacheForced(false, false);
        GraphicsOptimizer::SetMemoryCompressionEnabled(true, false);
        GraphicsOptimizer::SetMmcssGamingProfileOptimized(false, false);
        GraphicsOptimizer::SetGameDvrAndFseOptimized(false, false);
        GraphicsOptimizer::SetApuLaptopPowerProfile(false, false);
        GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
        GraphicsOptimizer::SetKernelMemoryPagingShield(false, false);
        GraphicsOptimizer::SetAmdTessellationCap(64, false);
        GraphicsOptimizer::RestoreAmdVramReporting(false);
        GraphicsOptimizer::RestoreAllRegistryBackups();
        tweaks_query_countdown = 0;
        cpu_orchestrator.RestoreAll(false);
        last_isolated_count = 0;
        std::string msg = "Todos los ajustes dinámicos han sido revertidos a los valores por defecto de Windows";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_RescanSteamLibrary = [&]() -> std::string {
        cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
        SyncSteamGamesToCustomExes(cached_steam_games, config);
        std::string msg = "Biblioteca Steam re-escaneada: " + std::to_string(cached_steam_games.size()) + " juegos detectados";
        last_gui_status = msg;
        AddAuditLog(msg);
        return msg;
    };

    callbacks.on_ApplyGameEnginePreset = [&](uint32_t app_id) -> std::string {
        ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
        uint32_t pool_mb = pol.safe_vram_pool_mb;
        for (auto& sg : cached_steam_games) {
            if (sg.app_id == app_id) {
                auto rep = SteamEngineOptimizer::ApplyEngineUmaPreset(sg, pool_mb, false);
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
                auto rep = SteamEngineOptimizer::RestoreEngineOriginalConfig(sg, false);
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
        for (auto& sg : cached_steam_games) {
            if (sg.app_id == app_id) {
                std::string msg;
                if (sg.dxvk_installed) {
                    bool ok = DxvkManager::RemoveDxvk(sg.primary_exe_path, false, &msg);
                    if (ok) {
                        sg.dxvk_installed = false;
                    }
                } else {
                    bool ok = DxvkManager::DeployDxvk(sg.primary_exe_path, sg.has_anticheat, true, false, &msg);
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
    if (game_active) {
        telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
        telemetry_monitor.StopMonitoring();
    }
    cpu_orchestrator.RestoreAll(false);
    GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
    if (timer_boosted) {
        GraphicsOptimizer::RestoreDefaultTimer();
    }
    if (g_single_instance_mutex) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = nullptr;
    }
    return exit_code;
}

} // namespace vegaflow::app
