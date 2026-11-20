#include "app/cli.hpp"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "windows/privileges.hpp"
#include "windows/change_journal.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "windows/safe_file_system.hpp"
#include "session/system_info.hpp"
#include "session/restore.hpp"
#include "app/app_state.hpp"
#include "session/config.hpp"
#include "session/reports.hpp"
#include "session/run_cycle.hpp"
#include "ui/app_identity.hpp"
#include "windows/text_convert.hpp"

namespace vegaflow::app {

std::optional<int> ParseCommandLine(int argc, char* argv[], CliOptions& opts) {
    // A path value must be there and must not be the next option (--dxvk-install --dry-run lost the simulation).
    auto has_value = [&](int i) { return i + 1 < argc && argv[i + 1][0] != '\0' && argv[i + 1][0] != '-'; };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--status" || arg == "-s") {
            opts.status_flag = true;
        } else if (arg == "--dry-run" || arg == "-d") {
            opts.dry_run_flag = true;
        } else if (arg == "--observe") {
            opts.observe_flag = true;
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
        } else if (arg == "--recover") {
            opts.recover_flag = true;
        } else if (arg == "--create-restore-point") {
            opts.create_restore_point_flag = true;
        } else if (arg == "--dxvk-install" && has_value(i)) {
            opts.dxvk_install_path = argv[++i];
        } else if (arg == "--dxvk-remove" && has_value(i)) {
            opts.dxvk_remove_path = argv[++i];
        } else if (arg == "--monitor" || arg == "-m") {
            opts.monitor_flag = true;
        } else if (arg == "--gui" || arg == "-g") {
            opts.gui_flag = true;
        } else if (arg == "--system-info") {
            opts.system_info_flag = true;
        } else if (arg == "--version") {
            std::cout << "VegaFlow Universal " << ui::kVersion << "\n";
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "VegaFlow Daemon - Dynamic Hardware & OS Performance Orchestrator\n";
            std::cout << "Usage: vegaflow_daemon [OPTIONS]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --status, -s             Display detailed hardware, RAM, MPO, and system telemetry report.\n";
            std::cout << "  --dry-run, -d            Simulate evaluation cycles without modifying processes or registry.\n";
            std::cout << "  --once, -1               Execute one optimization evaluation cycle and exit cleanly.\n";
            std::cout << "  --elevate, -e            Relaunch current executable with Administrator privileges (UAC).\n";
            std::cout << "  --purge-now, -p          Force immediate trimming of background working sets and Standby List purge.\n";
            std::cout << "  --mpo-disable            Disable Multi-Plane Overlay (MPO) in Windows DWM (OverlayTestMode=5, undocumented).\n";
            std::cout << "  --mpo-restore            Restore Multi-Plane Overlay (MPO) in Windows DWM to system default.\n";
            std::cout << "  --shader-cache-force     Force AMD driver shader cache to always compile to disk (0x3200).\n";
            std::cout << "  --shader-cache-default   Restore AMD driver shader cache to default setting.\n";
            std::cout << "  --mem-compression-off    Disable Windows kernel Memory Compression.\n";
            std::cout << "  --mem-compression-on     Enable Windows kernel Memory Compression.\n";
            std::cout << "  --apu-gpu-priority       Activate APU thermal & power profile with sustained GPU priority (CPU ceiling from the tier policy).\n";
            std::cout << "  --apu-balanced           Restore standard Windows balanced APU power profile.\n";
            std::cout << "  --vram-override          Override AMD VRAM reporting to 4096 MB in DXGI and AMD driver.\n";
            std::cout << "  --vram-restore           Put back the AMD VRAM reporting VegaFlow saved (nothing to put back: no change).\n";
            std::cout << "  --restore-registry       Restore all pending journal entries and VegaFlow registry backups.\n";
            std::cout << "  --recover                Alias for --restore-registry (recovers pending changes after crash).\n";
            std::cout << "  --create-restore-point   Attempt to create a Windows System Restore Point now.\n";
            std::cout << "  --dxvk-install <path>    Deploy DXVK (DirectX 11 to Vulkan with async GPL) to target game folder.\n";
            std::cout << "  --dxvk-remove <path>     Remove DXVK and restore original DirectX 11 runtime.\n";
            std::cout << "  --monitor, -m            Real-time console dashboard displaying live RAM and VRAM telemetry.\n";
            std::cout << "  --observe                With --gui: no automatic changes and no restore-point offer (safe mode on demand).\n";
            std::cout << "  --gui, -g                Launch native zero-overhead Win32/GDI dashboard with system tray support.\n";
            std::cout << "  --system-info            Output complete hardware, APU UMA, RAM topology, and OS specs in JSON format.\n";
            std::cout << "  --version                Print the version and exit.\n";
            std::cout << "  --help, -h               Display this help menu.\n";
            return 0;
        } else {
            // A mistyped option (--dryrun) must not fall through to the real daemon.
            std::cerr << "[ERROR] Unknown option or missing value: " << arg << " (see --help)\n";
            return 2;
        }
    }

    return std::nullopt;
}

