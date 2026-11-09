#include "app/cli.hpp"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "session/system_info.hpp"
#include "app/app_state.hpp"
#include "app/config.hpp"
#include "app/reports.hpp"
#include "app/run_cycle.hpp"
#include "app/text_convert.hpp"

namespace vegaflow::app {

std::optional<int> ParseCommandLine(int argc, char* argv[], CliOptions& opts) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--status" || arg == "-s") {
            opts.status_flag = true;
        } else if (arg == "--dry-run" || arg == "-d") {
            opts.dry_run_flag = true;
        } else if (arg == "--once" || arg == "-1") {
            opts.once_flag = true;
        } else if (arg == "--elevate" || arg == "-e") {
            opts.elevate_flag = true;
        } else if (arg == "--purge-now" || arg == "-p") {
            opts.purge_now_flag = true;
        } else if (arg == "--mpo-disable") {
            opts.mpo_disable_flag = true;
        } else if (arg == "--mpo-restore") {
            opts.mpo_restore_flag = true;
        } else if (arg == "--shader-cache-force") {
            opts.shader_cache_force_flag = true;
        } else if (arg == "--shader-cache-default") {
            opts.shader_cache_default_flag = true;
        } else if (arg == "--mem-compression-off") {
            opts.mem_compression_off_flag = true;
        } else if (arg == "--mem-compression-on") {
            opts.mem_compression_on_flag = true;
        } else if (arg == "--apu-gpu-priority") {
            opts.apu_gpu_priority_flag = true;
        } else if (arg == "--apu-balanced") {
            opts.apu_balanced_flag = true;
        } else if (arg == "--vram-override") {
            opts.vram_override_flag = true;
        } else if (arg == "--vram-restore") {
            opts.vram_restore_flag = true;
        } else if (arg == "--restore-registry") {
            opts.restore_registry_flag = true;
        } else if (arg == "--dxvk-install" && i + 1 < argc) {
            opts.dxvk_install_path = argv[++i];
        } else if (arg == "--dxvk-remove" && i + 1 < argc) {
            opts.dxvk_remove_path = argv[++i];
        } else if (arg == "--monitor" || arg == "-m") {
            opts.monitor_flag = true;
        } else if (arg == "--gui" || arg == "-g") {
            opts.gui_flag = true;
        } else if (arg == "--system-info") {
            opts.system_info_flag = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "VegaFlow Daemon - Dynamic Hardware & OS Performance Orchestrator\n";
            std::cout << "Usage: vegaflow_daemon [OPTIONS]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --status, -s             Display detailed hardware, RAM, timer, MPO, and system telemetry report.\n";
            std::cout << "  --dry-run, -d            Simulate evaluation cycles without modifying processes or registry.\n";
            std::cout << "  --once, -1               Execute one optimization evaluation cycle and exit cleanly.\n";
            std::cout << "  --elevate, -e            Relaunch current executable with Administrator privileges (UAC).\n";
            std::cout << "  --purge-now, -p          Force immediate trimming of background working sets and Standby List purge.\n";
            std::cout << "  --mpo-disable            Disable Multi-Plane Overlay (MPO) in Windows DWM to eliminate stutter/flicker.\n";
            std::cout << "  --mpo-restore            Restore Multi-Plane Overlay (MPO) in Windows DWM to system default.\n";
            std::cout << "  --shader-cache-force     Force AMD driver shader cache to always compile to disk (0x3200).\n";
            std::cout << "  --shader-cache-default   Restore AMD driver shader cache to default setting.\n";
            std::cout << "  --mem-compression-off    Disable Windows kernel Memory Compression to eliminate latency/hiccups.\n";
            std::cout << "  --mem-compression-on     Enable Windows kernel Memory Compression.\n";
            std::cout << "  --apu-gpu-priority       Activate APU thermal & power profile with sustained GPU priority (3.4 GHz CPU cap).\n";
            std::cout << "  --apu-balanced           Restore standard Windows balanced APU power profile.\n";
            std::cout << "  --vram-override          Override AMD VRAM reporting to 4096 MB in DXGI and AMD driver.\n";
            std::cout << "  --vram-restore           Restore AMD VRAM reporting to driver default (512 MB).\n";
            std::cout << "  --restore-registry       Restore the GameDVR and AMD driver registry values saved before VegaFlow changed them.\n";
            std::cout << "  --dxvk-install <path>    Deploy DXVK (DirectX 11 to Vulkan with async GPL) to target game folder.\n";
            std::cout << "  --dxvk-remove <path>     Remove DXVK and restore original DirectX 11 runtime.\n";
            std::cout << "  --monitor, -m            Real-time console dashboard displaying live RAM, VRAM, and FPS telemetry.\n";
            std::cout << "  --gui, -g                Launch native zero-overhead Win32/GDI dashboard with system tray support.\n";
            std::cout << "  --system-info            Output complete hardware, APU UMA, RAM topology, and OS specs in JSON format.\n";
            std::cout << "  --help, -h               Display this help menu.\n";
            return 0;
        }
    }

    return std::nullopt;
}

