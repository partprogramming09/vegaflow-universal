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
#include "safety/frame_telemetry.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "app/app_state.hpp"
#include "app/config.hpp"
#include "app/reports.hpp"
#include "app/run_cycle.hpp"
#include "app/text_convert.hpp"

namespace vegaflow::app {

int RunDaemon(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    const vegaflow::PrivilegeStatus& priv,
    bool once_mode) {
    using namespace vegaflow;

    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool timer_boosted = false;

    GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
    GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);

    if (once_mode) {
        RunSingleIteration(
            config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);
        if (game_active) {
            telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
            if (timer_boosted) {
                GraphicsOptimizer::RestoreDefaultTimer();
            }
            telemetry_monitor.StopMonitoring();
        }
        return 0;
    }

    // Continuous Daemon Mode (Zero aggressive polling via Waitable Timer)
    auto steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    SyncSteamGamesToCustomExes(steam_games, config);

    // Named so an elevated script can request a clean shutdown (RestoreAll) without a console.
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, L"Global\\VegaFlowStop");
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "[WARN] Failed to set console control handler.\n";
    }

    HANDLE waitable_timer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
        TIMER_ALL_ACCESS);
    if (!waitable_timer) {
        waitable_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    if (!waitable_timer) {
        std::cerr << "[ERROR] Failed to create Windows Waitable Timer (Error: " << GetLastError() << ")\n";
        return 1;
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = -10000LL * config.poll_interval_ms; // 100ns units, negative for relative
    if (!SetWaitableTimer(waitable_timer, &due_time, config.poll_interval_ms, nullptr, nullptr, FALSE)) {
        std::cerr << "[ERROR] SetWaitableTimer failed (Error: " << GetLastError() << ")\n";
        CloseHandle(waitable_timer);
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "            VEGAFLOW DAEMON INITIALIZED (ZERO-OVERHEAD ACTIVE)                  \n";
    std::cout << "  Poll Interval : " << config.poll_interval_ms << " ms (Waitable Timer)\n";
    std::cout << "  Timer Target  : " << (config.timer_resolution_ns100 / 10000.0) << " ms\n";
    std::cout << "  Architecture  : " << HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) << "\n";
    if (!priv.is_elevated) {
        std::cout << "  [WARN] Running without Administrator privileges: Standby List purge requires Admin.\n";
        std::cout << "         Run with --elevate to grant full access.\n";
    }
    DWORD session_id = 0;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) && session_id == 0) {
        std::cout << "  [WARN] Running in Windows session 0 (no desktop): game detection will not see any window.\n";
    }
    std::cout << "  Press Ctrl+C to terminate cleanly.\n";
    std::cout << "================================================================================\n";

    HANDLE wait_handles[2] = { g_stop_event, waitable_timer };

    while (g_running.load()) {
        DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            RunSingleIteration(
                config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor,
                game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);
        } else {
            break;
        }
    }

    std::cout << "\n[INFO] Shutdown signal received. Cleaning up resources...\n";
    if (game_active) {
        telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
        cpu_orchestrator.RestoreAll(false);
        GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
        if (timer_boosted) {
            GraphicsOptimizer::RestoreDefaultTimer();
        }
        telemetry_monitor.StopMonitoring();
    }
    GraphicsOptimizer::SetApuLaptopPowerProfile(false, false);
    // Restores the registry (GameDVR, AMD driver, MPO, MMCSS, priority, paging) to its state before VegaFlow.
    const bool registry_restored = GraphicsOptimizer::RestoreAllRegistryBackups();
    std::cout << "[INFO] Registry values restored to their original state: " << (registry_restored ? "yes" : "partially or not (see --restore-registry)") << "\n";

    CancelWaitableTimer(waitable_timer);
    CloseHandle(waitable_timer);
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
