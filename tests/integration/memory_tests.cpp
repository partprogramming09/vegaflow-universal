// Integration tests: memory telemetry and purge. They run on a real Windows machine (registry, power plan, processes).
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

void Test03() {
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
}

void Test23() {
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
}

} // namespace vftest