std::optional<int> RunOneShotCommand(const CliOptions& opts, int argc, char* argv[]) {
    // One-shot commands run before the daemon detects the hardware, so the policy is built here when one needs it.
    auto detected_policy = [] {
        return vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
    };
    vegaflow::SafeFileSystem file_system;
    vegaflow::WindowsRegistryPort reg;
    vegaflow::WindowsPowerPort power;
    if (opts.system_info_flag) {
        vegaflow::SystemInfo sys_info;
        std::cout << sys_info.ToJson() << "\n";
        return 0;
    }

    // If not running as Administrator and not a simulated run (--dry-run),
    // relaunch immediately with elevated privileges via UAC (runas) to open natively as Administrator.
    // The flags handed to the elevated instance, without --elevate; a value with spaces keeps its quotes.
    auto forwarded_args = [&]() {
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
        return forward_args;
    };
    if (!opts.dry_run_flag && !opts.status_flag && !vegaflow::PrivilegeManager::IsProcessElevated()) {
        std::wstring forward_args = forwarded_args();
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
            bool ok = vegaflow::PrivilegeManager::RelaunchAsAdmin(forwarded_args(), opts.dry_run_flag);
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
                && !opts.apu_gpu_priority_flag && !opts.apu_balanced_flag && !opts.vram_override_flag && !opts.vram_restore_flag && !opts.restore_registry_flag
                && !opts.recover_flag && !opts.create_restore_point_flag && !opts.once_flag && !opts.dry_run_flag
                && opts.dxvk_install_path.empty() && opts.dxvk_remove_path.empty()) {
                return 0;
            }
        }
    }

    if (opts.create_restore_point_flag) {
        if (opts.dry_run_flag) {
            std::cout << "[RestorePoint] [DRY-RUN] Simulaci\u00f3n de creaci\u00f3n de punto de restauraci\u00f3n de Windows.\n";
            return 0;
        }
        vegaflow::WindowsSystemUndo undo(file_system);
        std::string msg;
        const bool ok = undo.CreateRestorePoint("VegaFlow Universal - Manual", &msg);
        std::cout << "[RestorePoint] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (opts.apu_gpu_priority_flag) {
        vegaflow::ApuOptimizationPolicy pol = detected_policy();
        bool ok = vegaflow::PowerModeTweaks(power).SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, pol, opts.dry_run_flag);
        std::string mode_str = (pol.cpu_stapm_cap_mhz == 0)
            ? "Control Térmico APU: perfil sin tope activado (Efficient-Aggressive + EPP 25)"
            : ("GPU-Priority sustained mode active (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz CPU ceiling)");
        std::cout << "[APU Power] SetApuPowerMode(GpuPrioritySustained) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. " + mode_str + ".") : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.apu_balanced_flag) {
        bool ok = vegaflow::PowerModeTweaks(power).SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, detected_policy(), opts.dry_run_flag);
        std::cout << "[APU Power] SetApuPowerMode(Balanced) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows standard balanced power profile restored." : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.shader_cache_force_flag) {
        const auto hw = vegaflow::HardwareProfiler::DetectHardware();
        const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);
        vegaflow::GraphicsOptimizer gfx(hw, pol, reg, &power);
        bool ok = gfx.AmdDriver() && gfx.AmdDriver()->SetShaderCacheForced(true, pol, opts.dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache set to 0x3200 (Always Enabled on disk)." : (gfx.AmdDriver() ? "FAILED. Admin privileges required." : "SKIPPED. GPU is not AMD or driver tweaks blocked.")) << "\n";
        return ok ? 0 : 1;
    }

    if (opts.shader_cache_default_flag) {
        const auto hw = vegaflow::HardwareProfiler::DetectHardware();
        const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);
        vegaflow::GraphicsOptimizer gfx(hw, pol, reg, &power);
        bool ok = gfx.AmdDriver() && gfx.AmdDriver()->SetShaderCacheForced(false, pol, opts.dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache restored to driver default." : (gfx.AmdDriver() ? "FAILED. Admin privileges required." : "SKIPPED. GPU is not AMD or driver tweaks blocked.")) << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mem_compression_off_flag) {
        bool ok = vegaflow::KernelSchedulerTweaks(reg).SetMemoryCompressionEnabled(false, opts.dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression DISABLED (stutter-free latency mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mem_compression_on_flag) {
        bool ok = vegaflow::KernelSchedulerTweaks(reg).SetMemoryCompressionEnabled(true, opts.dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression restored." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.vram_override_flag) {
        const auto hw = vegaflow::HardwareProfiler::DetectHardware();
        const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);
        vegaflow::GraphicsOptimizer gfx(hw, pol, reg, &power);
        bool ok = gfx.AmdDriver() && gfx.AmdDriver()->ApplyVramReportingOverride(pol.safe_vram_pool_mb, opts.dry_run_flag);
        std::cout << "[AMD VRAM] ApplyAmdVramReportingOverride(" << pol.safe_vram_pool_mb << ") " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. VRAM reporting set to " + std::to_string(pol.safe_vram_pool_mb) + " MB in DXGI & AMD driver.") : (gfx.AmdDriver() ? "FAILED. Administrator privileges required." : "SKIPPED. GPU is not AMD or driver tweaks blocked.")) << "\n";
        return ok ? 0 : 1;
    }

    if (opts.vram_restore_flag) {
        const auto hw = vegaflow::HardwareProfiler::DetectHardware();
        const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);
        vegaflow::GraphicsOptimizer gfx(hw, pol, reg, &power);
        bool ok = gfx.AmdDriver() && gfx.AmdDriver()->RestoreVramReporting(opts.dry_run_flag);
        std::cout << "[AMD VRAM] RestoreAmdVramReporting() " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. VRAM reporting put back as VegaFlow saved it." : (gfx.AmdDriver() ? "FAILED. VegaFlow has no saved value to put back (or administrator privileges are missing)." : "SKIPPED. GPU is not AMD or driver tweaks blocked.")) << "\n";
        return ok ? 0 : 1;
    }

    if (opts.restore_registry_flag || opts.recover_flag) {
        vegaflow::SteadyClock clock;
        vegaflow::FileChangeJournal journal(file_system, clock, vegaflow::JournalFilePath());
        vegaflow::WindowsSystemUndo undo(file_system);
        const bool loaded = journal.Load();
        // A corrupt journal means its game-file changes were not reverted: that must not look like a clean restore.
        const bool journal_corrupt = journal.Header().corrupt;

        vegaflow::RestoreReport rep{};
        if (loaded && !journal.Header().corrupt) {
            rep = vegaflow::session::RestoreAllRecordedChanges(journal, undo, opts.dry_run_flag);
        } else if (journal.Header().corrupt) {
            std::cerr << "[WARN] Diario de cambios corrupto (" << journal.Header().corrupt_reason
                      << "); restaurando copias de respaldo del registro directamente.\n";
            if (!opts.dry_run_flag) {
                journal.ResetRecoveryState();
            }
        }

        size_t reg_rev = 0;
        size_t reg_tot = 0;
        bool reg_ok = true;
        if (!opts.dry_run_flag) {
            reg_ok = reg.RestoreAllBackups(&reg_rev, &reg_tot);
        }

        if (loaded && !journal.Header().corrupt && (rep.total_count > 0 || reg_tot == 0)) {
            std::cout << "[Restore] " << rep.summary_message << "\n";
        } else {
            std::cout << "[Restore] Se devolvieron " << reg_rev << " de " << reg_tot << ".\n";
        }
        return (rep.all_succeeded && reg_ok && !journal_corrupt) ? 0 : 1;
    }

    if (!opts.dxvk_install_path.empty()) {
        std::wstring wpath = Utf8ToWide(opts.dxvk_install_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager(file_system).DeployDxvk(wpath, detected_policy(), false, true, opts.dry_run_flag, &msg);
        std::cout << "[DXVK] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (!opts.dxvk_remove_path.empty()) {
        std::wstring wpath = Utf8ToWide(opts.dxvk_remove_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager(file_system).RemoveDxvk(wpath, opts.dry_run_flag, &msg);
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
        bool ok = vegaflow::DisplayTweaks(reg).SetMpoDisabled(true, opts.dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(true) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay is now DISABLED (stutter-free mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (opts.mpo_restore_flag) {
        bool ok = vegaflow::DisplayTweaks(reg).SetMpoDisabled(false, opts.dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(false) " << (opts.dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay restored to Windows default." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    return std::nullopt;
}

} // namespace vegaflow::app
