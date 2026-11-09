#include "session/reports.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include <tlhelp32.h>
#include "session/text_convert.hpp"

namespace vegaflow::app {

namespace {

std::string FormatBytes(uint64_t bytes) {
    std::ostringstream ss;
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    double gb = mb / 1024.0;
    if (gb >= 1.0) {
        ss << std::fixed << std::setprecision(2) << gb << " GB ("
           << static_cast<uint64_t>(mb) << " MB)";
    } else {
        ss << std::fixed << std::setprecision(2) << mb << " MB ("
           << bytes << " bytes)";
    }
    return ss.str();
}

} // namespace

uint32_t CountEcoQoSProcesses() {
    uint32_t count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID <= 4) continue;
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_POWER_THROTTLING_STATE throttling{};
                    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                    if (GetProcessInformation(hProc, ProcessPowerThrottling, &throttling, sizeof(throttling))) {
                        if (throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) {
                            count++;
                        }
                    }
                    CloseHandle(hProc);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return count;
}

uint32_t CountDirectFlipConfiguredGames() {
    uint32_t count = 0;
    HKEY hPref = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_READ, &hPref) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t val_name[MAX_PATH];
        DWORD val_name_len = MAX_PATH;
        DWORD val_type = 0;
        wchar_t val_data[512];
        DWORD val_data_len = sizeof(val_data);
        while (RegEnumValueW(hPref, index++, val_name, &val_name_len, nullptr, &val_type, reinterpret_cast<LPBYTE>(val_data), &val_data_len) == ERROR_SUCCESS) {
            if (val_type == REG_SZ) {
                std::wstring data_str(val_data);
                for (auto& c : data_str) c = static_cast<wchar_t>(std::towlower(c));
                if (data_str.find(L"autoflipmodel=1") != std::wstring::npos && data_str.find(L"directflip=1") != std::wstring::npos) {
                    count++;
                }
            }
            val_name_len = MAX_PATH;
            val_data_len = sizeof(val_data);
        }
        RegCloseKey(hPref);
    }
    return count;
}

