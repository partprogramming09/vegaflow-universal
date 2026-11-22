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
#include "windows/registry.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "windows/sandbox_guard.hpp"
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

/** On exit, restores any pending registry backups if any test left changes behind. */
struct PowerPlanGuard {
    ~PowerPlanGuard() {
        vegaflow::WindowsRegistryPort reg;
        reg.RestoreAllBackups();
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
        // Test 7 was removed along with ETW frame telemetry (Phase 6).
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
        {37, vftest::Test37},
        {38, vftest::Test38},
        {39, vftest::Test39},
        {40, vftest::Test40},
        {41, vftest::Test41},
        {42, vftest::Test42},
        {43, vftest::Test43},
        {44, vftest::Test44},
        {45, vftest::Test45},
        {46, vftest::Test46},
        {47, vftest::Test47},
        {48, vftest::Test48},
        {49, vftest::Test49},
        {50, vftest::Test50},
        {51, vftest::Test51},
        {52, vftest::Test52},
        {53, vftest::Test53},
        {54, vftest::Test54},
        {55, vftest::Test55},
        {56, vftest::Test56},
        {57, vftest::Test57},
        {58, vftest::Test58},
        {59, vftest::Test59},
        {60, vftest::Test60},
        {61, vftest::Test61},
        {62, vftest::Test62},
        {63, vftest::Test63},
        {64, vftest::Test64},
        {65, vftest::Test65},
        {66, vftest::Test66},
        {67, vftest::Test67},
        {68, vftest::Test68},
        {69, vftest::Test69},
        {70, vftest::Test70},
        {71, vftest::Test71},
        {72, vftest::Test72},
        {73, vftest::Test73},
        {74, vftest::Test74},
        {75, vftest::Test75},
        {76, vftest::Test76},
        {77, vftest::Test77},
        {78, vftest::Test78},
        {79, vftest::Test79},
        {80, vftest::Test80},
        {81, vftest::Test81},
        {82, vftest::Test82},
        {83, vftest::Test83},
        {84, vftest::Test84},
        {85, vftest::Test85},
        {86, vftest::Test86},
        {87, vftest::Test87},
        {88, vftest::Test88},
        {89, vftest::Test89},
        {90, vftest::Test90},
        {91, vftest::Test91},
        {92, vftest::Test92},
        {93, vftest::Test93},
        {94, vftest::Test94},
        {95, vftest::Test95},
        {96, vftest::Test96},
        {97, vftest::Test97},
        {98, vftest::Test98},
        {99, vftest::Test99},
        {100, vftest::Test100},
        {101, vftest::Test101},
        {102, vftest::Test102},
        {103, vftest::Test103},
        {104, vftest::Test104},
        {105, vftest::Test105},
        {106, vftest::Test106},
        {107, vftest::Test107},
        {108, vftest::Test108},  // E2.3
        {109, vftest::Test109},  // E2.5
        {110, vftest::Test110},  // E2.3 GPU line
        {111, vftest::Test111},  // E2.2
        {112, vftest::Test112},  // E2.6
        {113, vftest::Test113},  // refused background process journaled once
        {114, vftest::Test114},  // E2.4
        {115, vftest::Test115},  // E2.7
        {116, vftest::Test116},  // E2.4 adapter signature read only on demand
        {117, vftest::Test117},  // shutdown gives GameDVR back only if VegaFlow turned it off
        {118, vftest::Test118},  // shutdown restores the power plan only if VegaFlow applied its profile
        {119, vftest::Test119},  // memory compression entries close only when the original state is reached
        {120, vftest::Test120},  // GPU usage is the PDH sum when valid, DXGI otherwise
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
