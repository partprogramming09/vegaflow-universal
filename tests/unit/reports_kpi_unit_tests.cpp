// Unit test: the [KPI] line says "passed" only when the daemon is under its 15 MB budget.

#include <string>

#include "session/reports.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test96() {
    using vegaflow::app::MemoryKpiLine;
    constexpr uint64_t kMb = 1024 * 1024;
    const std::string small = MemoryKpiLine(2 * kMb);
    const std::string edge = MemoryKpiLine(15 * kMb);
    const std::string big = MemoryKpiLine(20 * kMb);
    const bool ok = small.find("passed") != std::string::npos && small.find("2 MB") != std::string::npos &&
                    edge.find("FAILED") != std::string::npos && big.find("FAILED") != std::string::npos &&
                    big.find("passed") == std::string::npos;
    RecordResult(ok, "Test 96: the memory KPI line fails above the 15 MB budget", small + " | " + edge + " | " + big);
}

} // namespace vftest
