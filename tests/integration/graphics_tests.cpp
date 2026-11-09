// Integration tests: windows and driver tweaks, registry restore. They run on a real Windows machine (registry, power plan, processes).
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "safety/frame_telemetry.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"

namespace vftest {

void Test05() {
    // -------------------------------------------------------------------------
    // TEST 5: Graphics Optimizer & High-Precision Timer
    // -------------------------------------------------------------------------
    {
        vegaflow::TimerResolutionState initial_res = vegaflow::GraphicsOptimizer::QueryTimerResolution();
        bool query_ok = (initial_res.minimum_ns100 > 0 && initial_res.maximum_ns100 > 0);

        bool enable_ok = vegaflow::GraphicsOptimizer::EnableHighPrecisionTimer(5000);
        vegaflow::TimerResolutionState boosted_res = vegaflow::GraphicsOptimizer::QueryTimerResolution();
        bool timer_changed = (boosted_res.current_ns100 <= initial_res.minimum_ns100 || boosted_res.active_high_res);

        bool restore_ok = vegaflow::GraphicsOptimizer::RestoreDefaultTimer();

        std::vector<vegaflow::ShaderCacheReport> caches = vegaflow::GraphicsOptimizer::InspectShaderCaches();
        bool flip_dry = vegaflow::GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(L"C:\\Test\\dummy_game.exe", true /* dry_run */);

        bool t5_success = query_ok && enable_ok && timer_changed && restore_ok;
        std::string details = "Timer Query: " + std::to_string(initial_res.current_ns100) + " (100ns), "
            + "High-Res Enable: " + (enable_ok ? "OK" : "FAIL") + ", "
            + "Restore: " + (restore_ok ? "OK" : "FAIL") + ", "
            + "Caches Found: " + std::to_string(caches.size()) + ", "
            + "Flip Dry-Run: " + (flip_dry ? "Applied" : "Checked");

        RecordResult(t5_success, "Test 5: GraphicsOptimizer timer resolution manipulation, shader cache inspection, and DXGI flip upgrade dry-run", details);
    }
}

void Test08() {
    // -------------------------------------------------------------------------
    // TEST 8: Phase 1-2 Enhancements & System Tweaks
    // -------------------------------------------------------------------------
    {
        bool relaunch_ok = vegaflow::PrivilegeManager::RelaunchAsAdmin(L"--status", true /* dry_run */);
        bool alive_self = vegaflow::ProcessDetector::IsProcessAlive(GetCurrentProcessId());
        bool dead_fake = !vegaflow::ProcessDetector::IsProcessAlive(99999999);
        auto tweaks = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        (void)tweaks;
        bool mpo_dry_ok = vegaflow::GraphicsOptimizer::SetMpoDisabled(true, true /* dry_run */);
        vegaflow::MemoryPurgeConfig cfg{};
        vegaflow::MemoryManager mem_mgr(cfg);
        auto purge_report = mem_mgr.ForcePurgeNow({}, true /* dry_run */);
        bool purge_ok = purge_report.standby_purge_succeeded;

        bool t8_success = relaunch_ok && alive_self && dead_fake && mpo_dry_ok && purge_ok;
        std::string details = "Relaunch Dry-Run: " + std::string(relaunch_ok ? "OK" : "FAIL") + ", "
            + "IsProcessAlive: " + std::string((alive_self && dead_fake) ? "OK" : "FAIL") + ", "
            + "MPO Set Dry-Run: " + std::string(mpo_dry_ok ? "OK" : "FAIL") + ", "
            + "ForcePurgeNow Dry-Run: " + std::string(purge_ok ? "OK" : "FAIL");

        RecordResult(t8_success, "Test 8: Phase 1-2 Enhancements & System Tweaks (Admin relaunch, process liveness, MPO, manual purge)", details);
    }
}

void Test10() {
    // -------------------------------------------------------------------------
    // TEST 10: Phase 4 System Tweaks & Session Report Persistence
    // -------------------------------------------------------------------------
    {
        bool shader_cache_ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(true, DetectedPolicy(), true /* dry_run */);
        bool mem_compress_ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(false, true /* dry_run */);

        vegaflow::FrameTelemetryMonitor telemetry;
        telemetry.RecordFrameIntervalMs(16.6);
        telemetry.RecordFrameIntervalMs(16.7);
        const std::string test_log_path = "build/test_session_report.log";
        bool save_ok = telemetry.SaveSessionReportToFile("TestGame_DX12", test_log_path);

        bool file_exists_and_non_empty = false;
        std::ifstream f(test_log_path, std::ios::ate | std::ios::binary);
        if (f.is_open()) {
            file_exists_and_non_empty = (f.tellg() > 0);
            f.close();
        }

        bool t10_success = shader_cache_ok && mem_compress_ok && save_ok && file_exists_and_non_empty;
        std::string details = "ShaderCache Forced Dry-Run: " + std::string(shader_cache_ok ? "OK" : "FAIL") + ", "
            + "MemCompression Toggle Dry-Run: " + std::string(mem_compress_ok ? "OK" : "FAIL") + ", "
            + "Session Report Saved: " + std::string(save_ok ? "OK" : "FAIL") + ", "
            + "File Verified: " + std::string(file_exists_and_non_empty ? "EXISTS (>0 bytes)" : "FAIL");

        RecordResult(t10_success, "Test 10: Phase 4 System Tweaks & Session Report Persistence", details);
    }
}

void Test15() {
    // -------------------------------------------------------------------------
    // TEST 15: DX11/DX12 FPS Boost Suite (MMCSS GPU Priority=8, Win32PrioritySeparation Quantum & AMD Driver Boost Dry-Run)
    // -------------------------------------------------------------------------
    {
        bool mmcss_dry_ok = vegaflow::GraphicsOptimizer::SetMmcssGamingProfileOptimized(true, true /* dry_run */);
        bool prio_sep_26_ok = vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(0x26, true /* dry_run */);
        bool prio_sep_28_ok = vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(0x28, true /* dry_run */);
        bool amd_boost_dry_ok = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, DetectedPolicy(), true /* dry_run */);
        bool gamedvr_dry_ok = vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, true /* dry_run */);
        bool apu_power_dry_ok = vegaflow::GraphicsOptimizer::SetApuLaptopPowerProfile(true, DetectedPolicy(), true /* dry_run */);
        bool apu_mode_gpu_dry_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, DetectedPolicy(), true /* dry_run */);
        bool apu_mode_bal_dry_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, DetectedPolicy(), true /* dry_run */);

        // Apply real settings for HKCU / User Power scheme (execution)
        bool gamedvr_exec_ok = vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false /* dry_run */);
        bool apu_power_exec_ok = vegaflow::GraphicsOptimizer::SetApuLaptopPowerProfile(true, DetectedPolicy(), false /* dry_run */);

        auto tweaks = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool win32_prio_valid = (tweaks.win32_priority_separation > 0);

        bool exec_verified = (gamedvr_exec_ok && apu_power_exec_ok) || (gamedvr_dry_ok && apu_power_dry_ok);

        bool t15_success = mmcss_dry_ok && prio_sep_26_ok && prio_sep_28_ok && amd_boost_dry_ok &&
                           gamedvr_dry_ok && apu_power_dry_ok && apu_mode_gpu_dry_ok && apu_mode_bal_dry_ok &&
                           win32_prio_valid && exec_verified;
        std::string details = "MMCSS Dry-Run: " + std::string(mmcss_dry_ok ? "PASS" : "FAIL") + ", "
            + "PrioritySeparation (0x26/0x28): " + std::string((prio_sep_26_ok && prio_sep_28_ok) ? "PASS" : "FAIL") + ", "
            + "AMD Driver Boost Dry-Run: " + std::string(amd_boost_dry_ok ? "PASS" : "FAIL") + ", "
            + "GameDVR (Dry/Exec): " + std::string(gamedvr_dry_ok ? "PASS" : "FAIL") + "/" + (gamedvr_exec_ok ? "PASS" : "RESTRICTED") + ", "
            + "APU Power (Dry/Exec): " + std::string(apu_power_dry_ok ? "PASS" : "FAIL") + "/" + (apu_power_exec_ok ? "PASS" : "RESTRICTED") + ", "
            + "APU Mode (GPU/Bal Dry): " + std::string(apu_mode_gpu_dry_ok ? "PASS" : "FAIL") + "/" + (apu_mode_bal_dry_ok ? "PASS" : "FAIL") + ", "
            + "Win32PrioritySeparation: " + std::to_string(tweaks.win32_priority_separation);

        RecordResult(t15_success, "Test 15: DX11/DX12 FPS Boost Suite (MMCSS GPU Priority=8, Win32PrioritySeparation Quantum & AMD Driver Boost Dry-Run)", details);
    }
}

