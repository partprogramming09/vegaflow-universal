#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>

#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "safety/frame_telemetry.hpp"
#include "app/app_state.hpp"
#include "app/cli.hpp"
#include "session/config.hpp"
#include "app/daemon_loop.hpp"
#include "app/gui_mode.hpp"
#include "app/monitor.hpp"
#include "session/reports.hpp"
#include "windows/config_location.hpp"
#include "windows/safe_file_system.hpp"

using namespace vegaflow::app;

namespace {

LONG WINAPI VegaFlowCrashHandler(EXCEPTION_POINTERS* /*ep*/) {
    vegaflow::GraphicsOptimizer::RestoreDefaultTimer();
    return EXCEPTION_EXECUTE_HANDLER;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    SetUnhandledExceptionFilter(VegaFlowCrashHandler);
    SetConsoleOutputCP(CP_UTF8);

    CliOptions opts;
    if (const auto exit_code = ParseCommandLine(argc, argv, opts)) return *exit_code;
    if (const auto exit_code = RunOneShotCommand(opts, argc, argv)) return *exit_code;

    // One path, next to the executable; it is only read when no account without administrator rights could write it.
    vegaflow::SafeFileSystem config_fs;
    const std::wstring config_path = vegaflow::ConfigFilePath();
    ConfigStore config_store(config_fs, config_path, vegaflow::ConfigLocationTrusted(config_path));
    AppConfig config = config_store.Load();
    if (const std::string warning = config_store.Report().Warning(); !warning.empty()) {
        std::cerr << "[WARN] " << warning << "\n";
    }

    if (opts.dry_run_flag && !opts.monitor_flag) {
        ExecuteDryRun(config);
        return 0;
    }

    if (!opts.once_flag) {
        g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
        if (g_single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExisting = FindWindowW(L"VegaFlowUniversalUxtuClass", nullptr);
            if (hExisting) {
                PostMessageW(hExisting, WM_COMMAND, 2001 /* ID_TRAY_RESTORE */, 0);
                SetForegroundWindow(hExisting);
            }
            std::cout << "[INFO] VegaFlow Universal is already running. Focusing existing instance.\n";
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
            return 0;
        }
    }

    using namespace vegaflow;
    PrivilegeStatus priv = PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();
    ApuOptimizationPolicy boot_policy = HardwareProfiler::BuildOptimizationPolicy(hw);

    if (priv.is_elevated && boot_policy.apply_uma_vram_reg_override) {
        GraphicsOptimizer::ApplyAmdVramReportingOverride(boot_policy.safe_vram_pool_mb, opts.dry_run_flag);
    }

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    CpuOrchestrator cpu_orchestrator;
    FrameTelemetryMonitor telemetry_monitor;

    if (opts.monitor_flag) {
        RunMonitorDashboard(config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, opts.once_flag);
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        return 0;
    }

    if (opts.gui_flag) {
        return RunGuiMode(config, config_store, hw, cpu_orchestrator, mem_mgr, telemetry_monitor);
    }

    return RunDaemon(config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, priv, opts.once_flag);
}
