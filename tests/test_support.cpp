#include "test_support.hpp"

#include "detect/hardware_profiler.hpp"

#include <iostream>

namespace vftest {

int g_pass_count = 0;
int g_fail_count = 0;

void RecordResult(bool success, const std::string& test_name, const std::string& details) {
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

const vegaflow::ApuOptimizationPolicy& DetectedPolicy() {
    static const vegaflow::ApuOptimizationPolicy policy =
        vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
    return policy;
}

} // namespace vftest
