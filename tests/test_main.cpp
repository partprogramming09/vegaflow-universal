// VegaFlow test suite entry point. The tests themselves are in tests/integration/, one file per module; they run
// here in their original order because several of them change the registry and the power plan for real.

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

#include "test_support.hpp"

#include <exception>
#include <utility>

using vftest::g_fail_count;
using vftest::g_pass_count;

namespace {

/** On exit, restores the power plan to its previous state if any test really applied the APU profile. */
struct PowerPlanGuard {
    ~PowerPlanGuard() {
        if (vegaflow::GraphicsOptimizer::IsApuPowerModeApplied()) {
            vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, vftest::DetectedPolicy());
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

    // A test that throws is recorded as failed and the rest still run; PowerPlanGuard restores the machine when
    // main returns.
    const std::pair<int, void (*)()> tests[] = {
        {1, vftest::Test01},
        {2, vftest::Test02},
        {3, vftest::Test03},
        {4, vftest::Test04},
        {5, vftest::Test05},
        {6, vftest::Test06},
        {7, vftest::Test07},
        {8, vftest::Test08},
        // Test 9 was removed along with the image scaling feature.
        {10, vftest::Test10},
        {11, vftest::Test11},
        // Test 12 was removed along with the image scaling feature.
        {13, vftest::Test13},
        {14, vftest::Test14},
        {15, vftest::Test15},
        {16, vftest::Test16},
        {17, vftest::Test17},
        {18, vftest::Test18},
        {19, vftest::Test19},
        {20, vftest::Test20},
        {21, vftest::Test21},
        {22, vftest::Test22},
        {23, vftest::Test23},
        {24, vftest::Test24},
        {25, vftest::Test25},
        {26, vftest::Test26},
        {27, vftest::Test27},
        {28, vftest::Test28},
        {29, vftest::Test29},
        {30, vftest::Test30},
        {31, vftest::Test31},
        {32, vftest::Test32},
        {33, vftest::Test33},
        {34, vftest::Test34},
        {35, vftest::Test35},
        {36, vftest::Test36},
    };
    for (const auto& [number, run] : tests) {
        try {
            run();
        } catch (const std::exception& e) {
            vftest::RecordResult(false, "Test " + std::to_string(number) + " threw an exception", e.what());
        } catch (...) {
            vftest::RecordResult(false, "Test " + std::to_string(number) + " threw an exception", "unknown type");
        }
    }

    // -------------------------------------------------------------------------
    // FINAL SUMMARY
    // -------------------------------------------------------------------------
    std::cout << "\n================================================================================\n";
    std::cout << "TEST RESULTS: " << g_pass_count << " PASSED, " << g_fail_count << " FAILED\n";
    std::cout << "================================================================================\n";

    return (g_fail_count == 0) ? 0 : 1;
}
