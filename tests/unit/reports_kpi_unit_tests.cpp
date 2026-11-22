// Unit test: the [KPI] line says "passed" only when the daemon is under its 15 MB budget.

#include <string>

#include "session/reports.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test104() {
    using vegaflow::app::MemoryKpiLine;
    constexpr uint64_t kMb = 1024 * 1024;
    const std::string small = MemoryKpiLine(2 * kMb);
    const std::string edge = MemoryKpiLine(15 * kMb);
    const std::string big = MemoryKpiLine(20 * kMb);
    const bool ok = small.find("passed") != std::string::npos && small.find("2 MB") != std::string::npos &&
                    edge.find("FAILED") != std::string::npos && big.find("FAILED") != std::string::npos &&
                    big.find("passed") == std::string::npos;
    RecordResult(ok, "Test 104: the memory KPI line fails above the 15 MB budget", small + " | " + edge + " | " + big);
}

// F2.7 review: --status prints the [WHY THIS PROFILE] header only when there is at least one reason line.
void Test135() {
    using vegaflow::app::WhyThisProfileSection;
    const std::string none = WhyThisProfileSection({});
    const std::string some = WhyThisProfileSection({"Tier 2 limits.", "Power: AC."});
    const bool ok = none.empty() && some.rfind("[WHY THIS PROFILE]\n", 0) == 0 &&
                    some.find("  - Tier 2 limits.\n") != std::string::npos &&
                    some.find("  - Power: AC.\n") != std::string::npos && some.back() == '\n';
    RecordResult(ok, "Test 135: --status omits the [WHY THIS PROFILE] header when there are no reasons",
                 "none='" + none + "' | some=" + some);
}

} // namespace vftest
