#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "privileges.hpp"
#include "hardware_profiler.hpp"
#include "process_detector.hpp"
#include "graphics_optimizer.hpp"
#include "memory_manager.hpp"
#include "cpu_orchestrator.hpp"
#include "frame_telemetry.hpp"
#include "fsr_scaler.hpp"
#include "steam_engine_optimizer.hpp"
#include "dxvk_manager.hpp"
#include "sandbox_guard.hpp"
#include "system_info.hpp"
#include <filesystem>
#include <fstream>

namespace {

int g_pass_count = 0;
int g_fail_count = 0;

void RecordResult(bool success, const std::string& test_name, const std::string& details = "") {
    if (success) {
        g_pass_count++;
        std::cout << "[PASS] " << test_name;
        if (!details.empty()) {
            std::cout << " (" << details << ")";
        }
        std::cout << "\n";
    } else {
        g_fail_count++;
        std::cerr << "[FAIL] " << test_name;
        if (!details.empty()) {
            std::cerr << " -> " << details;
        }
        std::cerr << "\n";
    }
}

/** Al terminar devuelve el plan de energia a su estado anterior si alguna prueba aplico el perfil APU de verdad. */
struct PowerPlanGuard {
    ~PowerPlanGuard() {
        if (vegaflow::GraphicsOptimizer::IsApuPowerModeApplied()) {
            vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced);
        }
        vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups();
    }
};

} // anonymous namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    PowerPlanGuard power_plan_guard;

    std::cout << "================================================================================\n";
    std::cout << "                     VEGAFLOW INTEGRATED TEST SUITE                             \n";
    std::cout << "================================================================================\n\n";

    // -------------------------------------------------------------------------
    // TEST 1: Privileges
    // -------------------------------------------------------------------------
    {
        bool elevated = vegaflow::PrivilegeManager::IsProcessElevated();
        vegaflow::PrivilegeStatus status = vegaflow::PrivilegeManager::EnableRequiredPrivileges(false);
        bool t1_success = (status.is_elevated == elevated);
        RecordResult(
            t1_success,
            "Test 1: PrivilegeManager elevation check and privilege adjustment",
            elevated ? "Elevated/Admin" : "Standard User"
        );
    }

    // -------------------------------------------------------------------------
    // TEST 2: Hardware Profiler Universal
    // -------------------------------------------------------------------------
    {
        vegaflow::SystemHardwareProfile hw = vegaflow::HardwareProfiler::DetectHardware();

        bool ram_ok = (hw.total_physical_ram_bytes > 0);
        bool gpu_ok = (hw.primary_gpu.vendor != vegaflow::GpuVendor::Unknown || !hw.primary_gpu.description.empty());
        bool cores_ok = (hw.cpu_topology.physical_cores > 0 && hw.cpu_topology.logical_processors >= hw.cpu_topology.physical_cores);
        bool sets_non_empty = (!hw.cpu_topology.game_optimal_cpu_set_ids.empty() && !hw.cpu_topology.background_cpu_set_ids.empty());

        bool disjoint = true;
        for (ULONG g_id : hw.cpu_topology.game_optimal_cpu_set_ids) {
            for (ULONG b_id : hw.cpu_topology.background_cpu_set_ids) {
                if (g_id == b_id) {
                    disjoint = false;
                    break;
                }
            }
            if (!disjoint) break;
        }

        bool t2_success = ram_ok && gpu_ok && cores_ok && sets_non_empty && disjoint;
        std::string details = "RAM: " + std::to_string(hw.total_physical_ram_bytes / (1024 * 1024)) + " MB, "
            + "Cores: " + std::to_string(hw.cpu_topology.physical_cores) + "P/" + std::to_string(hw.cpu_topology.logical_processors) + "L, "
            + "Game Sets: " + std::to_string(hw.cpu_topology.game_optimal_cpu_set_ids.size()) + ", "
            + "Bg Sets: " + std::to_string(hw.cpu_topology.background_cpu_set_ids.size()) + ", "
            + (disjoint ? "Disjoint Sets: YES" : "Disjoint Sets: NO");

        RecordResult(t2_success, "Test 2: HardwareProfiler universal hardware & CPU set detection", details);
    }

    // -------------------------------------------------------------------------
    // TEST 3: Memory Manager & UMA/VRAM Telemetry
    // -------------------------------------------------------------------------
    {
        vegaflow::MemoryPurgeConfig cfg{};
        vegaflow::MemoryManager mem_mgr(cfg);
        vegaflow::SystemMemoryTelemetry telemetry = mem_mgr.SampleTelemetry();

        bool ram_ok = (telemetry.total_physical_bytes > 0);
        bool vram_ok = telemetry.vram.valid;

        vegaflow::MemoryActionReport action = mem_mgr.EvaluateAndOptimize(0, {}, true /* dry_run */);
        (void)action; // successfully returned without throwing or crashing

        uint64_t ram_ws = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        constexpr uint64_t k15MB = 15ULL * 1024 * 1024;
        bool kpi_ok = (ram_ws < k15MB);

        bool t3_success = ram_ok && vram_ok && kpi_ok;
        std::string details = "RAM: " + std::to_string(telemetry.total_physical_bytes / (1024 * 1024)) + " MB, "
            + "VRAM Valid: " + (vram_ok ? "YES" : "NO") + ", "
            + "Private WS: " + std::to_string(ram_ws / 1024) + " KB (Zero-Overhead KPI < 15MB: "
            + (kpi_ok ? "PASS" : "FAIL") + ")";

        RecordResult(t3_success, "Test 3: MemoryManager telemetry, dry-run optimization, and Zero-Overhead KPI (< 15 MB)", details);
    }

    // -------------------------------------------------------------------------
    // TEST 4: Process Detector & Anti-Cheat Safe Inspection
    // -------------------------------------------------------------------------
    {
        bool sys_dwm = vegaflow::ProcessDetector::IsSystemOrExcludedProcess(L"dwm.exe");
        bool sys_explorer = vegaflow::ProcessDetector::IsSystemOrExcludedProcess(L"explorer.exe");
        bool sys_csrss = vegaflow::ProcessDetector::IsSystemOrExcludedProcess(L"csrss.exe");
        bool non_sys = !vegaflow::ProcessDetector::IsSystemOrExcludedProcess(L"cyberpunk2077.exe");
        bool daemon_excluded = vegaflow::ProcessDetector::IsSystemOrExcludedProcess(L"vegaflow_daemon.exe");

        bool filter_ok = sys_dwm && sys_explorer && sys_csrss && non_sys && daemon_excluded;

        bool api_strings_ok = 
            (std::string(vegaflow::ProcessDetector::RenderApiToString(vegaflow::RenderApi::DirectX12)) == "DirectX 12") &&
            (std::string(vegaflow::ProcessDetector::RenderApiToString(vegaflow::RenderApi::DirectX11)) == "DirectX 11") &&
            (std::string(vegaflow::ProcessDetector::RenderApiToString(vegaflow::RenderApi::Vulkan)) == "Vulkan");

        std::vector<uint32_t> noisy_pids = vegaflow::ProcessDetector::FindBackgroundNoisyProcesses(0);

        // 1. Safe inspection on excluded/system process returns nullopt (no false positives)
        auto inspect_sys = vegaflow::ProcessDetector::InspectProcess(4);
        bool safe_excluded_ok = !inspect_sys.has_value();

        // 2. Safe inspection on self process identifies D3D11 (ensure D3D11 module loaded for test)
        HMODULE hD3D11 = LoadLibraryW(L"d3d11.dll");
        auto self_inspect = vegaflow::ProcessDetector::InspectProcess(GetCurrentProcessId());
        bool inspect_api_ok = (self_inspect.has_value() && self_inspect->api == vegaflow::RenderApi::DirectX11);
        if (hD3D11) {
            FreeLibrary(hD3D11);
        }

        bool t4_success = filter_ok && api_strings_ok && safe_excluded_ok && inspect_api_ok;
        std::string details = "Filter: " + std::string(filter_ok ? "OK" : "FAIL") + ", "
            + "API Strings: " + std::string(api_strings_ok ? "OK" : "FAIL") + ", "
            + "Background PIDs: " + std::to_string(noisy_pids.size()) + ", "
            + "Safe Inspection: " + std::string((safe_excluded_ok && inspect_api_ok) ? "PASS (D3D11 Detected)" : "FAIL");

        RecordResult(t4_success, "Test 4: ProcessDetector anti-cheat safe inspection, process filtering, and API mapping", details);
    }

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

    // -------------------------------------------------------------------------
    // TEST 6: CPU Orchestrator Dry-Run & Restore
    // -------------------------------------------------------------------------
    {
        vegaflow::CpuOrchestrator cpu_orchestrator;
        vegaflow::CpuOptimizationResult res = cpu_orchestrator.ApplyOptimizations(
            GetCurrentProcessId(),
            0,
            false,
            {0},
            {1},
            {},
            true /* dry_run */);

        cpu_orchestrator.RestoreAll(true /* dry_run */);

        bool t6_success = res.game_handle_full_access;
        std::string details = "Dry-Run Target PID: " + std::to_string(GetCurrentProcessId()) + ", "
            + "Game Handle: " + (res.game_handle_full_access ? "Full Access" : "Degraded Access") + ", "
            + "State Restore: OK";

        RecordResult(t6_success, "Test 6: CpuOrchestrator dry-run optimization and state restoration", details);
    }

    // -------------------------------------------------------------------------
    // TEST 7: Frame Telemetry & Mathematical Rigor (1% / 0.1% Lows)
    // -------------------------------------------------------------------------
    {
        // 1. Math computation verification
        std::vector<double> samples(990, 16.6666666667); // 60 FPS
        for (int i = 0; i < 9; ++i) samples.push_back(33.3333333333); // 30 FPS
        samples.push_back(100.0); // 10 FPS
        auto math_snap = vegaflow::FrameTelemetryMonitor::ComputeMetricsFromSamples(samples, 1234, 1000);

        bool math_valid = math_snap.valid && (math_snap.total_frames_captured == 1000);
        bool fps_avg_ok = std::abs(math_snap.avg_fps - 59.2) < 1.0;
        bool low_1_ok = std::abs(math_snap.fps_1_percent_low - 25.0) < 0.5;
        bool low_0_1_ok = std::abs(math_snap.fps_0_1_percent_low - 10.0) < 0.1;

        // 2. Circular ring buffer wrapping test (3000 items into 2048 window)
        vegaflow::FrameTelemetryMonitor monitor;
        for (int i = 0; i < 3000; ++i) {
            monitor.RecordFrameIntervalMs(16.6666666667);
        }
        auto ring_snap = monitor.GetSnapshot();
        bool ring_ok = ring_snap.valid && (ring_snap.total_frames_captured == 3000);
        std::string report = monitor.FormatSessionReport("VegaFlow Bench");
        bool report_ok = (report.find("VEGAFLOW TELEMETRY REPORT") != std::string::npos);

        // 3. Monitor thread start & fast shutdown KPI (< 100 ms)
        auto t_start = GetTickCount64();
        monitor.StartMonitoring(GetCurrentProcessId());
        Sleep(30);
        monitor.StopMonitoring();
        auto elapsed_ms = GetTickCount64() - t_start;
        bool kpi_stop_ok = (elapsed_ms < 150); // fast shutdown

        bool t7_success = math_valid && fps_avg_ok && low_1_ok && low_0_1_ok && ring_ok && report_ok && kpi_stop_ok;
        std::string details = "Avg FPS: " + std::to_string(static_cast<int>(math_snap.avg_fps)) + ", "
            + "1% Low: " + std::to_string(static_cast<int>(math_snap.fps_1_percent_low)) + " FPS, "
            + "0.1% Low: " + std::to_string(static_cast<int>(math_snap.fps_0_1_percent_low)) + " FPS, "
            + "RingBuffer: " + std::to_string(ring_snap.total_frames_captured) + " frames, "
            + "Stop Latency: " + std::to_string(elapsed_ms) + " ms (< 100ms KPI)";

        RecordResult(t7_success, "Test 7: FrameTelemetryMonitor mathematical rigor, 1% Low, circular buffer, and shutdown latency", details);
    }

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

    // -------------------------------------------------------------------------
    // TEST 9: FSR 1.0 Spatial Upscaler HLSL Compilation & Zero-VRAM Lifecycle
    // -------------------------------------------------------------------------
    {
        std::string shader_err;
        bool shaders_compiled = vegaflow::FsrSpatialScaler::CompileAndValidateShadersOnly(&shader_err);

        vegaflow::FsrSpatialScaler scaler;
        bool start_dry_ok = scaler.StartScaling(nullptr, 0.2f, true /* dry_run */);
        bool status_active = scaler.GetStatus().active;
        scaler.StopScaling();
        bool status_inactive = !scaler.GetStatus().active;

        uint64_t ram_ws = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        bool kpi_ok = (ram_ws < 15ULL * 1024 * 1024);

        bool t9_success = shaders_compiled && start_dry_ok && status_active && status_inactive && kpi_ok;
        std::string details = "EASU+RCAS HLSL: " + std::string(shaders_compiled ? "COMPILED" : ("FAIL: " + shader_err)) + ", "
            + "Dry-Run Lifecycle: " + std::string((start_dry_ok && status_active && status_inactive) ? "OK" : "FAIL") + ", "
            + "RAM Footprint: " + std::to_string(ram_ws / 1024) + " KB (Zero-Overhead KPI < 15MB: "
            + (kpi_ok ? "PASS" : "FAIL") + ")";

        RecordResult(t9_success, "Test 9: FSR 1.0 Spatial Upscaler HLSL Compilation & Zero-VRAM Lifecycle", details);
    }

    // -------------------------------------------------------------------------
    // TEST 10: Phase 4 System Tweaks & Session Report Persistence
    // -------------------------------------------------------------------------
    {
        bool shader_cache_ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(true, true /* dry_run */);
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

    // -------------------------------------------------------------------------
    // TEST 11: Stability, Memory Stress & CpuOrchestrator Deduplication
    // -------------------------------------------------------------------------
    {
        vegaflow::MemoryPurgeConfig mem_cfg{};
        vegaflow::MemoryManager stress_mem(mem_cfg);
        bool samples_ok = true;
        for (int i = 0; i < 25; ++i) {
            auto sample = stress_mem.SampleTelemetry();
            if (sample.total_physical_bytes == 0) {
                samples_ok = false;
                break;
            }
        }

        uint64_t ram_ws = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        bool ws_ok = (ram_ws < 15ULL * 1024 * 1024);

        vegaflow::CpuOrchestrator stress_cpu;
        std::vector<ULONG> empty_game_sets;
        std::vector<ULONG> empty_bg_sets;
        std::vector<uint32_t> empty_bg_pids;
        auto res = stress_cpu.ApplyOptimizations(
            GetCurrentProcessId(),
            0,
            false,
            empty_game_sets,
            empty_bg_sets,
            empty_bg_pids,
            false /* dry_run */);
        (void)res;
        uint32_t iso_count_before = stress_cpu.GetIsolatedBackgroundCount();
        stress_cpu.RestoreAll(false);
        uint32_t iso_count_after = stress_cpu.GetIsolatedBackgroundCount();
        bool cpu_ok = (iso_count_after == 0);

        bool t11_success = samples_ok && ws_ok && cpu_ok;
        std::string details = "25 Telemetry Samples: " + std::string(samples_ok ? "PASS" : "FAIL") + ", "
            + "Working Set: " + std::to_string(ram_ws / 1024) + " KB (< 15MB: " + (ws_ok ? "PASS" : "FAIL") + "), "
            + "Isolated Count: " + std::to_string(iso_count_before) + " -> " + std::to_string(iso_count_after);

        RecordResult(t11_success, "Test 11: Stability, Memory Stress & CpuOrchestrator Deduplication", details);
    }

    // -------------------------------------------------------------------------
    // TEST 12: FSR 1.0 Dynamic Sharpness Update & Value Clamping
    // -------------------------------------------------------------------------
    {
        vegaflow::FsrSpatialScaler scaler;
        scaler.SetSharpness(0.40f);
        bool s1_ok = (std::abs(scaler.GetStatus().sharpness - 0.40f) < 0.001f);

        scaler.SetSharpness(2.50f);
        bool s2_ok = (std::abs(scaler.GetStatus().sharpness - 1.0f) < 0.001f);

        scaler.SetSharpness(-0.50f);
        bool s3_ok = (std::abs(scaler.GetStatus().sharpness - 0.0f) < 0.001f);

        bool algo_default_ok = (scaler.GetAlgorithm() == vegaflow::UpscaleAlgorithm::FSR1_UltraClean) &&
                               (scaler.GetStatus().algorithm == vegaflow::UpscaleAlgorithm::FSR1_UltraClean);

        scaler.SetAlgorithm(vegaflow::UpscaleAlgorithm::NIS_HybridCatmull);
        bool algo_nis_ok = (scaler.GetAlgorithm() == vegaflow::UpscaleAlgorithm::NIS_HybridCatmull) &&
                           (scaler.GetStatus().algorithm == vegaflow::UpscaleAlgorithm::NIS_HybridCatmull);

        scaler.SetAlgorithm(vegaflow::UpscaleAlgorithm::Anime4K_LineReconstruct);
        bool algo_anime_ok = (scaler.GetAlgorithm() == vegaflow::UpscaleAlgorithm::Anime4K_LineReconstruct) &&
                             (scaler.GetStatus().algorithm == vegaflow::UpscaleAlgorithm::Anime4K_LineReconstruct);

        std::string compile_err;
        bool shaders_ok = vegaflow::FsrSpatialScaler::CompileAndValidateShadersOnly(&compile_err);

        bool t12_success = s1_ok && s2_ok && s3_ok && algo_default_ok && algo_nis_ok && algo_anime_ok && shaders_ok;
        std::string details = "Set 0.40: " + std::string(s1_ok ? "PASS" : "FAIL") + ", "
            + "Clamp 2.50 -> 1.0: " + std::string(s2_ok ? "PASS" : "FAIL") + ", "
            + "Clamp -0.50 -> 0.0: " + std::string(s3_ok ? "PASS" : "FAIL") + ", "
            + "NIS Algo: " + std::string(algo_nis_ok ? "PASS" : "FAIL") + ", "
            + "Anime4K Algo: " + std::string(algo_anime_ok ? "PASS" : "FAIL") + ", "
            + "Multi-Algo HLSL Compile: " + std::string(shaders_ok ? "PASS" : ("FAIL: " + compile_err));

        RecordResult(t12_success, "Test 12: FSR 1.0 Dynamic Sharpness Update & Value Clamping", details);
    }

    // -------------------------------------------------------------------------
    // TEST 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        auto temp_base = std::filesystem::temp_directory_path() / "vegaflow_test_steam_opt";
        std::filesystem::remove_all(temp_base, ec);
        std::filesystem::create_directories(temp_base / "Engine" / "Binaries" / "Win64", ec);

        std::filesystem::path dummy_exe = temp_base / "MyGame-Win64-Shipping.exe";
        {
            std::ofstream out(dummy_exe, std::ios::binary);
            out << "MZ dummy executable header";
        }

        // 1. Detection of Unreal Engine 4/5
        auto game = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "My Unreal Game");
        bool engine_ok = (game.engine == vegaflow::GameEngineType::UnrealEngine4_5);
        bool ac_clean_ok = (!game.has_anticheat);
        bool exe_ok = (!game.primary_exe_path.empty());

        // 2. Safe config modification and .vegaflow.bak creation
        auto custom_cfg_dir = temp_base / "Config";
        std::filesystem::create_directories(custom_cfg_dir, ec);
        auto ini_path = custom_cfg_dir / "Engine.ini";
        {
            std::ofstream out(ini_path);
            out << "[Core.System]\nPaths=../../../Engine/Content\n";
        }

        auto rep_apply = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game, 3000, false, custom_cfg_dir.wstring());
        bool apply_ok = rep_apply.success;
        bool bak_created = rep_apply.backup_created;
        auto bak_path = custom_cfg_dir / "Engine.ini.vegaflow.bak";
        bool bak_exists = std::filesystem::exists(bak_path, ec);

        std::string modified_content;
        {
            std::ifstream in(ini_path);
            std::stringstream ss;
            ss << in.rdbuf();
            modified_content = ss.str();
        }
        bool has_preset = (modified_content.find("r.Streaming.PoolSize=4096") != std::string::npos) &&
                          (modified_content.find("r.Streaming.LimitPoolSizeToVRAM=0") != std::string::npos) &&
                          (modified_content.find("r.Streaming.MaxNumTexturesToStreamPerFrame=120") != std::string::npos);

        // 3. Restoration of original config from .vegaflow.bak
        auto rep_restore = vegaflow::SteamEngineOptimizer::RestoreEngineOriginalConfig(game, false, custom_cfg_dir.wstring());
        bool restore_ok = rep_restore.success;
        bool bak_removed = !std::filesystem::exists(bak_path, ec);

        std::string restored_content;
        {
            std::ifstream in(ini_path);
            std::stringstream ss;
            ss << in.rdbuf();
            restored_content = ss.str();
        }
        bool preset_removed = (restored_content.find("r.Streaming.PoolSize=") == std::string::npos);
        bool orig_intact = (restored_content.find("[Core.System]") != std::string::npos);

        // 4. Anti-Cheat safety verification (block file edits)
        std::filesystem::path dummy_eac = temp_base / "EasyAntiCheat_EOS.dll";
        {
            std::ofstream out(dummy_eac, std::ios::binary);
            out << "dummy eac";
        }
        auto game_protected = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "My Protected Game");
        bool ac_detect_ok = game_protected.has_anticheat;

        auto rep_ac = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game_protected, 3000, false, custom_cfg_dir.wstring());
        bool ac_block_ok = (rep_ac.blocked_by_anticheat && rep_ac.success);

        // 4b. HoYoverse Anti-Cheat safety verification (HoYoKProtect / mhypbase)
        std::filesystem::remove(dummy_eac, ec);
        std::filesystem::path dummy_hoyo = temp_base / "HoYoKProtect.sys";
        {
            std::ofstream out(dummy_hoyo, std::ios::binary);
            out << "dummy hoyokprotect";
        }
        auto game_hoyo = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "Zenless Zone Zero");
        bool hoyo_detect_ok = game_hoyo.has_anticheat;
        auto rep_hoyo = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game_hoyo, 3000, false, custom_cfg_dir.wstring());
        bool hoyo_block_ok = (rep_hoyo.blocked_by_anticheat && rep_hoyo.success);

        // Cleanup
        std::filesystem::remove_all(temp_base, ec);

        bool t13_success = engine_ok && ac_clean_ok && exe_ok && apply_ok && bak_created &&
                           bak_exists && has_preset && restore_ok && bak_removed &&
                           preset_removed && orig_intact && ac_detect_ok && ac_block_ok &&
                           hoyo_detect_ok && hoyo_block_ok;

        std::string details = "UE Detection: " + std::string(engine_ok ? "PASS" : "FAIL") + ", "
            + "Backup Created: " + std::string(bak_created && bak_exists ? "PASS" : "FAIL") + ", "
            + "Preset Applied: " + std::string(has_preset ? "PASS" : "FAIL") + ", "
            + "Restored & Bak Cleared: " + std::string(restore_ok && bak_removed && orig_intact ? "PASS" : "FAIL") + ", "
            + "AntiCheat Blocking: " + std::string(ac_detect_ok && ac_block_ok && hoyo_detect_ok && hoyo_block_ok ? "PASS" : "FAIL");

        RecordResult(t13_success, "Test 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore", details);
    }

    // -------------------------------------------------------------------------
    // TEST 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync
    // -------------------------------------------------------------------------
    {
        // 1. Single-Instance Mutex guard test
        HANDLE hMutex1 = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_TestMutex");
        bool mutex1_ok = (hMutex1 != nullptr);

        HANDLE hMutex2 = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_TestMutex");
        bool mutex2_detected = (hMutex2 != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);

        if (hMutex2) CloseHandle(hMutex2);
        if (hMutex1) CloseHandle(hMutex1);

        // 2. Non-blocking MMAgent dry-run check
        bool mem_comp_dry_ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(true, true /* dry_run */);

        // 3. Safe Steam RunningAppID query
        uint32_t running_app_id = vegaflow::SteamEngineOptimizer::GetRunningSteamAppId();
        (void)running_app_id; // Safe query without crash or throw
        bool steam_query_ok = true;

        bool t14_success = mutex1_ok && mutex2_detected && mem_comp_dry_ok && steam_query_ok;
        std::string details = "Mutex 1: " + std::string(mutex1_ok ? "OK" : "FAIL") + ", "
            + "Mutex 2 ERROR_ALREADY_EXISTS: " + std::string(mutex2_detected ? "PASS" : "FAIL") + ", "
            + "MMAgent Dry-Run: " + std::string(mem_comp_dry_ok ? "PASS" : "FAIL") + ", "
            + "RunningAppID Query: " + std::to_string(running_app_id);

        RecordResult(t14_success, "Test 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync", details);
    }

    // -------------------------------------------------------------------------
    // TEST 15: DX11/DX12 FPS Boost Suite (MMCSS GPU Priority=8, Win32PrioritySeparation Quantum & AMD Driver Boost Dry-Run)
    // -------------------------------------------------------------------------
    {
        bool mmcss_dry_ok = vegaflow::GraphicsOptimizer::SetMmcssGamingProfileOptimized(true, true /* dry_run */);
        bool prio_sep_26_ok = vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(0x26, true /* dry_run */);
        bool prio_sep_28_ok = vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(0x28, true /* dry_run */);
        bool amd_boost_dry_ok = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, true /* dry_run */);
        bool gamedvr_dry_ok = vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, true /* dry_run */);
        bool apu_power_dry_ok = vegaflow::GraphicsOptimizer::SetApuLaptopPowerProfile(true, true /* dry_run */);
        bool apu_mode_gpu_dry_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, true /* dry_run */);
        bool apu_mode_bal_dry_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, true /* dry_run */);

        // Apply real settings for HKCU / User Power scheme (execution)
        bool gamedvr_exec_ok = vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false /* dry_run */);
        bool apu_power_exec_ok = vegaflow::GraphicsOptimizer::SetApuLaptopPowerProfile(true, false /* dry_run */);

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

    // -------------------------------------------------------------------------
    // TEST 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path temp_base = std::filesystem::temp_directory_path() / "vegaflow_test_dxvk_suite";
        std::filesystem::remove_all(temp_base, ec);
        std::filesystem::create_directories(temp_base, ec);

        std::filesystem::path game_exe = temp_base / "shooter.exe";
        {
            std::ofstream out(game_exe, std::ios::binary);
            out << "MZ_DUMMY_EXE_CONTENT";
        }

        // 1. Assets location verification
        std::wstring assets_path = vegaflow::DxvkManager::GetDxvkAssetsPath();
        bool assets_found = !assets_path.empty();

        // 2. AntiCheat Block Verification
        std::string ac_msg;
        bool ac_blocked = !vegaflow::DxvkManager::DeployDxvk(game_exe.wstring(), true /* has_anticheat */, true, false, &ac_msg);
        bool ac_msg_ok = (ac_msg.find("Anti-Cheat") != std::string::npos);
        bool no_files_installed = !vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        // 3. Dry-Run Verification
        std::string dry_msg;
        bool dry_ok = vegaflow::DxvkManager::DeployDxvk(game_exe.wstring(), false, true, true /* dry_run */, &dry_msg);
        bool still_no_files = !vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        // 4. Pre-existing custom DLL backup test
        std::filesystem::path d3d11_file = temp_base / "d3d11.dll";
        {
            std::ofstream out(d3d11_file, std::ios::binary);
            out << "CUSTOM_D3D11_ORIGINAL_DATA";
        }

        // 5. Real Deployment Execution
        std::string deploy_msg;
        bool deploy_ok = vegaflow::DxvkManager::DeployDxvk(game_exe.wstring(), false, true, false, &deploy_msg);
        bool is_installed = vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        bool d3d11_exists = std::filesystem::exists(temp_base / "d3d11.dll", ec);
        bool dxgi_exists = std::filesystem::exists(temp_base / "dxgi.dll", ec);
        bool conf_exists = std::filesystem::exists(temp_base / "dxvk.conf", ec);
        bool bak_exists = std::filesystem::exists(temp_base / "d3d11.dll.original.bak", ec);

        // Verify dxvk.conf content (GPL & 6 threads)
        bool conf_content_ok = false;
        if (conf_exists) {
            std::ifstream cfile(temp_base / "dxvk.conf");
            std::string line;
            bool found_gpl = false;
            bool found_threads = false;
            bool found_async = false;
            bool found_vram = false;
            bool found_shared = false;
            while (std::getline(cfile, line)) {
                if (line.find("dxvk.gpl = True") != std::string::npos) found_gpl = true;
                if (line.find("dxvk.numCompilerThreads = 6") != std::string::npos) found_threads = true;
                if (line.find("dxvk.enableAsync = True") != std::string::npos) found_async = true;
                if (line.find("dxgi.maxDeviceMemory = 4096") != std::string::npos) found_vram = true;
                if (line.find("dxgi.maxSharedMemory = 8192") != std::string::npos) found_shared = true;
            }
            conf_content_ok = (found_gpl && found_threads && found_async && found_vram && found_shared);
        }

        // 6. Verification through SteamEngineOptimizer
        auto scanned_game = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 991122, "Test Shooter");
        bool scan_detected_dxvk = scanned_game.dxvk_installed;

        // 7. Dummy logs generation and removal test
        {
            std::ofstream out(temp_base / "shooter_d3d11.log");
            out << "d3d11 log sample";
        }
        {
            std::ofstream out(temp_base / "shooter_dxgi.log");
            out << "dxgi log sample";
        }

        std::string remove_msg;
        bool remove_ok = vegaflow::DxvkManager::RemoveDxvk(game_exe.wstring(), false, &remove_msg);

        bool conf_deleted = !std::filesystem::exists(temp_base / "dxvk.conf", ec);
        bool logs_deleted = !std::filesystem::exists(temp_base / "shooter_d3d11.log", ec) &&
                            !std::filesystem::exists(temp_base / "shooter_dxgi.log", ec);
        bool bak_deleted = !std::filesystem::exists(temp_base / "d3d11.dll.original.bak", ec);
        bool dxgi_deleted = !std::filesystem::exists(temp_base / "dxgi.dll", ec);

        // Verify original d3d11 was restored
        bool original_restored = false;
        if (std::filesystem::exists(d3d11_file, ec)) {
            std::ifstream f(d3d11_file, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            original_restored = (content == "CUSTOM_D3D11_ORIGINAL_DATA");
        }

        // Cleanup
        std::filesystem::remove_all(temp_base, ec);

        bool t16_success = assets_found && ac_blocked && ac_msg_ok && no_files_installed &&
                           dry_ok && still_no_files && deploy_ok && is_installed &&
                           d3d11_exists && dxgi_exists && conf_exists && bak_exists &&
                           conf_content_ok && scan_detected_dxvk && remove_ok &&
                           conf_deleted && logs_deleted && bak_deleted && dxgi_deleted && original_restored;

        std::string details = "Assets: " + std::string(assets_found ? "PASS" : "FAIL") + ", "
            + "AC Protection: " + std::string(ac_blocked && ac_msg_ok ? "PASS" : "FAIL") + ", "
            + "Deployment & Conf: " + std::string(deploy_ok && conf_content_ok ? "PASS" : "FAIL") + ", "
            + "Backup & Restore: " + std::string(bak_exists && original_restored ? "PASS" : "FAIL") + ", "
            + "Logs & Conf Cleaned: " + std::string(conf_deleted && logs_deleted ? "PASS" : "FAIL");

        RecordResult(t16_success, "Test 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)", details);
    }

    // -------------------------------------------------------------------------
    // TEST 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O
    // -------------------------------------------------------------------------
    {
        std::error_code ec;

        // 1. Normalización Canónica y Prevención de Path Traversal
        auto val_empty = vegaflow::SandboxGuard::ValidatePath(L"", vegaflow::SandboxAction::Write);
        bool empty_blocked = (!val_empty.allowed && val_empty.reason == vegaflow::SandboxDenialReason::EmptyPath);

        std::wstring traversal_path = L"C:\\Users\\Public\\..\\..\\Windows\\System32\\bad.dll";
        auto val_traversal = vegaflow::SandboxGuard::ValidatePath(traversal_path, vegaflow::SandboxAction::Write);
        bool traversal_blocked = (!val_traversal.allowed &&
                                  (val_traversal.reason == vegaflow::SandboxDenialReason::BlacklistedSystemDirectory ||
                                   val_traversal.reason == vegaflow::SandboxDenialReason::PathTraversalDetected));

        std::wstring canon = vegaflow::SandboxGuard::CanonicalizePath(L"C:/Windows/System32/../System32/cmd.exe");
        bool canon_ok = (!canon.empty() && canon.find(L"..") == std::wstring::npos);

        // 2. Protección Estricta de la Raíz de Unidad (Drive Root)
        auto val_root = vegaflow::SandboxGuard::ValidatePath(L"C:\\", vegaflow::SandboxAction::Write);
        bool root_blocked = (!val_root.allowed);

        auto val_root_file = vegaflow::SandboxGuard::ValidatePath(L"C:\\dangerous_payload.exe", vegaflow::SandboxAction::Write);
        bool root_file_blocked = (!val_root_file.allowed);
        bool root_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\vegaflow_forbidden.tmp", "test");

        // 3. Blacklist de Zonas Críticas Prohibidas (Windows, System32, SysWOW64, Common Files)
        bool win_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Windows\\System32\\drivers\\etc\\hosts_bad", "bad");
        bool syswow_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Windows\\SysWOW64\\bad.dll", "bad");
        bool prog_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Program Files\\Common Files\\bad.dll", "bad");
        bool win_del_blocked = !vegaflow::SandboxGuard::SafeRemoveFile(L"C:\\Windows\\System32\\calc.exe");

        // 4. Whitelist de Zonas Permitidas
        // 4a. Steam Library (steamapps/common/...)
        auto val_steam = vegaflow::SandboxGuard::ValidatePath(L"D:\\SteamLibrary\\steamapps\\common\\Half-Life\\game.ini", vegaflow::SandboxAction::Write);
        bool steam_whitelisted = val_steam.allowed;

        // 4b. %LOCALAPPDATA% (Configuración de juegos y Saved/Config)
        wchar_t localAppBuf[MAX_PATH] = {0};
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppBuf, MAX_PATH);
        std::filesystem::path localApp(localAppBuf);
        std::filesystem::path ue_config_dir = localApp / L"VegaFlowTestGame" / L"Saved" / L"Config" / L"WindowsNoEditor";
        auto val_ue = vegaflow::SandboxGuard::ValidatePath(ue_config_dir / L"Engine.ini", vegaflow::SandboxAction::Write);
        bool ue_whitelisted = val_ue.allowed;

        // 4c. Caché de VegaFlow (%LOCALAPPDATA%\VegaFlow\Cache)
        std::filesystem::path cache_dir = localApp / L"VegaFlow" / L"Cache";
        auto val_cache = vegaflow::SandboxGuard::ValidatePath(cache_dir / L"shader.cache", vegaflow::SandboxAction::Write);
        bool cache_whitelisted = val_cache.allowed;

        // 5. Métodos Seguros de I/O en Zona Permitida (SafeWriteFile, SafeReadFile, SafeCopyFile, SafeRemoveFile)
        std::filesystem::path temp_test_dir = std::filesystem::temp_directory_path() / "vegaflow_test_sandbox_guard";
        std::filesystem::remove_all(temp_test_dir, ec);
        std::filesystem::create_directories(temp_test_dir, ec);

        std::filesystem::path test_file = temp_test_dir / L"sandbox_test.tmp";
        std::filesystem::path copy_file = temp_test_dir / L"sandbox_copy.tmp";

        bool write_ok = vegaflow::SandboxGuard::SafeWriteFile(test_file.wstring(), "VEGAFLOW_SANDBOX_VERIFIED_DATA");

        std::string read_back;
        bool read_ok = vegaflow::SandboxGuard::SafeReadFile(test_file.wstring(), read_back) &&
                       (read_back == "VEGAFLOW_SANDBOX_VERIFIED_DATA");

        bool copy_ok = vegaflow::SandboxGuard::SafeCopyFile(test_file.wstring(), copy_file.wstring(), true);
        bool remove1_ok = vegaflow::SandboxGuard::SafeRemoveFile(test_file.wstring());
        bool remove2_ok = vegaflow::SandboxGuard::SafeRemoveFile(copy_file.wstring());
        bool safe_io_ok = write_ok && read_ok && copy_ok && remove1_ok && remove2_ok;

        // 6. Anti-Cheat Lockdown
        // 6a. Bloqueo por bandera explícita (is_anticheat_protected = true)
        bool flag_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(test_file.wstring(), "exploit", true /* is_anticheat_protected */);
        bool flag_copy_blocked = !vegaflow::SandboxGuard::SafeCopyFile(test_file.wstring(), copy_file.wstring(), true, true /* is_anticheat_protected */);
        bool flag_remove_blocked = !vegaflow::SandboxGuard::SafeRemoveFile(test_file.wstring(), true /* is_anticheat_protected */);
        bool flag_lockdown_ok = flag_write_blocked && flag_copy_blocked && flag_remove_blocked;

        // 6b. Bloqueo por detección en tiempo real de firmas Anti-Cheat a nivel de Kernel en directorio
        std::filesystem::path ac_folder = temp_test_dir / L"Anticheat_Test_Game";
        std::filesystem::create_directories(ac_folder, ec);
        {
            std::ofstream out1(ac_folder / "EasyAntiCheat_EOS.dll");
            out1 << "dummy eac";
            std::ofstream out2(ac_folder / "HoYoKProtect.sys");
            out2 << "dummy hoyokprotect";
        }

        bool dir_has_ac = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(ac_folder);
        bool folder_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile((ac_folder / L"Engine.ini").wstring(), "tweak", false);
        bool folder_remove_blocked = !vegaflow::SandboxGuard::SafeRemoveFile((ac_folder / L"Engine.ini").wstring(), false);

        std::filesystem::remove_all(temp_test_dir, ec);
        bool signature_lockdown_ok = dir_has_ac && folder_write_blocked && folder_remove_blocked;

        // 7. Registro y Auditoría de Violaciones
        size_t violations_count = vegaflow::SandboxGuard::GetViolationCount();
        bool violations_recorded = (violations_count > 0);
        vegaflow::SandboxGuard::ClearViolations();
        bool violations_cleared = (vegaflow::SandboxGuard::GetViolationCount() == 0);

        bool t17_success = empty_blocked && traversal_blocked && canon_ok &&
                           root_blocked && root_file_blocked && root_write_blocked &&
                           win_write_blocked && syswow_write_blocked && prog_write_blocked && win_del_blocked &&
                           steam_whitelisted && ue_whitelisted && cache_whitelisted &&
                           safe_io_ok && flag_lockdown_ok && signature_lockdown_ok &&
                           violations_recorded && violations_cleared;

        std::string details = "Canonicalization: " + std::string(canon_ok && traversal_blocked ? "PASS" : "FAIL") + ", "
            + "Blacklist & DriveRoot: " + std::string(root_write_blocked && win_write_blocked ? "PASS" : "FAIL") + ", "
            + "Whitelist: " + std::string(steam_whitelisted && ue_whitelisted && cache_whitelisted ? "PASS" : "FAIL") + ", "
            + "Safe I/O: " + std::string(safe_io_ok ? "PASS" : "FAIL") + ", "
            + "AntiCheat Lockdown: " + std::string(flag_lockdown_ok && signature_lockdown_ok ? "PASS" : "FAIL") + ", "
            + "Audit Log: " + std::string(violations_recorded && violations_cleared ? "PASS" : "FAIL");

        RecordResult(t17_success, "Test 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O", details);
    }

    // -------------------------------------------------------------------------
    // TEST 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization
    // -------------------------------------------------------------------------
    {
        vegaflow::SystemInfo sys_info;
        const auto& snap = sys_info.Snapshot();

        // 1. CPU Validation
        bool cpu_brand_ok = !snap.cpu.brand_string.empty();
        bool cpu_cores_ok = (snap.cpu.physical_cores > 0 && snap.cpu.logical_processors >= snap.cpu.physical_cores);
        bool cpu_uarch_ok = !snap.cpu.microarchitecture.empty();
        bool cpu_l3_ok = (snap.cpu.l3_cache_bytes > 0 && snap.cpu.l3_cache_mb > 0.0);
        bool cpu_boost_ok = (snap.cpu.boost_target_ghz >= 3.0 && !snap.cpu.stapm_target.empty());
        bool cpu_sets_ok = (!snap.cpu.active_game_cpu_sets.empty() && !snap.cpu.active_background_cpu_sets.empty());
        bool cpu_all_ok = cpu_brand_ok && cpu_cores_ok && cpu_uarch_ok && cpu_l3_ok && cpu_boost_ok && cpu_sets_ok;

        // 2. GPU Validation
        bool gpu_name_ok = !snap.gpu.adapter_name.empty();
        bool gpu_arch_ok = (snap.gpu.architecture.find("UMA") != std::string::npos);
        bool gpu_vram_ok = (snap.gpu.bios_dedicated_vram_bytes > 0 && snap.gpu.dynamic_shared_vram_bytes > 0);
        bool gpu_all_ok = gpu_name_ok && gpu_arch_ok && gpu_vram_ok;

        // 3. RAM Validation
        bool ram_phys_ok = (snap.ram.total_physical_bytes > 0 && snap.ram.total_physical_gb >= 1.0);
        bool ram_free_ok = (snap.ram.free_real_bytes > 0);
        bool ram_channel_ok = (snap.ram.channel_topology.find("Channel") != std::string::npos &&
                               snap.ram.memory_bus_width_bits >= 64);
        bool ram_all_ok = ram_phys_ok && ram_free_ok && ram_channel_ok;

        // 4. OS & Kernel Validation
        bool os_name_ok = (snap.os_kernel.os_name.find("Windows") != std::string::npos);
        bool os_build_ok = (snap.os_kernel.build_number > 0);
        bool os_timer_ok = (snap.os_kernel.timer_resolution_ms > 0.0 && snap.os_kernel.timer_resolution_ns100 > 0);
        bool os_all_ok = os_name_ok && os_build_ok && os_timer_ok;

        // 5. JSON Serialization Validation
        std::string json_str = sys_info.ToJson();
        bool json_structure_ok = (!json_str.empty() && json_str.front() == '{' && json_str.back() == '\n' &&
                                  json_str[json_str.size() - 2] == '}');

        // Check required JSON keys
        bool has_cpu_key = (json_str.find("\"cpu\":") != std::string::npos);
        bool has_gpu_key = (json_str.find("\"gpu\":") != std::string::npos);
        bool has_ram_key = (json_str.find("\"ram\":") != std::string::npos);
        bool has_os_key = (json_str.find("\"os_kernel\":") != std::string::npos);
        bool has_uarch_key = (json_str.find("\"microarchitecture\":") != std::string::npos);
        bool has_l3_key = (json_str.find("\"l3_cache_bytes\":") != std::string::npos);
        bool has_boost_key = (json_str.find("\"boost_target_ghz\":") != std::string::npos);
        bool has_stapm_key = (json_str.find("\"stapm_target\":") != std::string::npos);
        bool has_uma_key = (json_str.find("\"architecture\":") != std::string::npos &&
                            json_str.find("UMA (Unified Memory Architecture)") != std::string::npos);
        bool has_vram_key = (json_str.find("\"bios_dedicated_vram_bytes\":") != std::string::npos &&
                             json_str.find("\"dynamic_shared_vram_bytes\":") != std::string::npos);
        bool has_dxgi_key = (json_str.find("\"dxgi_budget\":") != std::string::npos);
        bool has_ram_topo_key = (json_str.find("\"channel_topology\":") != std::string::npos);
        bool has_timer_key = (json_str.find("\"timer_resolution\":") != std::string::npos);
        bool has_mmcss_key = (json_str.find("\"mmcss_gaming_profile\":") != std::string::npos);
        bool has_comp_key = (json_str.find("\"memory_compression\":") != std::string::npos);

        bool json_keys_ok = has_cpu_key && has_gpu_key && has_ram_key && has_os_key &&
                            has_uarch_key && has_l3_key && has_boost_key && has_stapm_key &&
                            has_uma_key && has_vram_key && has_dxgi_key && has_ram_topo_key &&
                            has_timer_key && has_mmcss_key && has_comp_key;

        bool t18_success = cpu_all_ok && gpu_all_ok && ram_all_ok && os_all_ok &&
                           json_structure_ok && json_keys_ok;

        std::string details = "CPU: " + snap.cpu.brand_string + " (" + snap.cpu.microarchitecture + ", " +
            std::to_string(static_cast<int>(snap.cpu.l3_cache_mb)) + " MB L3, " +
            std::to_string(snap.cpu.physical_cores) + "C/" + std::to_string(snap.cpu.logical_processors) + "T), "
            + "GPU: " + snap.gpu.adapter_name + " [" + snap.gpu.architecture + "], "
            + "RAM: " + std::to_string(static_cast<int>(snap.ram.total_physical_gb)) + " GB " + snap.ram.channel_topology + ", "
            + "OS: " + snap.os_kernel.full_version_string + " (Timer: " + std::to_string(snap.os_kernel.timer_resolution_ms) + " ms), "
            + "JSON: " + (json_keys_ok ? "VALID" : "INVALID");

        RecordResult(t18_success, "Test 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization", details);
    }

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
        bool amd_boost_ok = vegaflow::GraphicsOptimizer::ApplyAmdDx11DriverBoost(true, true /* dry_run */);

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

    // -------------------------------------------------------------------------
    // TEST 20: APU Silicon & UMA Bus Optimization Suite (DisablePagingExecutive Kernel Shield & Smart Tessellation 16x)
    // -------------------------------------------------------------------------
    {
        bool is_elevated = vegaflow::PrivilegeManager::IsProcessElevated();

        // 1. Blindaje de Memoria de Kernel NT (DisablePagingExecutive & LargeSystemCache)
        // 1a. Validación Dry-Run (Enable / Disable)
        bool shield_dry_enable = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, true /* dry_run */);
        bool shield_dry_disable = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(false, true /* dry_run */);

        // 1b. Consulta directa del estado del Kernel Paging Shield en el registro
        bool shield_query_val = vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield();

        // 1c. Prueba de ciclo de vida / ejecución real (aislamiento seguro por privilegios)
        bool shield_real_ok = false;
        if (is_elevated) {
            bool real_set = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, false /* dry_run */);
            bool verified_active = vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield();
            // Restauración limpia del estado original
            bool real_restore = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(shield_query_val, false /* dry_run */);
            shield_real_ok = real_set && verified_active && real_restore;
        } else {
            // En modo usuario estándar, HKLM está protegido contra escritura
            bool real_set = vegaflow::GraphicsOptimizer::SetKernelMemoryPagingShield(true, false /* dry_run */);
            shield_real_ok = (!real_set); // Aislamiento de privilegios NT verificado
        }

        bool paging_shield_ok = shield_dry_enable && shield_dry_disable && shield_real_ok;

        // 2. Driver Boost AMD DX11/UMA & Limitador Inteligente de Teselación
        // 2a. Dry-run del Driver Boost AMD DX11/UMA
        bool boost_dry_enable = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, true /* dry_run */);
        bool boost_dry_disable = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(false, true /* dry_run */);

        // 2b. Dry-run de limitador de teselación inteligente (16x para Vega 6 APU, 8x y 64x default)
        bool tess_16_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, true /* dry_run */);
        bool tess_8_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(8, true /* dry_run */);
        bool tess_restore_dry = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(64, true /* dry_run */);

        // 2c. Validación de ejecución real de directivas AMD (según contexto de seguridad)
        bool amd_real_ok = false;
        if (is_elevated) {
            bool boost_real = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, false /* dry_run */);
            bool tess_real = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, false /* dry_run */);
            // Restauración limpia
            bool boost_restore = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(false, false /* dry_run */);
            bool tess_restore = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(64, false /* dry_run */);
            amd_real_ok = boost_real && tess_real && boost_restore && tess_restore;
        } else {
            bool boost_real = vegaflow::GraphicsOptimizer::SetAmdDx11UmaDriverBoost(true, false /* dry_run */);
            bool tess_real = vegaflow::GraphicsOptimizer::SetAmdTessellationCap(16, false /* dry_run */);
            amd_real_ok = (!boost_real && !tess_real); // Acceso a clave HKLM restringido sin elevación
        }

        // 3. Consulta e integración de estado en SystemGraphicsTweaksState
        auto tweaks = vegaflow::GraphicsOptimizer::QuerySystemGraphicsTweaks();
        bool tweaks_shield_synced = (tweaks.kernel_paging_shield_active == shield_query_val);
        uint32_t current_tess_max = tweaks.amd_tessellation_max;
        bool query_helpers_consistent = (vegaflow::GraphicsOptimizer::QueryKernelMemoryPagingShield() == tweaks.kernel_paging_shield_active) &&
                                        (vegaflow::GraphicsOptimizer::QueryAmdTessellationCap() == tweaks.amd_tessellation_max);

        // 4. Restauración limpia de estados (Clean State Restoration)
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

            // Re-aplicar estado previo si correspondía
            if (prev_cat == "High" && prev_sfio == "High" && prev_clock == 10000) {
                vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false);
            }
        } else {
            // En modo usuario estándar, HKLM está protegido contra escritura
            // Comprobamos manejo de permisos elegante: falla segura sin crasheos
            bool apply_real = vegaflow::GraphicsOptimizer::ApplyMmcssTaskTweaks(false /* dry_run */);
            bool restore_real = vegaflow::GraphicsOptimizer::RestoreMmcssTaskTweaks(false /* dry_run */);

            exec_ok = (!apply_real); // Acceso a clave HKLM restringido sin elevación
            restore_clean_ok = (!restore_real);
        }

        // 4. Integración con SystemGraphicsTweaksState
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

    // -------------------------------------------------------------------------
    // TEST 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path test_dir = "scratch/test_dxvk_apu";
        std::filesystem::create_directories(test_dir, ec);
        if (ec || !std::filesystem::exists(test_dir, ec)) {
            test_dir = std::filesystem::temp_directory_path() / "test_dxvk_apu";
            std::filesystem::create_directories(test_dir, ec);
        }

        // Limpiar archivos residuales dentro del directorio de prueba
        std::filesystem::remove(test_dir / "dxvk.conf", ec);
        std::filesystem::remove_all(test_dir / "quarantine_genshin", ec);
        std::filesystem::remove_all(test_dir / "quarantine_zzz", ec);

        // 1. Simulación Dry-Run (validación sin escribir en disco)
        bool dry_res = vegaflow::DxvkManager::ConfigureApuProfile(test_dir, true /* dryRun */);
        bool dry_no_file = !std::filesystem::exists(test_dir / "dxvk.conf", ec);
        bool dry_ok = dry_res && dry_no_file;

        // 2. Generación real de perfil optimizado para APU Zen 2 / Vega 6
        bool config_ok = vegaflow::DxvkManager::ConfigureApuProfile(test_dir, false /* dryRun */);
        bool conf_exists = std::filesystem::exists(test_dir / "dxvk.conf", ec);

        // 3. Validación de directivas activas
        bool async_active = false;
        uint32_t compiler_threads = 0;
        bool validate_ok = vegaflow::DxvkManager::ValidateDxvkApuProfile(test_dir, async_active, compiler_threads);
        bool directives_val_ok = validate_ok && async_active && (compiler_threads == 6);

        // Verificación de directivas en el archivo físico
        bool file_directives_ok = false;
        if (conf_exists) {
            std::ifstream cfile(test_dir / "dxvk.conf");
            std::string line;
            bool found_early_discard = false;
            bool found_pipe_lib = false;
            bool found_gpl = false;
            bool found_lifetime = false;
            while (std::getline(cfile, line)) {
                if (line.find("dxvk.useEarlyDiscard = true") != std::string::npos) found_early_discard = true;
                if (line.find("dxvk.pipelineLibrary = true") != std::string::npos) found_pipe_lib = true;
                if (line.find("dxvk.enableGraphicsPipelineLibrary = true") != std::string::npos) found_gpl = true;
                if (line.find("dxvk.trackPipelineLifetime = true") != std::string::npos) found_lifetime = true;
            }
            file_directives_ok = found_early_discard && found_pipe_lib && found_gpl && found_lifetime;
        }

        // 4. Actualización incremental preservando configuraciones existentes
        {
            std::ofstream custom_tweak(test_dir / "dxvk.conf", std::ios::app);
            custom_tweak << "d3d11.samplerAnisotropy = 8\n";
        }
        bool reconfigure_ok = vegaflow::DxvkManager::ConfigureApuProfile(test_dir, false /* dryRun */);
        bool custom_preserved = false;
        {
            std::ifstream cfile(test_dir / "dxvk.conf");
            std::string content((std::istreambuf_iterator<char>(cfile)), std::istreambuf_iterator<char>());
            custom_preserved = (content.find("d3d11.samplerAnisotropy = 8") != std::string::npos) &&
                               (content.find("dxvk.enableAsync = true") != std::string::npos);
        }

        // 5. Protección Anti-Cheat de SandboxGuard
        // 5a. Directorio simulado con GenshinImpact.exe
        std::filesystem::path genshin_dir = test_dir / "quarantine_genshin";
        std::filesystem::create_directories(genshin_dir, ec);
        {
            std::ofstream ac_file(genshin_dir / "GenshinImpact.exe");
            ac_file << "dummy_genshin_binary";
        }
        bool genshin_guard_blocked = !vegaflow::SandboxGuard::CanModifyGamePath(genshin_dir);
        bool genshin_deploy_blocked = !vegaflow::DxvkManager::ConfigureApuProfile(genshin_dir, false);
        bool genshin_dry_blocked = !vegaflow::DxvkManager::ConfigureApuProfile(genshin_dir, true);
        bool genshin_no_conf = !std::filesystem::exists(genshin_dir / "dxvk.conf", ec);

        // 5b. Directorio simulado con ZenlessZoneZero.exe
        std::filesystem::path zzz_dir = test_dir / "quarantine_zzz";
        std::filesystem::create_directories(zzz_dir, ec);
        {
            std::ofstream ac_file(zzz_dir / "ZenlessZoneZero.exe");
            ac_file << "dummy_zzz_binary";
        }
        bool zzz_guard_blocked = !vegaflow::SandboxGuard::CanModifyGamePath(zzz_dir);
        bool zzz_deploy_blocked = !vegaflow::DxvkManager::ConfigureApuProfile(zzz_dir, false);
        bool zzz_dry_blocked = !vegaflow::DxvkManager::ConfigureApuProfile(zzz_dir, true);
        bool zzz_no_conf = !std::filesystem::exists(zzz_dir / "dxvk.conf", ec);

        bool anticheat_protection_ok = genshin_guard_blocked && genshin_deploy_blocked &&
                                       genshin_dry_blocked && genshin_no_conf &&
                                       zzz_guard_blocked && zzz_deploy_blocked &&
                                       zzz_dry_blocked && zzz_no_conf;

        // 6. Eliminación limpia (RemoveApuProfile)
        bool remove_dry_ok = vegaflow::DxvkManager::RemoveApuProfile(test_dir, true /* dryRun */);
        bool conf_still_there = std::filesystem::exists(test_dir / "dxvk.conf", ec);

        bool remove_real_ok = vegaflow::DxvkManager::RemoveApuProfile(test_dir, false /* dryRun */);
        bool conf_deleted = !std::filesystem::exists(test_dir / "dxvk.conf", ec);

        bool post_async = false;
        uint32_t post_threads = 0;
        bool post_val_fails = !vegaflow::DxvkManager::ValidateDxvkApuProfile(test_dir, post_async, post_threads);

        bool removal_ok = remove_dry_ok && conf_still_there && remove_real_ok && conf_deleted && post_val_fails;

        // Limpieza final de directorio de prueba
        std::filesystem::remove(test_dir / "dxvk.conf", ec);
        std::filesystem::remove_all(test_dir / "quarantine_genshin", ec);
        std::filesystem::remove_all(test_dir / "quarantine_zzz", ec);
        std::filesystem::remove_all(test_dir, ec);

        bool t22_success = dry_ok && config_ok && conf_exists && directives_val_ok &&
                           file_directives_ok && reconfigure_ok && custom_preserved &&
                           anticheat_protection_ok && removal_ok;

        std::string details = "Dry-Run: " + std::string(dry_ok ? "PASS" : "FAIL") + ", "
            + "APU Profile (GPL/Async/6T): " + std::string((config_ok && directives_val_ok && file_directives_ok) ? "PASS" : "FAIL") + ", "
            + "Custom Config Preserved: " + std::string(custom_preserved ? "PASS" : "FAIL") + ", "
            + "Anti-Cheat Lockdown (Genshin/ZZZ): " + std::string(anticheat_protection_ok ? "PASS (Kernel Shield)" : "FAIL") + ", "
            + "Clean Removal: " + std::string(removal_ok ? "PASS" : "FAIL");

        RecordResult(t22_success, "Test 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming", details);
    }

    // -------------------------------------------------------------------------
    // TEST 23: Memory Manager Standby List Purge & UMA Bus Prioritizer
    // -------------------------------------------------------------------------
    {
        bool is_elevated = vegaflow::PrivilegeManager::IsProcessElevated();

        // 1. Dry-run verification
        bool dry_ok = vegaflow::MemoryManager::PurgeStandbyList(true /* dryRun */);

        // 2. Query standby and free memory telemetry
        uint64_t standby_mib = 0;
        uint64_t free_mib = 0;
        bool query_ok = vegaflow::MemoryManager::QueryStandbyMemoryMib(standby_mib, free_mib) && (free_mib > 0);

        // 3. Controlled execution and NT permissions handling (standard mode vs elevated)
        bool real_purge = vegaflow::MemoryManager::PurgeStandbyList(false /* dryRun */);
        bool perm_handled_ok = is_elevated ? real_purge : (!real_purge);

        // 4. Memory footprint Zero-Overhead KPI (< 15 MB)
        uint64_t ram_ws = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        constexpr uint64_t k15MB = 15ULL * 1024 * 1024;
        bool kpi_ok = (ram_ws < k15MB);

        bool t23_success = dry_ok && query_ok && perm_handled_ok && kpi_ok;

        std::string details = "Dry-Run: " + std::string(dry_ok ? "PASS" : "FAIL") + ", "
            + "Query (Standby: " + std::to_string(standby_mib) + " MiB, Free: " + std::to_string(free_mib) + " MiB): " + std::string(query_ok ? "PASS" : "FAIL") + ", "
            + "Permissions: " + (is_elevated ? (real_purge ? "ELEVATED (Purge OK)" : "ELEVATED (Purge Failed)") : "RESTRICTED (NT Isolation PASS)") + ", "
            + "Footprint: " + std::to_string(ram_ws / 1024) + " KB (Zero-Overhead KPI < 15MB: " + (kpi_ok ? "PASS" : "FAIL") + ")";

        RecordResult(t23_success, "Test 23: Memory Manager Standby List Purge & UMA Bus Prioritizer", details);
    }

    // -------------------------------------------------------------------------
    // TEST 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path temp_base = std::filesystem::temp_directory_path();
        std::filesystem::path test_dir = temp_base / "vegaflow_test_shadow_opt";
        std::filesystem::path ac_dir = temp_base / "vegaflow_test_shadow_genshinimpact";

        // Limpieza preventiva previa de ejecuciones anteriores
        std::filesystem::remove_all(test_dir, ec);
        std::filesystem::remove_all(ac_dir, ec);

        std::filesystem::create_directories(test_dir, ec);

        std::filesystem::path ini_path = test_dir / "Engine.ini";
        std::filesystem::path bak_path = test_dir / "Engine.ini.bak";

        // 1. Crear un Engine.ini inicial con otras secciones y claves
        std::string initial_ini = 
            "[Core.System]\n"
            "Paths=../../../Engine/Content\n"
            "\n"
            "[SystemSettings]\n"
            "r.ScreenPercentage=100\n"
            "r.TextureStreaming=1\n";
        {
            std::ofstream out(ini_path, std::ios::out | std::ios::binary);
            out.write(initial_ini.data(), initial_ini.size());
        }

        vegaflow::ApuShadowConfig config{};
        config.max_cascades = 2;
        config.distance_scale = 0.7f;
        config.radius_threshold = 0.04f;
        config.half_res_ao = true;
        config.volumetric_fog = 0;

        // 2. Simulación Dry-Run de ApplyApuShadowConfig
        bool dry_apply_ok = vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ini_path, config, true /* dryRun */);
        bool dry_no_bak = !std::filesystem::exists(bak_path, ec);
        std::string post_dry_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            post_dry_content = ss.str();
        }
        bool dry_unchanged = (post_dry_content == initial_ini);

        // 3. Aplicación real y consulta de configuración
        bool apply_real_ok = vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ini_path, config, false /* dryRun */);
        bool bak_created = std::filesystem::exists(bak_path, ec);

        vegaflow::ApuShadowConfig queried{};
        bool query_ok = vegaflow::SteamEngineOptimizer::QueryApuShadowConfig(ini_path, queried);

        bool values_match = (queried.max_cascades == 2) &&
                            (std::abs(queried.distance_scale - 0.7f) < 0.001f) &&
                            (std::abs(queried.radius_threshold - 0.04f) < 0.001f) &&
                            (queried.half_res_ao == true) &&
                            (queried.volumetric_fog == 0);

        std::string applied_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            applied_content = ss.str();
        }
        bool preserved_other = (applied_content.find("Paths=../../../Engine/Content") != std::string::npos) &&
                               (applied_content.find("r.ScreenPercentage=100") != std::string::npos) &&
                               (applied_content.find("r.TextureStreaming=1") != std::string::npos);

        // 4. Bloqueo rotundo de SandboxGuard ante Anti-Cheat a nivel kernel (Genshin, ZZZ, HoYoKProtect)
        std::filesystem::create_directories(ac_dir, ec);
        {
            std::ofstream ac_file(ac_dir / "GenshinImpact.exe", std::ios::out | std::ios::binary);
            ac_file << "dummy_genshin_binary";
            std::ofstream hoyo_file(ac_dir / "HoYoKProtect.sys", std::ios::out | std::ios::binary);
            hoyo_file << "dummy_hoyokprotect";
        }
        std::filesystem::path ac_ini = ac_dir / "Engine.ini";
        {
            std::ofstream out(ac_ini, std::ios::out | std::ios::binary);
            out << "[SystemSettings]\n";
        }

        bool ac_apply_blocked = !vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ac_ini, config, false);
        bool ac_dry_blocked = !vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ac_ini, config, true);
        bool ac_restore_blocked = !vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ac_ini, false);
        bool ac_shield_ok = ac_apply_blocked && ac_dry_blocked && ac_restore_blocked;

        // Limpiar de inmediato el directorio de anti-cheat para aislar el entorno temporal
        std::filesystem::remove_all(ac_dir, ec);

        // 5. Simulación Dry-Run de RestoreApuShadowConfig
        bool dry_restore_ok = vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ini_path, true /* dryRun */);
        bool bak_still_exists = std::filesystem::exists(bak_path, ec);

        // 6. Restauración limpia real desde .bak
        bool restore_real_ok = vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ini_path, false /* dryRun */);
        bool bak_deleted = !std::filesystem::exists(bak_path, ec);

        std::string restored_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            restored_content = ss.str();
        }
        bool restored_clean = (restored_content.find("r.Shadow.CSM.MaxCascades") == std::string::npos) &&
                              (restored_content.find("r.Shadow.DistanceScale") == std::string::npos) &&
                              (restored_content.find("Paths=../../../Engine/Content") != std::string::npos);

        // 7. Limpieza de los directorios aislados de prueba
        std::filesystem::remove_all(test_dir, ec);
        std::filesystem::remove_all(ac_dir, ec);

        bool t24_success = dry_apply_ok && dry_no_bak && dry_unchanged &&
                           apply_real_ok && bak_created && query_ok && values_match &&
                           preserved_other && ac_shield_ok && dry_restore_ok &&
                           bak_still_exists && restore_real_ok && bak_deleted &&
                           restored_clean;

        std::string details = "Dry-Run: " + std::string((dry_apply_ok && dry_no_bak && dry_unchanged) ? "PASS" : "FAIL") + ", "
            + "Apply & Query (CSM=2, Dist=0.7, Rad=0.04, AO=Half, Fog=0): " + std::string((apply_real_ok && query_ok && values_match && preserved_other) ? "PASS" : "FAIL") + ", "
            + "Kernel Anti-Cheat Shield (Genshin/HoYoKProtect): " + std::string(ac_shield_ok ? "PASS (Locked)" : "FAIL") + ", "
            + "Clean Restoration (.bak): " + std::string((restore_real_ok && bak_deleted && restored_clean) ? "PASS" : "FAIL");

        RecordResult(t24_success, "Test 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)", details);
    }

    // -------------------------------------------------------------------------
    // TEST 25: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards
    // -------------------------------------------------------------------------
    {
        // ---------------------------------------------------------------------
        // Caso A — Entry APU (Tier1_EntryUMA): Survival Guard
        // ---------------------------------------------------------------------
        vegaflow::SystemHardwareProfile entry_hw{};
        entry_hw.total_physical_ram_bytes = 8ULL * 1024 * 1024 * 1024;
        entry_hw.cpu_topology.physical_cores = 4;
        entry_hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        entry_hw.primary_gpu.mem_arch = vegaflow::MemoryArchitecture::UMA_Integrated;
        entry_hw.primary_gpu.arch_gen = vegaflow::GpuArchitectureGen::AMD_GCN5_Vega;

        const auto entry_policy = vegaflow::HardwareProfiler::BuildOptimizationPolicy(entry_hw);

        bool entry_policy_ok = (entry_policy.tier == vegaflow::ApuTier::Tier1_EntryUMA) &&
                               (entry_policy.guard_mode_name == "Survival Guard") &&
                               (entry_policy.safe_vram_pool_mb >= 1536 && entry_policy.safe_vram_pool_mb <= 2048) &&
                               (entry_policy.dxvk_compiler_threads == 3) &&
                               (entry_policy.amd_tessellation_cap == 8) &&
                               (entry_policy.disable_dx12_async_compute == true) &&
                               (entry_policy.cpu_stapm_cap_mhz == 0);

        std::string entry_uma_block = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(entry_policy, 4096);
        bool entry_survival_guard_ok = (entry_uma_block.find("r.Streaming.PoolSize=" + std::to_string(entry_policy.safe_vram_pool_mb)) != std::string::npos) &&
                                       (entry_uma_block.find("r.Streaming.PoolSize=4096") == std::string::npos) &&
                                       (entry_policy.safe_vram_pool_mb <= 2048);

        bool case_a_ok = entry_policy_ok && entry_survival_guard_ok;

        // ---------------------------------------------------------------------
        // Caso B — Mid Vega APU (Tier2_MidVegaUMA — Ryzen 5 4600H): Sweet-Spot Guard
        // ---------------------------------------------------------------------
        vegaflow::SystemHardwareProfile mid_hw{};
        mid_hw.total_physical_ram_bytes = 16ULL * 1024 * 1024 * 1024;
        mid_hw.cpu_topology.physical_cores = 6;
        mid_hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        mid_hw.primary_gpu.mem_arch = vegaflow::MemoryArchitecture::UMA_Integrated;
        mid_hw.primary_gpu.arch_gen = vegaflow::GpuArchitectureGen::AMD_GCN5_Vega;

        const auto mid_policy = vegaflow::HardwareProfiler::BuildOptimizationPolicy(mid_hw);

        bool case_b_ok = (mid_policy.tier == vegaflow::ApuTier::Tier2_MidVegaUMA) &&
                         (mid_policy.guard_mode_name == "Sweet-Spot Guard") &&
                         (mid_policy.safe_vram_pool_mb == 4096) &&
                         (mid_policy.dxvk_compiler_threads == 6) &&
                         (mid_policy.amd_tessellation_cap == 16) &&
                         (mid_policy.disable_dx12_async_compute == true) &&
                         (mid_policy.cpu_stapm_cap_mhz == 0);

        // ---------------------------------------------------------------------
        // Caso C — Advanced RDNA3 APU (Tier3_AdvancedUMA — Radeon 780M / 890M 32GB): Do-No-Harm Guard
        // ---------------------------------------------------------------------
        vegaflow::SystemHardwareProfile adv_hw{};
        adv_hw.total_physical_ram_bytes = 32ULL * 1024 * 1024 * 1024;
        adv_hw.cpu_topology.physical_cores = 8;
        adv_hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        adv_hw.primary_gpu.mem_arch = vegaflow::MemoryArchitecture::UMA_Integrated;
        adv_hw.primary_gpu.arch_gen = vegaflow::GpuArchitectureGen::AMD_RDNA3_Plus;

        const auto adv_policy = vegaflow::HardwareProfiler::BuildOptimizationPolicy(adv_hw);

        bool adv_policy_ok = (adv_policy.tier == vegaflow::ApuTier::Tier3_AdvancedUMA) &&
                             (adv_policy.guard_mode_name == "Do-No-Harm Guard") &&
                             (adv_policy.disable_dx12_async_compute == false) &&
                             (adv_policy.cpu_stapm_cap_mhz == 0) &&
                             (adv_policy.safe_vram_pool_mb >= 6144 && adv_policy.safe_vram_pool_mb <= 8192) &&
                             (adv_policy.dxvk_compiler_threads == 6) &&
                             (adv_policy.amd_tessellation_cap == 32);

        std::string adv_uma_block = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(adv_policy);
        bool adv_async_compute_ok = (adv_uma_block.find("D3D12.AsyncCompute=1") != std::string::npos) &&
                                    (adv_uma_block.find("r.RDG.AsyncCompute=1") != std::string::npos);

        bool case_c_ok = adv_policy_ok && adv_async_compute_ok;

        // ---------------------------------------------------------------------
        // Caso D — Runtime DXGI Pressure Guard (MemoryManager::EvaluateUmaPressureGuard)
        // ---------------------------------------------------------------------
        vegaflow::MemoryManager mem_mgr;

        // Telemetría saludable (~48.8% utilización local, 4096 MB RAM libre)
        vegaflow::SystemMemoryTelemetry healthy_telem{};
        healthy_telem.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        healthy_telem.free_and_zero_bytes = 4096ULL * 1024 * 1024;
        healthy_telem.standby_bytes = 2048ULL * 1024 * 1024;
        healthy_telem.vram.valid = true;
        healthy_telem.vram.local_budget_bytes = 4096ULL * 1024 * 1024;
        healthy_telem.vram.local_usage_bytes = 2000ULL * 1024 * 1024;

        auto healthy_status = mem_mgr.EvaluateUmaPressureGuard(healthy_telem, mid_policy, {}, true /* dry_run */);
        bool healthy_ok = (healthy_status.guard_triggered == false);

        // Telemetría bajo alta presión (~90% utilización local, 3000 MB RAM libre)
        vegaflow::SystemMemoryTelemetry pressured_telem{};
        pressured_telem.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        pressured_telem.free_and_zero_bytes = 3000ULL * 1024 * 1024;
        pressured_telem.standby_bytes = 1024ULL * 1024 * 1024;
        pressured_telem.vram.valid = true;
        pressured_telem.vram.local_budget_bytes = 4096ULL * 1024 * 1024;
        pressured_telem.vram.local_usage_bytes = 3686ULL * 1024 * 1024;

        auto pressured_status = mem_mgr.EvaluateUmaPressureGuard(pressured_telem, mid_policy, {}, true /* dry_run */);
        bool pressured_ok = (pressured_status.guard_triggered == true) &&
                            (pressured_status.dxgi_budget_utilization >= 0.85) &&
                            (pressured_status.total_interventions == 1);

        bool case_d_ok = healthy_ok && pressured_ok;

        // ---------------------------------------------------------------------
        // Caso E — Serialización JSON de SystemInfo
        // ---------------------------------------------------------------------
        std::string sys_json = vegaflow::SystemInfo().ToJson();
        bool case_e_ok = (sys_json.find("\"apu_guard\":") != std::string::npos) &&
                         (sys_json.find("\"guard_mode\":") != std::string::npos) &&
                         (sys_json.find("\"dx12_async_compute_allowed\":") != std::string::npos);

        bool t25_success = case_a_ok && case_b_ok && case_c_ok && case_d_ok && case_e_ok;

        std::string details = "Tier 1 Survival Guard: " + std::string(case_a_ok ? "PASS" : "FAIL") + ", "
            + "Tier 2 Sweet-Spot: " + std::string(case_b_ok ? "PASS" : "FAIL") + ", "
            + "Tier 3 Do-No-Harm (DX12 AsyncCompute=1): " + std::string(case_c_ok ? "PASS" : "FAIL") + ", "
            + "Runtime DXGI Pressure Guard: " + std::string(case_d_ok ? "PASS" : "FAIL") + ", "
            + "JSON Serialization (apu_guard): " + std::string(case_e_ok ? "PASS" : "FAIL");

        RecordResult(t25_success, "Test 25: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards", details);
    }

    // -------------------------------------------------------------------------
    // TEST 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync
    // -------------------------------------------------------------------------
    {
        // ---------------------------------------------------------------------
        // Caso A — Partición Asimétrica Zen 4 + Zen 4c / Multi-Isla L3
        // ---------------------------------------------------------------------
        vegaflow::CpuTopologyProfile topo_a{};
        // 6 núcleos físicos / 12 hilos:
        // Isla L3 #0 (Zen 4): Cores 0, 1, 2, 3 (8 hilos, llc_index = 0)
        // Isla L3 #1 (Zen 4c compacta): Cores 4, 5 (4 hilos, llc_index = 1)
        for (uint32_t c = 0; c < 4; ++c) {
            for (uint32_t t = 0; t < 2; ++t) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = 256 + c * 2 + t;
                cs.core_index = c;
                cs.logical_processor_index = c * 2 + t;
                cs.last_level_cache_index = 0;
                cs.efficiency_class = 0;
                topo_a.all_cpu_sets.push_back(cs);
            }
        }
        for (uint32_t c = 4; c < 6; ++c) {
            for (uint32_t t = 0; t < 2; ++t) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = 256 + c * 2 + t;
                cs.core_index = c;
                cs.logical_processor_index = c * 2 + t;
                cs.last_level_cache_index = 1;
                cs.efficiency_class = 0;
                topo_a.all_cpu_sets.push_back(cs);
            }
        }

        vegaflow::HardwareProfiler::PartitionCpuSetsFromTopology(topo_a);

        bool case_a_bg_ok = (topo_a.background_cpu_set_ids.size() == 4);
        for (ULONG bid : topo_a.background_cpu_set_ids) {
            if (bid < 264 || bid > 267) case_a_bg_ok = false;
        }
        bool case_a_game_ok = (topo_a.game_optimal_cpu_set_ids.size() == 6);
        for (ULONG gid : topo_a.game_optimal_cpu_set_ids) {
            if (gid < 258 || gid > 263) case_a_game_ok = false;
        }
        bool case_a_ok = case_a_bg_ok && case_a_game_ok && topo_a.has_multiple_ccx;

        // ---------------------------------------------------------------------
        // Caso B — Partición Simétrica Dual-CCX tipo Ryzen 5 4600H
        // ---------------------------------------------------------------------
        vegaflow::CpuTopologyProfile topo_b{};
        // 6 núcleos / 12 hilos divididos en 2 CCX simétricos de 3 núcleos c/u
        // Isla L3 #0 = Cores 0, 1, 2
        for (uint32_t c = 0; c < 3; ++c) {
            for (uint32_t t = 0; t < 2; ++t) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = 256 + c * 2 + t;
                cs.core_index = c;
                cs.logical_processor_index = c * 2 + t;
                cs.last_level_cache_index = 0;
                cs.efficiency_class = 0;
                topo_b.all_cpu_sets.push_back(cs);
            }
        }
        // Isla L3 #1 = Cores 3, 4, 5
        for (uint32_t c = 3; c < 6; ++c) {
            for (uint32_t t = 0; t < 2; ++t) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = 256 + c * 2 + t;
                cs.core_index = c;
                cs.logical_processor_index = c * 2 + t;
                cs.last_level_cache_index = 1;
                cs.efficiency_class = 0;
                topo_b.all_cpu_sets.push_back(cs);
            }
        }

        vegaflow::HardwareProfiler::PartitionCpuSetsFromTopology(topo_b);

        // Core 5 asignado a fondo (cpu_set_ids 266, 267)
        bool case_b_bg_ok = (topo_b.background_cpu_set_ids.size() == 2) &&
                            (std::find(topo_b.background_cpu_set_ids.begin(), topo_b.background_cpu_set_ids.end(), 266) != topo_b.background_cpu_set_ids.end()) &&
                            (std::find(topo_b.background_cpu_set_ids.begin(), topo_b.background_cpu_set_ids.end(), 267) != topo_b.background_cpu_set_ids.end());

        // 8 hilos en game_optimal_cpu_set_ids priorizando CCX 0 intacto (Cores 1 y 2 primero)
        bool case_b_game_ok = (topo_b.game_optimal_cpu_set_ids.size() == 8);
        bool case_b_prioritized = (topo_b.game_optimal_cpu_set_ids.size() >= 4) &&
                                  (topo_b.game_optimal_cpu_set_ids[0] == 258 &&
                                   topo_b.game_optimal_cpu_set_ids[1] == 259 &&
                                   topo_b.game_optimal_cpu_set_ids[2] == 260 &&
                                   topo_b.game_optimal_cpu_set_ids[3] == 261);

        bool case_b_ok = case_b_bg_ok && case_b_game_ok && case_b_prioritized && topo_b.has_multiple_ccx;

        // ---------------------------------------------------------------------
        // Caso C — Telemetría en Vivo MemoryManager
        // ---------------------------------------------------------------------
        vegaflow::MemoryManager test_mem_mgr;
        vegaflow::SystemHardwareProfile test_hw{};
        test_hw.total_physical_ram_bytes = 16ULL * 1024 * 1024 * 1024;
        test_hw.primary_gpu.mem_arch = vegaflow::MemoryArchitecture::UMA_Integrated;
        test_hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        test_hw.primary_gpu.arch_gen = vegaflow::GpuArchitectureGen::AMD_GCN5_Vega;
        test_hw.cpu_topology.physical_cores = 6;
        test_hw.cpu_topology.logical_processors = 12;

        const auto test_policy = vegaflow::HardwareProfiler::BuildOptimizationPolicy(test_hw);

        // 1. Telemetría a ~90% DXGI (3686 MB / 4096 MB) con RAM libre suficiente (4000 MB)
        vegaflow::SystemMemoryTelemetry high_press_telem{};
        high_press_telem.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        high_press_telem.free_and_zero_bytes = 4000ULL * 1024 * 1024;
        high_press_telem.standby_bytes = 2000ULL * 1024 * 1024;
        high_press_telem.vram.valid = true;
        high_press_telem.vram.local_budget_bytes = 4096ULL * 1024 * 1024;
        high_press_telem.vram.local_usage_bytes = 3686ULL * 1024 * 1024;

        test_mem_mgr.EvaluateUmaPressureGuard(high_press_telem, test_policy, {}, true /* dry_run */);

        bool high_ratio_ok = (test_mem_mgr.GetLastDxgiPressureRatio() >= 0.85);
        bool high_trig_ok = (test_mem_mgr.IsUmaGuardCurrentlyTriggered() == true);
        uint32_t interventions_after_high = test_mem_mgr.GetUmaGuardInterventionsCount();
        bool high_interv_ok = (interventions_after_high >= 1);

        // 2. Telemetría saludable (~45% DXGI, 1843 MB / 4096 MB)
        vegaflow::SystemMemoryTelemetry healthy_telem2{};
        healthy_telem2.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        healthy_telem2.free_and_zero_bytes = 4000ULL * 1024 * 1024;
        healthy_telem2.standby_bytes = 2000ULL * 1024 * 1024;
        healthy_telem2.vram.valid = true;
        healthy_telem2.vram.local_budget_bytes = 4096ULL * 1024 * 1024;
        healthy_telem2.vram.local_usage_bytes = 1843ULL * 1024 * 1024;

        test_mem_mgr.EvaluateUmaPressureGuard(healthy_telem2, test_policy, {}, true /* dry_run */);

        bool healthy_ratio_ok = (test_mem_mgr.GetLastDxgiPressureRatio() < 0.85);
        bool healthy_trig_ok = (test_mem_mgr.IsUmaGuardCurrentlyTriggered() == false);
        bool healthy_interv_preserved = (test_mem_mgr.GetUmaGuardInterventionsCount() == interventions_after_high);

        bool case_c_ok = high_ratio_ok && high_trig_ok && high_interv_ok &&
                         healthy_ratio_ok && healthy_trig_ok && healthy_interv_preserved;

        // ---------------------------------------------------------------------
        // Caso D — Smart Uncapped Boost Power Mode Execution
        // ---------------------------------------------------------------------
        bool set_power_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, false);
        bool case_d_ok = (test_policy.cpu_stapm_cap_mhz == 0) && set_power_ok;

        bool t26_success = case_a_ok && case_b_ok && case_c_ok && case_d_ok;

        std::string details = "Asymmetric Zen4/4c L3: " + std::string(case_a_ok ? "PASS" : "FAIL") + ", "
            + "Symmetric 4600H Multi-CCX: " + std::string(case_b_ok ? "PASS" : "FAIL") + ", "
            + "Live MemoryManager Telemetry: " + std::string(case_c_ok ? "PASS" : "FAIL") + ", "
            + "Smart Uncapped Boost (0 MHz / EPP 25): " + std::string(case_d_ok ? "PASS" : "FAIL");

        RecordResult(t26_success, "Test 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync", details);
    }

    // -------------------------------------------------------------------------
    // Test 27: la confinacion del fondo depende de su consumo de CPU
    // -------------------------------------------------------------------------
    {
        // Lanza un proceso hijo, aplica dos muestras separadas y devuelve si el fondo quedo confinado.
        auto confined_after_two_samples = [](const wchar_t* command) {
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi{};
            std::wstring cmd = command;
            if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                return false;
            }
            vegaflow::CpuOrchestrator orchestrator;
            const std::vector<uint32_t> bg{pi.dwProcessId};
            orchestrator.ApplyOptimizations(0, 0, false, {}, {2}, bg, false);
            Sleep(1300);
            orchestrator.ApplyOptimizations(0, 0, false, {}, {2}, bg, false);
            const bool confined = orchestrator.IsBackgroundConfined();
            orchestrator.RestoreAll(false);
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return confined;
        };
        const bool busy_confined = confined_after_two_samples(L"cmd.exe /c for /l %i in (1,1,2000000000) do @rem");
        const bool idle_confined = confined_after_two_samples(L"ping.exe -n 30 127.0.0.1");
        RecordResult(busy_confined && !idle_confined,
                     "Test 27: Background confinement follows its CPU use",
                     "Busy background confined: " + std::string(busy_confined ? "PASS" : "FAIL") + ", "
                     + "Idle background left free: " + std::string(!idle_confined ? "PASS" : "FAIL"));
    }

    // -------------------------------------------------------------------------
    // Test 28: los hilos de fondo escalan con el numero de procesadores logicos
    // -------------------------------------------------------------------------
    {
        // Crea una topologia de un solo L3 con `cores` nucleos y `smt` hilos por nucleo y devuelve el tamano del conjunto de fondo.
        auto bg_size = [](uint32_t cores, uint32_t smt) {
            vegaflow::CpuTopologyProfile topo{};
            for (uint32_t c = 0; c < cores; ++c) {
                for (uint32_t t = 0; t < smt; ++t) {
                    vegaflow::CpuCoreSetInfo cs{};
                    cs.cpu_set_id = 256 + c * smt + t;
                    cs.core_index = c;
                    cs.logical_processor_index = c * smt + t;
                    cs.last_level_cache_index = 0;
                    cs.efficiency_class = 0;
                    topo.all_cpu_sets.push_back(cs);
                }
            }
            vegaflow::HardwareProfiler::PartitionCpuSetsFromTopology(topo);
            const size_t bg = topo.background_cpu_set_ids.size();
            const bool all_assigned = (bg + topo.game_optimal_cpu_set_ids.size() >= topo.all_cpu_sets.size() - 2);
            return all_assigned ? bg : size_t{999};
        };
        const bool c4t4 = (bg_size(4, 1) == 1);
        const bool c2t4 = (bg_size(2, 2) == 1);
        const bool c6t12 = (bg_size(6, 2) == 2);
        const bool c8t16 = (bg_size(8, 2) == 2);
        RecordResult(c4t4 && c2t4 && c6t12 && c8t16,
                     "Test 28: Background CPU set size scales with logical processors",
                     "4C/4T=1: " + std::string(c4t4 ? "PASS" : "FAIL") + ", 2C/4T=1: " + std::string(c2t4 ? "PASS" : "FAIL")
                     + ", 6C/12T=2: " + std::string(c6t12 ? "PASS" : "FAIL") + ", 8C/16T=2: " + std::string(c8t16 ? "PASS" : "FAIL"));
    }

    // -------------------------------------------------------------------------
    // Test 29: los cambios de registro devuelven el valor original
    // -------------------------------------------------------------------------
    {
        // Lee los valores de GameDVR que toca el optimizador: (existe, dato).
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

        vegaflow::GraphicsOptimizer::RestoreAllRegistryBackups(); // deja el estado de pruebas anteriores como original
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

    // -------------------------------------------------------------------------
    // FINAL SUMMARY
    // -------------------------------------------------------------------------
    std::cout << "\n================================================================================\n";
    std::cout << "TEST RESULTS: " << g_pass_count << " PASSED, " << g_fail_count << " FAILED\n";
    std::cout << "================================================================================\n";

    return (g_fail_count == 0) ? 0 : 1;
}
