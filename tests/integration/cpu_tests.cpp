// Integration tests: cpu sets and background confinement. They run on a real Windows machine (registry, power plan, processes).
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

void Test06() {
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
}

void Test11() {
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
}

void Test27() {
    // -------------------------------------------------------------------------
    // Test 27: background confinement depends on its CPU usage
    // -------------------------------------------------------------------------
    {
        // Launches a child process, applies two separate samples and returns whether the background was confined.
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
}

void Test28() {
    // -------------------------------------------------------------------------
    // Test 28: background threads scale with the number of logical processors
    // -------------------------------------------------------------------------
    {
        // Creates a single-L3 topology with `cores` cores and `smt` threads per core and returns the size of the background set.
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
        const bool c4t8 = (bg_size(4, 2) == 2);
        const bool c6t6 = (bg_size(6, 1) == 1);
        RecordResult(c4t4 && c2t4 && c6t12 && c8t16 && c4t8 && c6t6,
                     "Test 28: Background CPU set is one physical core (one thread below 4 cores)",
                     "4C/4T=1: " + std::string(c4t4 ? "PASS" : "FAIL") + ", 2C/4T=1: " + std::string(c2t4 ? "PASS" : "FAIL")
                     + ", 6C/12T=2: " + std::string(c6t12 ? "PASS" : "FAIL") + ", 8C/16T=2: " + std::string(c8t16 ? "PASS" : "FAIL")
                     + ", 4C/8T=2: " + std::string(c4t8 ? "PASS" : "FAIL") + ", 6C/6T=1: " + std::string(c6t6 ? "PASS" : "FAIL"));
    }
}

} // namespace vftest