std::optional<int> RunOneShotCommand(const CliOptions& opts, int argc, char* argv[]) {
    if (opts.system_info_flag) {
        vegaflow::SystemInfo sys_info;
        std::cout << sys_info.ToJson() << "\n";
        return 0;
    }

    // If not running as Administrator and not a simulated run (--dry-run),
    // relaunch immediately with elevated privileges via UAC (runas) to open natively as Administrator.
    if (!opts.dry_run_flag && !opts.status_flag && !vegaflow::PrivilegeManager::IsProcessElevated()) {
        std::wstring forward_args;
        for (int j = 1; j < argc; ++j) {
            std::string a = argv[j];
            if (a != "--elevate" && a != "-e") {
                if (!forward_args.empty()) forward_args += L" ";
                if (a.find(' ') != std::string::npos) {
                    forward_args += L"\"" + Utf8ToWide(a) + L"\"";
                } else {
                    forward_args += Utf8ToWide(a);
                }
            }
        }
        if (forward_args.empty()) {
            forward_args = L"--gui";
        }
        if (vegaflow::PrivilegeManager::RelaunchAsAdmin(forward_args, false)) {
            return 0;
        }
    }

    if (opts.elevate_flag) {
        if (!vegaflow::PrivilegeManager::IsProcessElevated()) {
            std::cout << "[INFO] Requesting UAC Administrator elevation for VegaFlow...\n";
            std::wstring forward_args;
            for (int j = 1; j < argc; ++j) {
                std::string a = argv[j];
                if (a != "--elevate" && a != "-e") {
                    if (!forward_args.empty()) forward_args += L" ";
                    forward_args += Utf8ToWide(a);
                }
            }
            bool ok = vegaflow::PrivilegeManager::RelaunchAsAdmin(forward_args, opts.dry_run_flag);
            if (ok) {
                std::cout << "[INFO] Elevated instance launched. Terminating standard process.\n";
                return 0;
            } else {
                std::cerr << "[ERROR] Elevation was cancelled by user or denied.\n";
                return 1;
            }
        } else {
            std::cout << "[INFO] Process is already running with Administrator privileges.\n";
            if (!opts.gui_flag && !opts.status_flag && !opts.purge_now_flag && !opts.monitor_flag && !opts.mpo_disable_flag && !opts.mpo_restore_flag
                && !opts.shader_cache_force_flag && !opts.shader_cache_default_flag && !opts.mem_compression_off_flag && !opts.mem_compression_on_flag
                && !opts.apu_gpu_priority_flag && !opts.apu_balanced_flag && !opts.vram_override_flag && !opts.vram_restore_flag && !opts.restore_registry_flag && !opts.once_flag
                && opts.dxvk_install_path.empty() && opts.dxvk_remove_path.empty()) {
                return 0;
            }
        }
    }

    if (opts.apu_gpu_priority_flag) {
        vegaflow::ApuOptimizationPolicy pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
        bool ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, opts.dry_run_flag);
        std::string mode_str = (pol.cpu_stapm_cap_mhz == 0)
            ? "Control Térmico APU: Smart Uncapped Boost Activado (100% CPU 4.0 GHz + 100% GPU con Efficient-Aggressive + EPP 25)"
            : ("GPU-Priority sustained mode active (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz CPU ceiling / 15W continuous)");
        std::cout << "[APU Power] SetApuPowerMode(GpuPrioritySustained) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. " + mode_str + ".") : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.apu_balanced_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, opts.dry_run_flag);
        std::cout << "[APU Power] SetApuPowerMode(Balanced) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows standard balanced power profile restored." : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.shader_cache_force_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(true, opts.dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache set to 0x3200 (Always Enabled on disk)." : "FAILED. Admin privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.shader_cache_default_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(false, opts.dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache restored to driver default." : "FAILED. Admin privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mem_compression_off_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(false, opts.dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression DISABLED (stutter-free latency mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mem_compression_on_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(true, opts.dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression restored." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.vram_override_flag) {
        vegaflow::ApuOptimizationPolicy pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
        bool ok = vegaflow::GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, opts.dry_run_flag);
        std::cout << "[AMD VRAM] ApplyAmdVramReportingOverride(" << pol.safe_vram_pool_mb << ") " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. VRAM reporting set to " + std::to_string(pol.safe_vram_pool_mb) + " MB in DXGI & AMD driver.") : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.vram_restore_flag) {
        bool ok = vegaflow::GraphicsOptimizer::RestoreAmdVramReporting(opts.dry_run_flag);
        std::cout << "[AMD VRAM] RestoreAmdVramReporting() " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. VRAM reporting restored to driver default (512 MB)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.restore_registry_flag) {
        bool ok = vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups();
        std::cout << "[Registry] RestoreAllRegistryBackups() "
                  << (ok ? "SUCCEEDED. GameDVR and AMD driver values are back to what they were before VegaFlow." : "FAILED for at least one value (administrator privileges may be required).") << "\n";
        return ok ? 0 : 1;
    }

    if (!opts.dxvk_install_path.empty()) {
        std::wstring wpath = Utf8ToWide(opts.dxvk_install_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager::DeployDxvk(wpath, false, true, opts.dry_run_flag, &msg);
        std::cout << "[DXVK] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (!opts.dxvk_remove_path.empty()) {
        std::wstring wpath = Utf8ToWide(opts.dxvk_remove_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager::RemoveDxvk(wpath, opts.dry_run_flag, &msg);
        std::cout << "[DXVK] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (opts.status_flag) {
        PrintStatusReport();
        return 0;
    }

    if (opts.purge_now_flag) {
        ExecutePurgeNow(opts.dry_run_flag);
        return 0;
    }

    if (opts.mpo_disable_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMpoDisabled(true, opts.dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay is now DISABLED (stutter-free mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mpo_restore_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMpoDisabled(false, opts.dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay restored to Windows default." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    return std::nullopt;
}

} // namespace vegaflow::app
