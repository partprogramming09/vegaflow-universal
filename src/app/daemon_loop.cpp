#include "app/daemon_loop.hpp"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <string>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "app/app_state.hpp"
#include "session/config.hpp"
#include "session/reports.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "windows/safe_file_system.hpp"
#include "session/run_cycle.hpp"
#include "windows/text_convert.hpp"
#include "windows/event_sources.hpp"
#include "session/wake_loop.hpp"

namespace vegaflow::app {

int RunDaemon(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    const vegaflow::PrivilegeStatus& priv,
    bool once_mode,
    bool safe_mode,
    const std::string& safe_reason) {
    using namespace vegaflow;
    // Built once from the boot profile, the same way each optimizer used to rebuild it on every call.
    ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw);
    SafeFileSystem file_system; // the real FileSystem port (SandboxGuard)
    WindowsRegistryPort reg;
    WindowsPowerPort power;
    GraphicsOptimizer gfx(hw, policy, reg, &power);

    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;

    if (safe_mode) {
        std::cout << "[WARN] MODO SEGURO ACTIVO (" << safe_reason << "): "
                  << "Arrancando sin aplicar ajustes del sistema. Usa --restore-registry si necesitas revertir.\n";
    } else {
        gfx.DxgiFlip().MigrateLegacyPreferences(false);
        // E2.2: opt-in in config.json.
        if (config.auto_game_dvr_off) gfx.Display().SetGameDvrCaptureDisabled(true, false);
        if (config.auto_power_profile && gfx.PowerMode()) {
            gfx.PowerMode()->SetApuLaptopPowerProfile(true, policy, false);
        }
    }

    if (once_mode) {
        if (!safe_mode) {
            RunSingleIteration(
                config, file_system, hw, cpu_orchestrator, mem_mgr,
                game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd);
            if (game_active) {
                SaveSessionReportToFile(WideToUtf8(active_game_exe), active_game_api, active_game_pid, "logs/session_reports.log");
                cpu_orchestrator.RestoreAll(false);
                gfx.KernelScheduler().SetDynamicPrioritySeparation(0x02, false);
            }
            if (gfx.PowerMode() && gfx.PowerMode()->IsApuPowerModeApplied()) {
                gfx.PowerMode()->SetApuLaptopPowerProfile(false, policy, false);
            }
            gfx.Display().RestoreGameDvrCapture(false);
            gfx.RestoreAllRegistryBackups();
        }
        return 0;
    }

    // Continuous Daemon Mode: wakes on events (E2.4); a timer is only a heartbeat
    auto steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    SyncSteamGamesToCustomExes(steam_games, config);

    // Named so an elevated script can request a clean shutdown (RestoreAll) without a console.
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, L"Global\\VegaFlowStop");
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "[WARN] Failed to set console control handler.\n";
    }

    // E2.4: created on this thread, which is the one that waits (the hook and the message window need its queue).
    WindowsEventSource event_source(g_stop_event);

    std::cout << "================================================================================\n";
    std::cout << "            VEGAFLOW DAEMON INITIALIZED                                    \n";
    if (event_source.EventsActive()) {
        std::cout << "  Wake-up       : events (idle safety net " << session::kIdleSafetyNetMs / 1000 << " s, "
                  << config.poll_interval_ms << " ms while a game runs)\n";
    } else {
        std::cout << "  Poll Interval : " << session::kPollFallbackMs << " ms (events unavailable)\n";
    }
    std::cout << "  Architecture  : " << HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) << "\n";
    if (!priv.is_elevated) {
        std::cout << "  [WARN] Running without Administrator privileges: Standby List purge requires Admin.\n";
        std::cout << "         Run with --elevate to grant full access.\n";
    }
    DWORD session_id = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id)) {
        if (const char* warn = session::NoDesktopWarning(session_id)) std::cout << "  " << warn << "\n";
    }
    std::cout << "  Press Ctrl+C to terminate cleanly.\n";
    std::cout << "================================================================================\n";

    session::WakeLoopHooks hooks;
    hooks.should_stop = [] { return !g_running.load(); };
    hooks.game_active = [&] { return game_active; };
    hooks.log = [](const std::string& m) { std::cerr << m << "\n"; };
    hooks.iterate = [&](const session::WakeSet& wake, bool check_adapters) {
        if (safe_mode) return;
        const uint64_t prev_sig = hw.adapter_signature;
        const uint8_t prev_pref = hw.user_gpu_pref;
        RunSingleIteration(
            config, file_system, hw, cpu_orchestrator, mem_mgr,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd,
            nullptr, nullptr, nullptr, check_adapters);
        // The iteration re-reads the hardware when the adapters change, and a power-source change recomputes the
        // static policy; the tweaks and telemetry follow it.
        if (wake.power || hw.adapter_signature != prev_sig || hw.user_gpu_pref != prev_pref) {
            policy = HardwareProfiler::BuildOptimizationPolicy(hw);
            gfx.Rebind(hw, policy);
            mem_mgr.SetTargetLuid(hw.primary_gpu.luid);
        }
    };
    hooks.cleanup = [&] {
    std::cout << "\n[INFO] Shutdown signal received. Cleaning up resources...\n";
    if (!safe_mode) {
        if (game_active) {
            SaveSessionReportToFile(WideToUtf8(active_game_exe), active_game_api, active_game_pid, "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
            gfx.KernelScheduler().SetDynamicPrioritySeparation(0x02, false);
        }
        if (gfx.PowerMode()) {
            gfx.PowerMode()->SetApuLaptopPowerProfile(false, policy, false);
        }
        gfx.Display().RestoreGameDvrCapture(false);
        // Restores the registry (GameDVR, AMD driver, MPO, MMCSS, priority, paging) to its state before VegaFlow.
        const bool registry_restored = gfx.RestoreAllRegistryBackups();
        std::cout << "[INFO] Registry values restored to their original state: " << (registry_restored ? "yes" : "partially or not (see --restore-registry)") << "\n";
    }
    };

    session::RunWakeLoop(event_source, SteadyClock::Instance(), config.poll_interval_ms, hooks);

    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
    if (g_single_instance_mutex) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = nullptr;
    }

    std::cout << "[INFO] VegaFlow Daemon terminated cleanly.\n";
    return 0;
}

} // namespace vegaflow::app