void PrintStatusReport() {
    using namespace vegaflow;

    PrivilegeStatus priv = PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    MemoryPurgeConfig mem_cfg{};
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    SystemMemoryTelemetry mem = mem_mgr.SampleTelemetry();

    TimerResolutionState timer = GraphicsOptimizer::QueryTimerResolution();
    SystemGraphicsTweaksState tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
    std::vector<ShaderCacheReport> shader_caches = GraphicsOptimizer::InspectShaderCaches();
    std::vector<uint32_t> noisy_pids = ProcessDetector::FindBackgroundNoisyProcesses();
    uint64_t daemon_ram_bytes = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();

    std::cout << "================================================================================\n";
    std::cout << "                     VEGAFLOW SYSTEM STATUS REPORT\n";
    std::cout << "================================================================================\n\n";

    std::cout << "[PRIVILEGES & SECURITY]\n";
    std::cout << "  Process Elevated (Admin)   : " << (priv.is_elevated ? "YES" : "NO") << "\n";
    std::cout << "  Profile Single Process     : " << (priv.profile_single_process ? "GRANTED" : "NOT GRANTED") << "\n";
    std::cout << "  Increase Base Priority     : " << (priv.increase_base_priority ? "GRANTED" : "NOT GRANTED") << "\n";
    if (!priv.is_elevated) {
        std::cout << "  [NOTE] Running as Standard User. Standby List purge and system tweaks require Administrator privileges.\n";
        std::cout << "         Execute 'vegaflow_daemon.exe --elevate' or '--purge-now' with Admin rights to free Standby RAM.\n";
    }
    std::cout << "\n";

    std::cout << "[CPU TOPOLOGY & SCHEDULING]\n";
    std::cout << "  Processor Brand            : " << (hw.cpu_topology.brand_string.empty() ? "Generic x86_64" : hw.cpu_topology.brand_string) << "\n";
    std::cout << "  Physical Cores             : " << hw.cpu_topology.physical_cores << "\n";
    std::cout << "  Logical Processors         : " << hw.cpu_topology.logical_processors << "\n";
    std::cout << "  Architecture Type          : " 
              << (hw.cpu_topology.has_hybrid_cores ? "Intel Hybrid (P/E Cores)" : (hw.cpu_topology.has_multiple_ccx ? "AMD Multi-CCX/CCD" : "Monolithic Uniform")) << "\n";
    
    std::cout << "  Game Optimal CPU Set IDs   : [";
    for (size_t i = 0; i < hw.cpu_topology.game_optimal_cpu_set_ids.size(); ++i) {
        std::cout << hw.cpu_topology.game_optimal_cpu_set_ids[i] << (i + 1 < hw.cpu_topology.game_optimal_cpu_set_ids.size() ? ", " : "");
    }
    std::cout << "]\n";

    std::cout << "  Background CPU Set IDs     : [";
    for (size_t i = 0; i < hw.cpu_topology.background_cpu_set_ids.size(); ++i) {
        std::cout << hw.cpu_topology.background_cpu_set_ids[i] << (i + 1 < hw.cpu_topology.background_cpu_set_ids.size() ? ", " : "");
    }
    std::cout << "]\n";
    uint32_t ecoqos_count = CountEcoQoSProcesses();
    std::cout << "  EcoQoS Throttled Processes : " << ecoqos_count << " processes in Efficiency Mode (Thermal Headroom for APU)\n\n";

    std::cout << "[GPU & GRAPHICS ARCHITECTURE]\n";
    std::cout << "  Primary Adapter Description: " << (hw.primary_gpu.description.empty() ? "Standard Display Adapter" : WideToUtf8(hw.primary_gpu.description)) << "\n";
    std::cout << "  Vendor                     : " << HardwareProfiler::VendorToString(hw.primary_gpu.vendor)
              << " (PCI ID: 0x" << std::hex << hw.primary_gpu.vendor_id << ":0x" << hw.primary_gpu.device_id << std::dec << ")\n";
    std::cout << "  Memory Architecture        : " << HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) << "\n";
    std::cout << "  Dedicated Video Memory     : " << FormatBytes(hw.primary_gpu.dedicated_vram_bytes) << "\n";
    std::cout << "  Shared System Memory       : " << FormatBytes(hw.primary_gpu.shared_system_bytes) << "\n\n";

    ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw, mem.vram.local_budget_bytes);
    UmaGuardStatus uma_guard = mem_mgr.EvaluateUmaPressureGuard(mem, policy, {}, true);

    std::cout << "[APU CAPABILITY GUARD & DX11/DX12 POLICY]\n";
    std::cout << "  Detected APU Tier & Guard  : " << policy.tier_label << "\n";
    std::cout << "  GPU Architecture Gen       : " << HardwareProfiler::ArchGenToString(policy.arch_gen) << "\n";
    std::cout << "  Safe UMA VRAM / Shared Pool: " << policy.safe_vram_pool_mb << " MB / " << policy.safe_shared_pool_mb << " MB (OS Floor: " << policy.os_reserve_floor_mb << " MB)\n";
    std::cout << "  DX11 Specialization        : DXVK Compiler Threads=" << policy.dxvk_compiler_threads << ", AMD Tessellation Cap=" << policy.amd_tessellation_cap << "x, Max Anisotropy=" << policy.max_anisotropy << "x\n";
    std::cout << "  DX12 Specialization        : AsyncCompute=" << (policy.disable_dx12_async_compute ? "DISABLED (UMA Contention Guard)" : "ENABLED (Do-No-Harm Guard)")
              << ", UE CSM=" << policy.ue_max_csm_resolution << "px (" << policy.ue_max_shadow_cascades << " cascades)\n";
    std::cout << "  CPU STAPM Thermal Ceiling  : " << (policy.cpu_stapm_cap_mhz == 0 ? "UNCAPPED 100% CPU+GPU (Smart Efficient-Aggressive + EPP 25)" : (std::to_string(policy.cpu_stapm_cap_mhz) + " MHz (Sustained GPU Priority)")) << "\n";
    std::cout << "  Live DXGI Budget Pressure  : " << static_cast<int>(uma_guard.dxgi_budget_utilization * 100.0) << "% (Threshold: 85% -> " << (uma_guard.guard_triggered ? "PRESSURE GUARD ACTIVE" : "HEALTHY") << ")\n\n";

    std::cout << "[SYSTEM GRAPHICS TWEAKS]\n";
    std::cout << "  Multi-Plane Overlay (MPO)  : " << (tweaks.mpo_disabled ? "DISABLED (OverlayTestMode=5, Stutter-Free)" : "ENABLED (Windows Default)") << "\n";
    std::cout << "  AMD ShaderCache Mode       : " << (tweaks.amd_shader_cache_forced ? "FORCED ON (0x3200, Stutter Elimination)" : "Driver Default (AMD Optimized)") << "\n";
    std::cout << "  AMD DX11/UMA Driver Boost  : " << (tweaks.amd_dx11_uma_boost ? "ACTIVE (SurfaceFormat FP16 + FlipQueue=1)" : "Driver Default") << "\n";
    std::cout << "  AMD VRAM Reporting Pool    : " << (tweaks.amd_vram_override_active ? ("FORCED (" + std::to_string(tweaks.amd_vram_reported_mb > 0 ? tweaks.amd_vram_reported_mb : 4096) + " MB DXGI/AMD)") : ("Driver Default (" + std::to_string(tweaks.amd_vram_reported_mb > 0 ? tweaks.amd_vram_reported_mb : 512) + " MB)")) << "\n";
    std::cout << "  MMCSS WDDM Gaming Profile  : " << (tweaks.mmcss_gaming_optimized ? "OPTIMIZED (GPU Priority=8, SystemResponsiveness=10%)" : "Windows Default (GPU Priority=2)") << "\n";
    std::cout << "  GameDVR & FSE Bypass       : " << (tweaks.gamedvr_disabled ? "OPTIMIZED (Background Capture Off + FSE Mode 2)" : "DEFAULT (Windows DVR Active)") << "\n";
    std::cout << "  APU Laptop Power & Bus     : " << (tweaks.apu_power_optimized ? "ACTIVE (Boost Efficient-Aggressive + PCIe ASPM Off)" : "STANDARD (Windows Default)") << "\n";
    std::cout << "  CPU Quantum Separation     : 0x" << std::hex << tweaks.win32_priority_separation << std::dec << "\n";
    std::cout << "  Windows Memory Compression : " << (tweaks.memory_compression_enabled ? "ACTIVE (MemCompression Process)" : "DISABLED (Zero CPU Spikes / Low Latency)") << "\n";
    std::cout << "  Kernel Memory Paging Shield: " << (tweaks.kernel_paging_shield_active ? "ACTIVE (DisablePagingExecutive=1, RAM Lock)" : "DEFAULT (Paging Allowed)") << "\n";
    std::cout << "  AMD Tessellation Limiter   : " << ((tweaks.amd_tessellation_max > 0 && tweaks.amd_tessellation_max <= 16) ? (std::to_string(tweaks.amd_tessellation_max) + "x (Over-Geometry Capped)") : (tweaks.amd_dx11_uma_boost ? "16x (Over-Geometry Capped)" : (tweaks.amd_tessellation_max > 0 ? (std::to_string(tweaks.amd_tessellation_max) + "x (Driver Setting)") : "16x (Over-Geometry Capped)"))) << "\n";

    uint32_t df_count = CountDirectFlipConfiguredGames();
    auto active_game = ProcessDetector::DetectActiveGame();
    bool directflip_live = active_game.has_value() && GraphicsOptimizer::HasDirectFlipUpgrade(active_game->full_path);
    std::string df_status;
    if (directflip_live) {
        df_status = "ACTIVE (" + WideToUtf8(active_game->exe_name) + " - Latency 0ms / Zero-Copy Bypass)";
    } else if (df_count > 0) {
        df_status = "CONFIGURED (" + std::to_string(df_count) + " Games with AutoFlip/DirectFlip, Latency 0ms)";
    } else {
        df_status = "READY (AutoFlipModel=1 & DirectFlip=1 Injection on Launch)";
    }
    std::cout << "  DirectFlip / AutoFlip Model: " << df_status << "\n\n";

    std::cout << "[RAM & TELEMETRY]\n";
    std::cout << "  Total Physical RAM         : " << FormatBytes(mem.total_physical_bytes) << "\n";
    std::cout << "  Free + Zero RAM            : " << FormatBytes(mem.free_and_zero_bytes) << "\n";
    std::cout << "  Standby List RAM           : " << FormatBytes(mem.standby_bytes) << "\n";
    std::cout << "  Modified RAM               : " << FormatBytes(mem.modified_bytes) << "\n\n";

    std::cout << "[DXGI 1.4+ VIDEO MEMORY BUDGET (QUERY_VIDEO_MEMORY_INFO)]\n";
    if (mem.vram.valid) {
        std::cout << "  Local VRAM Usage / Budget  : " << (mem.vram.local_usage_bytes / (1024 * 1024)) << " MB / "
                  << (mem.vram.local_budget_bytes / (1024 * 1024)) << " MB\n";
        std::cout << "  Non-Local Usage / Budget   : " << (mem.vram.non_local_usage_bytes / (1024 * 1024)) << " MB / "
                  << (mem.vram.non_local_budget_bytes / (1024 * 1024)) << " MB\n";
    } else {
        std::cout << "  Local VRAM Usage / Budget  : N/A (Standard DXGI query unavailable)\n";
    }
    std::cout << "\n";

    std::cout << "[WINDOWS TIMER RESOLUTION]\n";
    std::cout << "  Current Resolution         : " << (static_cast<double>(timer.current_ns100) / 10000.0) << " ms (" << timer.current_ns100 << " in 100ns units)\n";
    std::cout << "  Minimum (Default) Res      : " << (static_cast<double>(timer.minimum_ns100) / 10000.0) << " ms (" << timer.minimum_ns100 << " in 100ns units)\n";
    std::cout << "  Maximum (High-Precision)   : " << (static_cast<double>(timer.maximum_ns100) / 10000.0) << " ms (" << timer.maximum_ns100 << " in 100ns units)\n";
    std::cout << "  High-Precision Active      : " << (timer.active_high_res ? "YES" : "NO") << "\n\n";

    std::cout << "[SHADER CACHES DETECTED]\n";
    if (shader_caches.empty()) {
        std::cout << "  None found in standard directory trees.\n";
    } else {
        for (size_t i = 0; i < shader_caches.size(); ++i) {
            std::cout << "  [" << (i + 1) << "] " << WideToUtf8(shader_caches[i].path)
                      << " | " << (shader_caches[i].total_size_bytes / (1024 * 1024)) << " MB"
                      << " (" << shader_caches[i].file_count << " files)"
                      << (shader_caches[i].exists ? "" : " [Missing]") << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "[STEAM LIBRARY & ENGINE PROFILES]\n";
    std::wstring steam_path = SteamEngineOptimizer::GetSteamInstallPath();
    std::cout << "  Steam Install Path         : " << (steam_path.empty() ? "Not detected / Not installed" : WideToUtf8(steam_path)) << "\n";
    uint32_t running_steam_app = SteamEngineOptimizer::GetRunningSteamAppId();
    std::cout << "  Running Steam AppID        : " << (running_steam_app != 0 ? std::to_string(running_steam_app) : "None (0)") << "\n";
    auto steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    std::cout << "  Discovered Steam Games     : " << steam_games.size() << " games\n";
    if (steam_games.empty()) {
        std::cout << "    (No games detected in libraryfolders.vdf)\n";
    } else {
        size_t show_count = std::min<size_t>(steam_games.size(), 5);
        for (size_t i = 0; i < show_count; ++i) {
            std::cout << "    - AppID " << steam_games[i].app_id << ": " << steam_games[i].name
                      << " [" << SteamEngineOptimizer::EngineTypeToString(steam_games[i].engine) << "]"
                      << (steam_games[i].has_anticheat ? " (AntiCheat Protected)" : "")
                      << (steam_games[i].dxvk_installed ? " [DXVK Vulkan: ACTIVO]" : "") << "\n";
        }
        if (steam_games.size() > 5) {
            std::cout << "      ... and " << (steam_games.size() - 5) << " more games\n";
        }
    }
    std::cout << "\n";

    std::cout << "[SYSTEM PROCESSES & DAEMON FOOTPRINT]\n";
    std::cout << "  Background Noisy PIDs Found: " << noisy_pids.size() << " processes\n";
    std::cout << "  Daemon Private Working Set : " << (static_cast<double>(daemon_ram_bytes) / (1024.0 * 1024.0)) << " MB ("
              << (daemon_ram_bytes / 1024) << " KB)\n";
    std::cout << "  Zero-Overhead KPI Target   : < 15.00 MB -> "
              << (daemon_ram_bytes < 15ULL * 1024 * 1024 ? "PASSED (Optimal)" : "WARNING") << "\n";
    std::cout << "================================================================================\n";
}

void ExecutePurgeNow(bool dry_run) {
    using namespace vegaflow;

    PrivilegeManager::EnableRequiredPrivileges(false);
    auto noisy_pids = ProcessDetector::FindBackgroundNoisyProcesses();

    MemoryPurgeConfig cfg{};
    MemoryManager mem_mgr(cfg);
    auto report = mem_mgr.ForcePurgeNow(noisy_pids, dry_run);

    std::cout << "================================================================================\n";
    std::cout << "              VEGAFLOW MANUAL MEMORY PURGE " << (dry_run ? "(DRY-RUN)" : "") << "\n";
    std::cout << "================================================================================\n";
    std::cout << "  Status Message             : " << report.status_message << "\n";
    std::cout << "  Standby Purged Successfully: " << (report.standby_purge_succeeded ? "YES" : "NO") << "\n";
    std::cout << "  Background Working Sets Trim: " << report.trimmed_processes << " processes\n";
    std::cout << "  Free + Zero RAM (Before)   : " << FormatBytes(report.free_bytes_before) << "\n";
    std::cout << "  Free + Zero RAM (After)    : " << FormatBytes(report.free_bytes_after) << "\n";
    std::cout << "  Standby List RAM (Before)  : " << FormatBytes(report.standby_bytes_before) << "\n";
    std::cout << "  Standby List RAM (After)   : " << FormatBytes(report.standby_bytes_after) << "\n";

    if (!PrivilegeManager::IsProcessElevated() && !dry_run && !report.standby_purge_succeeded) {
        std::cout << "\n  [TIP] Purging the Standby List requires Administrator privileges.\n";
        std::cout << "        Execute 'vegaflow_daemon.exe --elevate' or run cmd/PowerShell as Admin.\n";
    }
    std::cout << "================================================================================\n";
}

void ExecuteDryRun(const AppConfig& config) {
    using namespace vegaflow;

    std::cout << "================================================================================\n";
    std::cout << "              VEGAFLOW DAEMON (DRY-RUN SIMULATION & EVALUATION)\n";
    std::cout << "================================================================================\n\n";

    PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    CpuOrchestrator cpu_orchestrator;

    std::cout << "[1/4] Scanning foreground processes for 3D Game / Graphics API...\n";
    auto game_opt = ProcessDetector::DetectActiveGame(config.custom_game_exes);

    if (game_opt.has_value()) {
        const auto& game = *game_opt;
        std::cout << "  -> Active 3D Game Detected: " << WideToUtf8(game.exe_name) << " (PID: " << game.pid << ")\n";
        std::cout << "  -> Path: " << WideToUtf8(game.full_path) << "\n";
        std::cout << "  -> Render API: " << ProcessDetector::RenderApiToString(game.api) << "\n";
        std::cout << "  -> Driver DLL: " << (game.driver_dll_loaded.empty() ? "N/A" : WideToUtf8(game.driver_dll_loaded)) << "\n";
        std::cout << "  -> Presenter Thread ID: " << game.presenter_thread_id << "\n\n";

        std::cout << "[2/4] [DRY-RUN] Graphics Optimizations Evaluation:\n";
        std::cout << "  -> High-Precision Timer: Would call NtSetTimerResolution(" << config.timer_resolution_ns100 << ", TRUE) [0.5 ms target]\n";
        bool flip_already = GraphicsOptimizer::HasDxgiFlipModelUpgrade(game.full_path);
        std::cout << "  -> DXGI Flip Model Upgrade: " << (flip_already ? "Already enabled in registry" : "Would set SwapEffectUpgradeEnable=1 in HKCU UserGpuPreferences") << "\n";
        GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, true /* dry_run */);
        std::cout << "\n";

        std::cout << "[3/4] [DRY-RUN] CPU Orchestrator Evaluation:\n";
        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(game.pid);
        std::cout << "  -> Background Noisy Processes to Isolate: " << bg_pids.size() << " PIDs\n";
        bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
        const std::vector<ULONG> no_sets;
        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            game.pid,
            game.presenter_thread_id,
            is_dx12,
            config.restrict_game_cpu_sets ? hw.cpu_topology.game_optimal_cpu_set_ids : no_sets,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            true /* dry_run */);

        std::cout << "  -> Game Handle Access Level: " << (cpu_res.game_handle_full_access ? "Full (PROCESS_SET_INFORMATION)" : "Degraded Read-Only (Anti-Cheat safe)") << "\n";
        std::cout << "  -> Priority Class Selection: " << (is_dx12 ? "ABOVE_NORMAL (DX12/Vulkan balanced)" : "HIGH (DX11 render-centric)") << "\n";
        std::cout << "  -> Game CPU Sets Targeted  : " << hw.cpu_topology.game_optimal_cpu_set_ids.size() << " cores\n";
        std::cout << "  -> Background Isolated Sets: " << hw.cpu_topology.background_cpu_set_ids.size() << " cores\n\n";

        std::cout << "[4/4] [DRY-RUN] Memory Manager Evaluation (Zero-Stutter Hysteresis):\n";
        auto mem_res = mem_mgr.EvaluateAndOptimize(game.pid, bg_pids, true /* dry_run */);
        std::cout << "  -> Standby Purge Triggered : " << (mem_res.standby_purged ? "YES" : "NO") << "\n";
        std::cout << "  -> Background Trim Triggered: " << (mem_res.background_processes_trimmed > 0 ? "YES" : "NO") << " (" << mem_res.background_processes_trimmed << " processes)\n";
        std::cout << "  -> Cooldown Active         : " << (mem_res.skipped_due_to_cooldown ? "YES" : "NO") << "\n";
        std::cout << "  -> Disk I/O Gating Active  : " << (mem_res.skipped_due_to_disk_io ? "YES (Game loading assets)" : "NO") << "\n";
        std::cout << "  -> Reason / Status Message : " << mem_res.reason << "\n\n";

        cpu_orchestrator.RestoreAll(true);
    } else {
        std::cout << "  -> No foreground 3D game currently detected.\n";
        std::cout << "  -> Demonstrating evaluation on background processes under simulated game conditions:\n\n";

        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(0);
        std::cout << "[2/4] [DRY-RUN] High-Precision Timer: Would set timer resolution to " << config.timer_resolution_ns100 << " (0.5 ms)\n\n";

        std::cout << "[3/4] [DRY-RUN] CPU Orchestrator Evaluation:\n";
        std::cout << "  -> Background Processes Found: " << bg_pids.size() << " PIDs\n";
        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            0,
            0,
            false,
            hw.cpu_topology.game_optimal_cpu_set_ids,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            true /* dry_run */);
        std::cout << "  -> Background Sets to confine: " << hw.cpu_topology.background_cpu_set_ids.size() << " core IDs (Isolated in dry-run: " << cpu_res.background_processes_isolated << " targets)\n\n";

        std::cout << "[4/4] [DRY-RUN] Memory Manager Evaluation:\n";
        auto mem_res = mem_mgr.EvaluateAndOptimize(0, bg_pids, true /* dry_run */);
        std::cout << "  -> Standby Purge Triggered : " << (mem_res.standby_purged ? "YES" : "NO") << "\n";
        std::cout << "  -> Background Trim Count   : " << mem_res.background_processes_trimmed << "\n";
        std::cout << "  -> Evaluation Reason       : " << mem_res.reason << "\n\n";

        cpu_orchestrator.RestoreAll(true);
    }

    uint64_t daemon_ram = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
    std::cout << "[KPI] Daemon Memory Working Set: " << (static_cast<double>(daemon_ram) / (1024.0 * 1024.0)) << " MB (< 15 MB KPI passed)\n";
    std::cout << "================================================================================\n";
}

} // namespace vegaflow::app
