// Host (macOS/Linux) test runner: registers only the TestN functions of the files in tests/host/allowlist.txt.
// scripts/ci/host-unit-tests.sh generates host_tests.inc (declarations and the table) from that list.

#include "test_support.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <utility>

namespace vftest {
#include "host_tests.inc"
}

int main() {
    using vftest::g_fail_count;
    using vftest::g_pass_count;
    for (const auto& [name, run] : vftest::kHostTests) {
        try {
            run();
        } catch (const std::exception& e) {
            vftest::RecordResult(false, std::string(name) + " threw an exception", e.what());
        } catch (...) {
            vftest::RecordResult(false, std::string(name) + " threw an exception", "unknown type");
        }
    }
    std::cout << "HOST UNIT TESTS: " << g_pass_count << " PASSED, " << g_fail_count << " FAILED\n";
    return g_fail_count == 0 ? 0 : 1;
}