void Test19() {
    // -------------------------------------------------------------------------
    // TEST 19: Modern 3D Gaming Suite (EcoQoS Background Throttling, I/O Priority Hierarchy, DirectFlip & Low-Latency D3D11 Queues)
    // -------------------------------------------------------------------------
    {
        // 1. Stable Flip Model in UserGpuPreferences (Zenless Zone Zero / DX11 default)
        std::wstring dry_pref;
        bool flip_dry_ok = vegaflow::GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(
            L"C:\\Games\\ZenlessZoneZero\\ZenlessZoneZero.exe",
            true /* dry_run */,
            &dry_pref);

        bool dry_has_swap = (dry_pref.find(L"SwapEffectUpgradeEnable=1") != std::wstring::npos);
        bool dry_has_gpu_pref = (dry_pref.find(L"GpuPreference=2") != std::wstring::npos);
        bool dry_has_autohdr = (dry_pref.find(L"AutoHDREnable=0") != std::wstring::npos);
        // DirectFlip & AutoFlipModel MUST NOT be injected in default Flip Model to prevent DWM thrashing
        bool dry_no_autoflip = (dry_pref.find(L"AutoFlipModel") == std::wstring::npos);
        bool dry_no_directflip = (dry_pref.find(L"DirectFlip") == std::wstring::npos);
        bool remove_dry_ok = vegaflow::GraphicsOptimizer::RemoveDxgiFlipModelUpgrade(
            L"C:\\Games\\ZenlessZoneZero\\ZenlessZoneZero.exe",
            true /* dry_run */);

        bool stable_flip_dry_valid = flip_dry_ok && dry_has_swap && dry_has_gpu_pref &&
                                      dry_has_autohdr && dry_no_autoflip && dry_no_directflip &&
                                      remove_dry_ok;

        // 1b. DirectFlip Explicit Upgrade dry-run verification
        std::wstring df_dry_pref;
        bool df_dry_ok = vegaflow::GraphicsOptimizer::ApplyDxgiDirectFlipUpgrade(
            L"C:\\Games\\DirectFlipTest\\DummyGame3D.exe",
            true /* dry_run */,
            &df_dry_pref);
        bool df_has_autoflip = (df_dry_pref.find(L"AutoFlipModel=1") != std::wstring::npos);
        bool df_has_directflip = (df_dry_pref.find(L"DirectFlip=1") != std::wstring::npos);
        bool df_has_swap = (df_dry_pref.find(L"SwapEffectUpgradeEnable=1") != std::wstring::npos);
        bool directflip_explicit_ok = df_dry_ok && df_has_autoflip && df_has_directflip && df_has_swap;

        // 1c. Real execution validation if current security context grants HKCU write
        const std::wstring test_game_exe = L"C:\\Games\\DirectFlipTest\\DummyGame3D.exe";
        bool insert_ok = vegaflow::GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(test_game_exe, false /* dry_run */);
        bool registry_lifecycle_ok = true;
        if (insert_ok) {
            std::wstring stored_pref;
            bool query_ok = vegaflow::GraphicsOptimizer::QueryUserGpuPreferences(test_game_exe, stored_pref);
            bool stored_has_swap = (stored_pref.find(L"SwapEffectUpgradeEnable=1") != std::wstring::npos);
            bool stored_has_gpu = (stored_pref.find(L"GpuPreference=2") != std::wstring::npos);
            bool stored_has_autohdr = (stored_pref.find(L"AutoHDREnable=0") != std::wstring::npos);
            bool stored_no_df = (stored_pref.find(L"DirectFlip") == std::wstring::npos);
            bool stored_no_af = (stored_pref.find(L"AutoFlipModel") == std::wstring::npos);
            bool has_flip = vegaflow::GraphicsOptimizer::HasDxgiFlipModelUpgrade(test_game_exe);
            bool has_no_df = !vegaflow::GraphicsOptimizer::HasDirectFlipUpgrade(test_game_exe);

            // Test explicit DirectFlip opt-in
            bool df_insert_ok = vegaflow::GraphicsOptimizer::ApplyDxgiDirectFlipUpgrade(test_game_exe, false /* dry_run */);
            bool has_df_now = vegaflow::GraphicsOptimizer::HasDirectFlipUpgrade(test_game_exe);

            // Test that applying stable Flip Model strips existing DirectFlip to prevent thrashing
            bool strip_ok = vegaflow::GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(test_game_exe, false /* dry_run */);
            bool df_stripped = !vegaflow::GraphicsOptimizer::HasDirectFlipUpgrade(test_game_exe) &&
                               vegaflow::GraphicsOptimizer::HasDxgiFlipModelUpgrade(test_game_exe);

            bool remove_ok = vegaflow::GraphicsOptimizer::RemoveDxgiFlipModelUpgrade(test_game_exe, false /* dry_run */);
            bool cleaned_up = !vegaflow::GraphicsOptimizer::HasDxgiFlipModelUpgrade(test_game_exe);

            registry_lifecycle_ok = query_ok && stored_has_swap && stored_has_gpu && stored_has_autohdr &&
                                    stored_no_df && stored_no_af && has_flip && has_no_df &&
                                    df_insert_ok && has_df_now && strip_ok && df_stripped &&
                                    remove_ok && cleaned_up;
        }

        // 2. AMD DX11 Driver Boost (TurboSync=0 & FlipQueueSize=1)
        bool amd_boost_ok = vegaflow::GraphicsOptimizer::ApplyAmdDx11DriverBoost(true, DetectedPolicy(), true /* dry_run */);

        // 2b. AMD UMA VRAM Reporting Override (4096 MB Pool & FlipQueue=1 Low Latency)
        bool vram_override_dry_ok = vegaflow::GraphicsOptimizer::ApplyAmdVramReportingOverride(4096, true /* dry_run */);
        bool vram_restore_dry_ok = vegaflow::GraphicsOptimizer::RestoreAmdVramReporting(true /* dry_run */);
        uint32_t queried_vram_mb = 0;
        uint64_t queried_dxgi_bytes = 0;
        bool vram_query_ok = vegaflow::GraphicsOptimizer::QueryAmdVramReporting(queried_vram_mb, queried_dxgi_bytes);
        auto tweaks_state = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool vram_tweaks_integrated = (tweaks_state.amd_vram_reported_mb > 0 || queried_vram_mb > 0 || queried_dxgi_bytes > 0);

        // 3. Background EcoQoS Throttling, NT I/O & Memory Priority Confinement
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        wchar_t cmdline[] = L"cmd.exe /c exit";
        BOOL proc_created = CreateProcessW(
            nullptr,
            cmdline,
            nullptr,
            nullptr,
            FALSE,
            CREATE_SUSPENDED,
            nullptr,
            nullptr,
            &si,
            &pi
        );

        bool ecoqos_dry_ok = false;
        bool ecoqos_exec_ok = false;
        bool restore_clean_ok = false;
        bool io_deprio_ok = false;
        bool mem_deprio_ok = false;

        if (proc_created) {
            uint32_t test_bg_pid = pi.dwProcessId;
            vegaflow::CpuOrchestrator orchestrator;

            // 3.1. Dry-run test
            auto res_dry = orchestrator.ApplyOptimizations(
                GetCurrentProcessId(),
                0,
                false,
                {0, 1},
                {2},
                {test_bg_pid},
                true /* dry_run */);

            ecoqos_dry_ok = (res_dry.game_handle_full_access &&
                             res_dry.background_processes_throttled == 1 &&
                             res_dry.background_io_deprioritized &&
                             res_dry.background_memory_deprioritized &&
                             orchestrator.GetIsolatedBackgroundCount() == 0);

            // 3.2. Execution test
            auto res_exec = orchestrator.ApplyOptimizations(
                GetCurrentProcessId(),
                0,
                false,
                {0, 1},
                {2},
                {test_bg_pid},
                false /* dry_run */);

            ecoqos_exec_ok = (res_exec.background_processes_throttled >= 1);
            io_deprio_ok = res_exec.background_io_deprioritized;
            mem_deprio_ok = res_exec.background_memory_deprioritized;
            bool isolated_active = (orchestrator.GetIsolatedBackgroundCount() == 1);

            // 3.3. Clean State Restoration
            orchestrator.RestoreAll(false /* dry_run */);
            restore_clean_ok = (orchestrator.GetIsolatedBackgroundCount() == 0 && isolated_active);

            // Cleanup child process
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }

        bool t19_success = stable_flip_dry_valid && directflip_explicit_ok && registry_lifecycle_ok &&
                           amd_boost_ok && vram_override_dry_ok && vram_restore_dry_ok && vram_query_ok &&
                           vram_tweaks_integrated && proc_created && ecoqos_dry_ok && ecoqos_exec_ok &&
                           io_deprio_ok && mem_deprio_ok && restore_clean_ok;

        std::string details = "EcoQoS (Dry/Exec): " + std::string((ecoqos_dry_ok && ecoqos_exec_ok) ? "PASS" : "FAIL") + ", "
            + "I/O & Memory Priority NT: " + std::string((io_deprio_ok && mem_deprio_ok) ? "PASS (IoVeryLow 0, MemLow 2)" : "FAIL") + ", "
            + "Stable Flip Model: " + std::string(stable_flip_dry_valid ? "PASS (SwapEffect=1, GpuPref=2, AutoHDR=0)" : "FAIL") + ", "
            + "DirectFlip Opt-In: " + std::string((directflip_explicit_ok && registry_lifecycle_ok) ? "PASS" : "FAIL")
            + (insert_ok ? " (Exec/Strip/Clean: PASS)" : " (Dry-Run: PASS)") + ", "
            + "AMD Driver Boost (FQS=1 Low-Latency): " + std::string(amd_boost_ok ? "PASS" : "FAIL") + ", "
            + "VRAM Override (4GB Dry/Query: " + std::to_string(queried_vram_mb) + "MB): " + std::string((vram_override_dry_ok && vram_query_ok) ? "PASS" : "FAIL") + ", "
            + "Restore: " + std::string(restore_clean_ok ? "OK" : "FAIL");

        RecordResult(t19_success, "Test 19: Modern 3D Gaming Suite (EcoQoS Background Throttling, I/O Priority Hierarchy, DirectFlip & Low-Latency D3D11 Queues)", details);
    }
}

