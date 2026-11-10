#pragma once
// Shared by the test files: result counters, RecordResult and the TSV reader for tests/vectors/.

#include "model/hardware.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace vftest {

extern int g_pass_count;
extern int g_fail_count;

// Calls `row(cells, raw_line)` for every data row of a tab-separated vector file (blank lines and '#' comments are
// skipped). The caller decides what a malformed row means.
template <typename RowFn>
void ForEachTsvRow(const std::string& path, RowFn row) {
    std::ifstream file(path);
    std::string line;
    while (file && std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cells;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) cells.push_back(cell);
        row(cells, line);
    }
}

void RecordResult(bool success, const std::string& test_name, const std::string& details = "");

// The policy of this machine, built once the same way the optimizers used to build it on every call
// (DetectHardware + BuildOptimizationPolicy, without the live DXGI budget).
const vegaflow::ApuOptimizationPolicy& DetectedPolicy();

// One function per numbered test, in tests/integration/. The numbers are stable: 9 and 12 were removed.
void Test01();
void Test02();
void Test03();
void Test04();
void Test05();
void Test06();
void Test07();
void Test08();
void Test10();
void Test11();
void Test13();
void Test14();
void Test15();
void Test16();
void Test17();
void Test18();
void Test19();
void Test20();
void Test21();
void Test22();
void Test23();
void Test24();
void Test25();
void Test26();
void Test27();
void Test28();
void Test29();
void Test30();
void Test31();
void Test32();
void Test33();
void Test34();
void Test35();
void Test36();
void Test37();
void Test38();
void Test39();
void Test40();
void Test41();

} // namespace vftest
