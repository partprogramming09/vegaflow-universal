#include "app/monitor.hpp"

#include <windows.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "app/app_state.hpp"
#include "windows/safe_file_system.hpp"
#include "session/reports.hpp"
#include "session/run_cycle.hpp"
#include "session/text_convert.hpp"

namespace vegaflow::app {

void RunMonitorDashboard(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    bool once_mode) {

    using namespace vegaflow;
    SafeFileSystem file_system; // the real FileSystem port (SandboxGuard)
    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;

    std::cout << "====================================================================================================\n";
    std::cout << "                               VEGAFLOW LIVE TELEMETRY DASHBOARD\n";
    std::cout << "====================================================================================================\n";
    std::cout << "[RAM Libre | Standby List | VRAM Local | Juego Activo + API]\n";
    std::cout << "----------------------------------------------------------------------------------------------------\n";

    auto print_line = [&]() {
        RunSingleIteration(
            config, file_system, hw, cpu_orchestrator, mem_mgr,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd);

        auto mem = mem_mgr.SampleTelemetry();

        uint64_t free_mb = mem.free_and_zero_bytes / (1024 * 1024);
        uint64_t standby_mb = mem.standby_bytes / (1024 * 1024);
        uint64_t vram_used_mb = mem.vram.local_usage_bytes / (1024 * 1024);
        uint64_t vram_budget_mb = mem.vram.local_budget_bytes / (1024 * 1024);

        std::string game_info = game_active ? (WideToUtf8(active_game_exe) + " (" + active_game_api + ")") : "Ninguno";

        std::ostringstream ss;
        ss << "["
           << "RAM Lib: " << free_mb << " MB | "
           << "Standby: " << standby_mb << " MB | "
           << "VRAM: " << vram_used_mb << "/" << vram_budget_mb << " MB | "
           << "Juego: " << game_info
           << "]";
        std::cout << ss.str() << "\n";
    };

    if (once_mode) {
        print_line();
        if (game_active) {
            SaveSessionReportToFile(WideToUtf8(active_game_exe), active_game_api, active_game_pid, "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
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
        SaveSessionReportToFile(WideToUtf8(active_game_exe), active_game_api, active_game_pid, "logs/session_reports.log");
        cpu_orchestrator.RestoreAll(false);
    }
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
}

} // namespace vegaflow::app