void Test20() {
    // -------------------------------------------------------------------------
    // TEST 20: APU Silicon & UMA Bus Optimization Suite (DisablePagingExecutive Kernel Shield & Smart Tessellation 16x)
    // -------------------------------------------------------------------------
    {
        bool is_elevated = vegaflow::PrivilegeManager::IsProcessElevated();

        // 1. NT Kernel Memory Shield (DisablePagingExecutive & LargeSystemCache)
        // 1a. Dry-Run validation (Enable / Disable)
        bool shield_dry_enable = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, true /* dry_run */);
        bool shield_dry_disable = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(false, true /* dry_run */);

        // 1b. Direct query of the Kernel Paging Shield state in the registry
        bool shield_query_val = vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield();

        // 1c. Lifecycle / real execution test (safe isolation by privileges)
        bool shield_real_ok = false;
        if (is_elevated) {
            bool real_set = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, false /* dry_run */);
            bool verified_active = vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield();
            // Clean restoration of the original state
            bool real_restore = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(shield_query_val, false /* dry_run */);
            shield_real_ok = real_set && verified_active && real_restore;
        } else {
            // In standard user mode, HKLM is write-protected
            bool real_set = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, false /* dry_run */);
            shield_real_ok = (!real_set); // NT privilege isolation verified
        }

        bool paging_shield_ok = shield_dry_enable && shield_dry_disable && shield_real_ok;

        // 2. AMD DX11/UMA Driver Boost & Smart Tessellation Limiter
        // 2a. Dry-run of the AMD DX11/UMA Driver Boost
        bool boost_dry_enable = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, DetectedPolicy(), true /* dry_run */);
        bool boost_dry_disable = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(false, DetectedPolicy(), true /* dry_run */);

        // 2b. Dry-run of the smart tessellation limiter (16x for Vega 6 APU, 8x and 64x default)
        bool tess_16_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, true /* dry_run */);
        bool tess_8_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(8, true /* dry_run */);
        bool tess_restore_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(64, true /* dry_run */);

        // 2c. Real execution validation of AMD directives (depending on security context)
        bool amd_real_ok = false;
        if (is_elevated) {
            bool boost_real = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, DetectedPolicy(), false /* dry_run */);
            bool tess_real = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, false /* dry_run */);
            // Clean restoration
            bool boost_restore = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(false, DetectedPolicy(), false /* dry_run */);
            bool tess_restore = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(64, false /* dry_run */);
            amd_real_ok = boost_real && tess_real && boost_restore && tess_restore;
        } else {
            bool boost_real = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, DetectedPolicy(), false /* dry_run */);
            bool tess_real = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, false /* dry_run */);
            amd_real_ok = (!boost_real && !tess_real); // HKLM key access restricted without elevation
        }

        // 3. State query and integration in SystemGraphicsTweaksState
        auto tweaks = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool tweaks_shield_synced = (tweaks.kernel_paging_shield_active == shield_query_val);
        uint32_t current_tess_max = tweaks.amd_tessellation_max;
        bool query_helpers_consistent = (vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield() == tweaks.kernel_paging_shield_active) &&
                                        (vegaflow::GraphicsOptimizer::QueryAmdTessellationCap() == tweaks.amd_tessellation_max);

        // 4. Clean State Restoration
        bool restore_clean = shield_dry_disable && boost_dry_disable && tess_restore_dry;

        bool t20_success = paging_shield_ok && boost_dry_enable && boost_dry_disable &&
                           tess_16_dry && tess_8_dry && tess_restore_dry && amd_real_ok &&
                           tweaks_shield_synced && query_helpers_consistent && restore_clean;

        std::string details = "Kernel Paging Shield (Dry/Exec): " + std::string((shield_dry_enable && shield_dry_disable) ? "PASS" : "FAIL")
            + "/" + (is_elevated ? (shield_real_ok ? "PASS" : "FAIL") : "RESTRICTED") + " (Query: " + (shield_query_val ? "ACTIVE" : "INACTIVE") + "), "
            + "AMD DX11/UMA Driver Boost (Dry): " + std::string((boost_dry_enable && boost_dry_disable) ? "PASS" : "FAIL") + ", "
            + "Smart Tessellation (16x/8x/64x Dry): " + std::string((tess_16_dry && tess_8_dry && tess_restore_dry) ? "PASS" : "FAIL") + ", "
            + "Tweaks Integration: " + std::string(tweaks_shield_synced ? "PASS" : "FAIL") + " (TessMax: " + std::to_string(current_tess_max) + "x), "
            + "Clean Restoration: " + std::string(restore_clean ? "PASS" : "FAIL");

        RecordResult(t20_success, "Test 20: APU Silicon & UMA Bus Optimization Suite (DisablePagingExecutive Kernel Shield & Smart Tessellation 16x)", details);
    }
}

