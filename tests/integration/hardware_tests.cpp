// Integration tests: hardware detection, apu tier and its policy. They run on a real Windows machine (registry, power plan, processes).
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
#include "windows/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"

namespace vftest {

void Test02() {
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
}

void Test04() {
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
}

void Test25() {
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

        // Healthy telemetry (~48.8% local utilization, 4096 MB free RAM)
        vegaflow::SystemMemoryTelemetry healthy_telem{};
        healthy_telem.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        healthy_telem.free_and_zero_bytes = 4096ULL * 1024 * 1024;
        healthy_telem.standby_bytes = 2048ULL * 1024 * 1024;
        healthy_telem.vram.valid = true;
        healthy_telem.vram.local_budget_bytes = 4096ULL * 1024 * 1024;
        healthy_telem.vram.local_usage_bytes = 2000ULL * 1024 * 1024;

        auto healthy_status = mem_mgr.EvaluateUmaPressureGuard(healthy_telem, mid_policy, {}, true /* dry_run */);
        bool healthy_ok = (healthy_status.guard_triggered == false);

        // Telemetry under high pressure (~90% local utilization, 3000 MB free RAM)
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
        // Case E — SystemInfo JSON serialization
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
}

void Test26() {
    // -------------------------------------------------------------------------
    // TEST 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync
    // -------------------------------------------------------------------------
    {
        // ---------------------------------------------------------------------
        // Case A — Asymmetric Partition Zen 4 + Zen 4c / Multi-Island L3
        // ---------------------------------------------------------------------
        vegaflow::CpuTopologyProfile topo_a{};
        // 6 physical cores / 12 threads:
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
        // Case B — Symmetric Dual-CCX Partition like Ryzen 5 4600H
        // ---------------------------------------------------------------------
        vegaflow::CpuTopologyProfile topo_b{};
        // 6 cores / 12 threads split into 2 symmetric CCX of 3 cores each
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

        // 8 threads in game_optimal_cpu_set_ids prioritizing an intact CCX 0 (Cores 1 and 2 first)
        bool case_b_game_ok = (topo_b.game_optimal_cpu_set_ids.size() == 8);
        bool case_b_prioritized = (topo_b.game_optimal_cpu_set_ids.size() >= 4) &&
                                  (topo_b.game_optimal_cpu_set_ids[0] == 258 &&
                                   topo_b.game_optimal_cpu_set_ids[1] == 259 &&
                                   topo_b.game_optimal_cpu_set_ids[2] == 260 &&
                                   topo_b.game_optimal_cpu_set_ids[3] == 261);

        bool case_b_ok = case_b_bg_ok && case_b_game_ok && case_b_prioritized && topo_b.has_multiple_ccx;

        // ---------------------------------------------------------------------
        // Case C — MemoryManager Live Telemetry
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

        // 1. Telemetry at ~90% DXGI (3686 MB / 4096 MB) with enough free RAM (4000 MB)
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

        // 2. Healthy telemetry (~45% DXGI, 1843 MB / 4096 MB)
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
        bool set_power_ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, DetectedPolicy(), false);
        bool case_d_ok = (test_policy.cpu_stapm_cap_mhz == 0) && set_power_ok;

        bool t26_success = case_a_ok && case_b_ok && case_c_ok && case_d_ok;

        std::string details = "Asymmetric Zen4/4c L3: " + std::string(case_a_ok ? "PASS" : "FAIL") + ", "
            + "Symmetric 4600H Multi-CCX: " + std::string(case_b_ok ? "PASS" : "FAIL") + ", "
            + "Live MemoryManager Telemetry: " + std::string(case_c_ok ? "PASS" : "FAIL") + ", "
            + "Smart Uncapped Boost (0 MHz / EPP 25): " + std::string(case_d_ok ? "PASS" : "FAIL");

        RecordResult(t26_success, "Test 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync", details);
    }
}

void Test32() {
    // -------------------------------------------------------------------------
    // Test 32: APU tier classification against the shared vector file (same vectors as the Rust core)
    // -------------------------------------------------------------------------
    {
        int total = 0;
        int mismatches = 0;
        std::string first_mismatch;
        ForEachTsvRow("tests/vectors/apu_tier.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
            if (col.size() != 7) { ++mismatches; if (first_mismatch.empty()) first_mismatch = "bad row: " + line; return; }

            vegaflow::SystemHardwareProfile profile{};
            profile.primary_gpu.mem_arch = (col[1] == "dedicated") ? vegaflow::MemoryArchitecture::Dedicated_VRAM
                                                                    : vegaflow::MemoryArchitecture::UMA_Integrated;
            profile.primary_gpu.vendor = (col[2] == "amd")      ? vegaflow::GpuVendor::AMD
                                       : (col[2] == "intel")    ? vegaflow::GpuVendor::Intel
                                       : (col[2] == "nvidia")   ? vegaflow::GpuVendor::NVIDIA
                                                                : vegaflow::GpuVendor::Unknown;
            profile.primary_gpu.arch_gen = static_cast<vegaflow::GpuArchitectureGen>(std::stoul(col[3]));
            profile.total_physical_ram_bytes = std::stoull(col[4]) * 1024ULL * 1024ULL;
            profile.cpu_topology.physical_cores = static_cast<uint32_t>(std::stoul(col[5]));

            const auto expected = static_cast<vegaflow::ApuTier>(std::stoul(col[6]));
            const auto actual = vegaflow::HardwareProfiler::BuildOptimizationPolicy(profile).tier;
            ++total;
            if (actual != expected) {
                ++mismatches;
                if (first_mismatch.empty()) first_mismatch = col[0];
            }
        });
        bool t32_ok = (total >= 19) && (mismatches == 0);
        RecordResult(t32_ok, "Test 32: APU tier classification matches the shared vector file",
                     std::to_string(total) + " vectors, " + std::to_string(mismatches) + " mismatches" +
                     (first_mismatch.empty() ? "" : " (first: " + first_mismatch + ")"));
    }
}

} // namespace vftest
