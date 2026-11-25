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

// One function per numbered test, in tests/integration/. The numbers are stable: 7, 9 and 12 were removed.
void Test01();
void Test02();
void Test03();
void Test04();
void Test05();
void Test06();
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
void Test42();
void Test43();
void Test44();
void Test45();
void Test46();
void Test47();
void Test48();
void Test49();
void Test50();
void Test51();
void Test52();
void Test53();
void Test54();
void Test55();
void Test56();
void Test57();
void Test58();
void Test59();
void Test60();
void Test61();
void Test62();
void Test63();
void Test64();
void Test65();
void Test66();
void Test67();
void Test68();
void Test69();
void Test70();
void Test71();
void Test72();
void Test73();
void Test74();
void Test75();
void Test76();
void Test77();
void Test78();
void Test79();
void Test80();
void Test81();
void Test82();
void Test83();
void Test84();
void Test85();
void Test86();
void Test87();
void Test88();
void Test89();
void Test90();
void Test91();
void Test92();
void Test93();
void Test94();
void Test95();
void Test96();
void Test97();
void Test98();
void Test99();
void Test100();
void Test101();
void Test102();
void Test103();
void Test104();
void Test105();
void Test106();
void Test107();
void Test108();  // E2.3
void Test109();  // E2.5
void Test110();  // E2.3 GPU line
void Test111();  // E2.2
void Test112();  // E2.6
void Test113();  // refused background process journaled once
void Test114();  // E2.4
void Test115();  // E2.7
void Test116();  // E2.4 adapter signature read only on demand
void Test117();  // shutdown gives GameDVR back only if VegaFlow turned it off
void Test135();  // F2.7 review: no reasons, no [WHY THIS PROFILE] header
void Test118();  // shutdown restores the power plan only if VegaFlow applied its profile
void Test119();  // memory compression entries close only when the original state is reached
void Test120();  // GPU usage is the PDH sum when valid, DXGI otherwise
void Test121();  // F1.1 restoring an absent value without access to its key stays pending
void Test122();  // F1.2 giving back never writes a factory value without VegaFlow's copy
void Test123();  // F1.3 no journal entry, no per-exe DXGI write
void Test124();  // F1.4 giving a process back leaves EcoQoS to Windows
void Test125();  // F1.4 a refused process restore stays pending in the journal
void Test126();  // F1.5 game files: copy before every write, foreign .bak ignored, real empty config kept
void Test127();  // F1.6 Volver todo names failed registry copies and keeps safe mode on a partial restore
void Test128();  // F1.6 power profile needs a read of the original and restores into its own plan
void Test129();  // F1.6 the seven power settings exist in the real plan; apply + give back leaves it identical
void Test130();  // F1.6 a setting the plan lacks is skipped, the other six are saved and given back
void Test131();  // F1.7 a failed copy of the original DLL leaves DXVK's DLL in place
void Test132();  // F1.2 the manual off of Ajustes writes the factory value with a copy
void Test133();  // F1.7 review: the manual MMCSS off gives back the legacy value copies too
void Test134();  // F1.2 review: the manual Balanced journals the plan it finds before writing
void Test136();  // startup questions ask only a visible console; no answer is the safe default
void Test137();  // a startup question reads one line key by key (Enter, Backspace, UTF-8, long line)
void Test138();  // the startup question limit is absolute and a key after it does not count
void Test142();  // F4.4: a broken config.json is the person's decision (factory values after a copy, or open the file)
void Test141();  // a standard-user token can join the session log lock an elevated process created
void Test140();  // a filtered token cannot hard-link a protected file it cannot write (F3 hardlink investigation)
void Test143();  // F4.1 the window's polling cadence: visible, hidden, with and without a game
void Test144();  // F4.3 --doctor reads the machine through a DoctorProbe (fake machine)
void Test145();  // F4.3 the Juegos buttons ask the GameFiles port (fake that records the calls)
void Test146();  // F4.3 PartitionCpuSets / BuildOptimizationPolicy / session cycle with a fake environment
void Test139();  // end of input or a failed read is no answer and discards pending keys

} // namespace vftest