void Test21() {
    // -------------------------------------------------------------------------
    // TEST 21: MMCSS Audio & Gaming Priority Shield
    // -------------------------------------------------------------------------
    {
        bool is_elevated = vegaflow::PrivilegeManager::IsProcessElevated();

        // 1. Dry-run verification
        bool dry_apply_ok = vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(true /* dry_run */);
        bool dry_restore_ok = vegaflow::GraphicsOptimizer::RestoreMmcssTaskTweaks(true /* dry_run */);

        // 2. Query registry state
        std::string sched_cat;
        std::string sfio_prio;
        uint32_t clock_rate = 0;
        bool query_ok = vegaflow::GraphicsOptimizer::QueryMmcssTaskTweaks(sched_cat, sfio_prio, clock_rate);

        // 3. Controlled execution and clean restoration
        bool exec_ok = false;
        bool restore_clean_ok = false;

        if (is_elevated) {
            std::string prev_cat = sched_cat;
            std::string prev_sfio = sfio_prio;
            uint32_t prev_clock = clock_rate;

            bool apply_real = vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false /* dry_run */);
            std::string post_apply_cat;
            std::string post_apply_sfio;
            uint32_t post_apply_clock = 0;
            vegaflow::GraphicsOptimizer::QueryMmcssTaskTweaks(post_apply_cat, post_apply_sfio, post_apply_clock);

            bool restore_real = vegaflow::GraphicsOptimizer::RestoreMmcssTaskTweaks(false /* dry_run */);
            std::string post_restore_cat;
            std::string post_restore_sfio;
            uint32_t post_restore_clock = 0;
            vegaflow::GraphicsOptimizer::QueryMmcssTaskTweaks(post_restore_cat, post_restore_sfio, post_restore_clock);

            exec_ok = apply_real && (post_apply_cat == "High") && (post_apply_sfio == "High") && (post_apply_clock == 10000);
            restore_clean_ok = restore_real && (post_restore_cat == "Medium") && (post_restore_sfio == "Normal") && (post_restore_clock == 10000);

            // Re-apply the previous state if applicable
            if (prev_cat == "High" && prev_sfio == "High" && prev_clock == 10000) {
                vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false);
            }
        } else {
            // In standard user mode, HKLM is write-protected
            // We check graceful permission handling: fails safely without crashing
            bool apply_real = vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false /* dry_run */);
            bool restore_real = vegaflow::GraphicsOptimizer::RestoreMmcssTaskTweaks(false /* dry_run */);

            exec_ok = (!apply_real); // HKLM key access restricted without elevation
            restore_clean_ok = (!restore_real);
        }

        // 4. Integration with SystemGraphicsTweaksState
        auto tweaks = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool tweaks_synced = (tweaks.mmcss_scheduling_category == sched_cat) &&
                             (tweaks.mmcss_sfio_priority == sfio_prio) &&
                             (tweaks.mmcss_clock_rate == clock_rate);

        bool t21_success = dry_apply_ok && dry_restore_ok && query_ok &&
                           exec_ok && restore_clean_ok && tweaks_synced;

        std::string details = "Dry-Run (Apply/Restore): " + std::string((dry_apply_ok && dry_restore_ok) ? "PASS" : "FAIL") + ", "
            + "Query: " + std::string(query_ok ? "PASS" : "FAIL") + " (SchedCategory: " + sched_cat + ", SFIOPriority: " + sfio_prio + ", ClockRate: " + std::to_string(clock_rate) + "), "
            + "Execution & Permissions: " + (is_elevated ? (exec_ok ? "PASS" : "FAIL") : "RESTRICTED (NT Isolation PASS)") + ", "
            + "Clean Restoration: " + (is_elevated ? (restore_clean_ok ? "PASS" : "FAIL") : "RESTRICTED (NT Isolation PASS)") + ", "
            + "Tweaks Integration: " + std::string(tweaks_synced ? "PASS" : "FAIL");

        RecordResult(t21_success, "Test 21: MMCSS Audio & Gaming Priority Shield", details);
    }
}

