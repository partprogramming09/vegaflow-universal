#include "test_support.hpp"

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

} // namespace vftest
