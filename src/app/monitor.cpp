#include "app/monitor.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
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
#include "optimize/fsr/fsr_scaler.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#include "detect/system_info.hpp"
#include <filesystem>
#include <tlhelp32.h>
#include "app/app_state.hpp"
#include "app/run_cycle.hpp"
#include "app/text_convert.hpp"

namespace vegaflow::app {

void RunMonitorDashboard(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    vegaflow::FsrSpatialScaler& fsr_scaler,
    bool once_mode) {

    using namespace vegaflow;
    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool timer_boosted = false;

    std::cout << "====================================================================================================\n";
    std::cout << "                               VEGAFLOW LIVE TELEMETRY DASHBOARD\n";
    std::cout << "====================================================================================================\n";
    std::cout << "[RAM Libre | Standby List | VRAM Local | Timer | Juego Activo + API | Avg FPS | 1% Low | 0.1% Low | StdDev ms]\n";
    std::cout << "----------------------------------------------------------------------------------------------------\n";

    auto print_line = [&]() {
        RunSingleIteration(
            config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);

        auto mem = mem_mgr.SampleTelemetry();
        auto timer = GraphicsOptimizer::QueryTimerResolution();
        auto snap = telemetry_monitor.GetSnapshot();

        uint64_t free_mb = mem.free_and_zero_bytes / (1024 * 1024);
        uint64_t standby_mb = mem.standby_bytes / (1024 * 1024);
        uint64_t vram_used_mb = mem.vram.local_usage_bytes / (1024 * 1024);
        uint64_t vram_budget_mb = mem.vram.local_budget_bytes / (1024 * 1024);
        double timer_ms = static_cast<double>(timer.current_ns100) / 10000.0;

        std::string game_info = game_active ? (WideToUtf8(active_game_exe) + " (" + active_game_api + ")") : "Ninguno";

        std::ostringstream ss;
        ss << "["
           << "RAM Lib: " << free_mb << " MB | "
           << "Standby: " << standby_mb << " MB | "
           << "VRAM: " << vram_used_mb << "/" << vram_budget_mb << " MB | "
           << "Timer: " << std::fixed << std::setprecision(1) << timer_ms << "ms | "
           << "Juego: " << game_info << " | "
           << "Avg: " << std::setprecision(1) << snap.avg_fps << " FPS | "
           << "1% Low: " << std::setprecision(1) << snap.fps_1_percent_low << " | "
           << "0.1% Low: " << std::setprecision(1) << snap.fps_0_1_percent_low << " | "
           << "StdDev: " << std::setprecision(2) << snap.frametime_stddev_ms << "ms"
           << "]";
        std::cout << ss.str() << "\n";
    };

    if (once_mode) {
        print_line();
        if (game_active) {
            fsr_scaler.StopScaling();
            telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
            if (timer_boosted) GraphicsOptimizer::RestoreDefaultTimer();
            telemetry_monitor.StopMonitoring();
        }
        return;
    }

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    while (g_running.load()) {
        print_line();
        DWORD wr = WaitForSingleObject(g_stop_event, config.poll_interval_ms);
        if (wr == WAIT_OBJECT_0 || !g_running.load()) {
            break;
        }
    }

    if (game_active) {
        fsr_scaler.StopScaling();
        telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
        cpu_orchestrator.RestoreAll(false);
        if (timer_boosted) GraphicsOptimizer::RestoreDefaultTimer();
        telemetry_monitor.StopMonitoring();
    }
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
}

} // namespace vegaflow::app