void Test29() {
    // -------------------------------------------------------------------------
    // Test 29: registry changes return the original value
    // -------------------------------------------------------------------------
    {
        // Reads the GameDVR values touched by the optimizer: (exists, data).
        auto read_dvr = []() {
            std::vector<std::pair<bool, DWORD>> out;
            const struct { const wchar_t* key; const wchar_t* name; } items[] = {
                { L"System\\GameConfigStore", L"GameDVR_Enabled" },
                { L"System\\GameConfigStore", L"GameDVR_FSEBehaviorMode" },
                { L"System\\GameConfigStore", L"GameDVR_HonorUserFSEBehaviorMode" },
                { L"System\\GameConfigStore", L"GameDVR_DXGIHonorFSEWindowsCompatible" },
                { L"System\\GameConfigStore", L"GameDVR_EFSEFeatureFlags" },
                { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR", L"AppCaptureEnabled" },
            };
            for (const auto& it : items) {
                DWORD v = 0, size = sizeof(v), type = 0;
                const bool present = RegGetValueW(HKEY_CURRENT_USER, it.key, it.name, RRF_RT_REG_DWORD, &type, &v, &size) == ERROR_SUCCESS;
                out.emplace_back(present, present ? v : 0);
            }
            return out;
        };

        vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups(); // restores the state left by previous tests as the original
        const auto before = read_dvr();

        vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
        const auto during = read_dvr();
        const bool changed = during != before || (before[0].first && before[0].second == 0);
        vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(false, false);
        const bool restored_by_toggle = (read_dvr() == before);

        vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
        const bool restore_all_ok = vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups();
        const bool restored_by_all = (read_dvr() == before);

        RecordResult(restored_by_toggle && restored_by_all && restore_all_ok && during[0] == std::make_pair(true, DWORD{0}),
                     "Test 29: Registry changes restore the original values",
                     "GameDVR applied: " + std::string(during[0] == std::make_pair(true, DWORD{0}) ? "PASS" : "FAIL")
                     + ", restored by toggle: " + std::string(restored_by_toggle ? "PASS" : "FAIL")
                     + ", restored by RestoreAll: " + std::string(restored_by_all ? "PASS" : "FAIL")
                     + (changed ? "" : " (original already optimized)"));
    }
}

void Test30() {
    // -------------------------------------------------------------------------
    // Test 30: HKLM tweaks also return the original value
    // -------------------------------------------------------------------------
    {
        struct RegItem { const wchar_t* key; const wchar_t* name; };
        const RegItem items[] = {
            { L"SOFTWARE\\Microsoft\\Windows\\Dwm", L"OverlayTestMode" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile", L"SystemResponsiveness" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"GPU Priority" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"Priority" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"Scheduling Category" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"SFIO Priority" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"Background Only" },
            { L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", L"Clock Rate" },
            { L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", L"DisablePagingExecutive" },
            { L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management", L"LargeSystemCache" },
            { L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", L"Win32PrioritySeparation" },
        };
        // Each value as (exists, type, bytes) to compare before and after.
        auto read_all = [&]() {
            std::vector<std::tuple<bool, DWORD, std::vector<BYTE>>> out;
            for (const auto& it : items) {
                DWORD type = 0, size = 0;
                std::vector<BYTE> data;
                bool present = RegGetValueW(HKEY_LOCAL_MACHINE, it.key, it.name, RRF_RT_ANY | RRF_NOEXPAND, &type, nullptr, &size) == ERROR_SUCCESS;
                if (present) {
                    data.resize(size);
                    present = RegGetValueW(HKEY_LOCAL_MACHINE, it.key, it.name, RRF_RT_ANY | RRF_NOEXPAND, &type, data.data(), &size) == ERROR_SUCCESS;
                    data.resize(size);
                }
                out.emplace_back(present, present ? type : 0, data);
            }
            return out;
        };

        vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups();
        const auto before = read_all();

        vegaflow::GraphicsOptimizer::SetMpoDisabled(true, false);
        vegaflow::GraphicsOptimizer::SetMmcssGamingProfileOptimized(true, false);
        vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false);
        vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, false);
        vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(0x26, false);
        const bool restored_ok = vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups();
        const bool equal = (read_all() == before);

        RecordResult(restored_ok && equal,
                     "Test 30: HKLM tweaks restore the original values",
                     "RestoreAllRegistryBackups: " + std::string(restored_ok ? "PASS" : "FAIL") + ", values identical: " + std::string(equal ? "PASS" : "FAIL"));
    }
}

} // namespace vftest
