// Link-time stand-ins for the few Windows-only functions that host-compilable code still names.
// (.cc, not .cpp, so the Windows builds that glob tests/**/*.cpp do not pick it up.) Every stub aborts with its own name: a test that passes on the host never reaches one. If a test does, the
// abort says which function it needed, and that test file belongs in the exclusion list (tests/host/allowlist.txt).

#include "detect/hardware_profiler.hpp"
#include "windows/change_journal.hpp"
#include "windows/memory.hpp"
#include "windows/system_counters.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void Unavailable(const char* name) {
    std::fprintf(stderr, "[HOST STUB] %s needs Windows; exclude this test file from the host list\n", name);
    std::abort();
}
} // namespace

namespace vegaflow {

SystemHardwareProfile HardwareProfiler::DetectHardware(bool) { Unavailable("HardwareProfiler::DetectHardware"); }
ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile&, uint64_t) {
    Unavailable("HardwareProfiler::BuildOptimizationPolicy");
}

MemoryProbe& DefaultWindowsMemoryProbe() { Unavailable("DefaultWindowsMemoryProbe"); }
MemoryControl& DefaultWindowsMemoryControl() { Unavailable("DefaultWindowsMemoryControl"); }
uint64_t QueryCurrentProcessPrivateWorkingSetBytes() { Unavailable("QueryCurrentProcessPrivateWorkingSetBytes"); }

ChangeJournal* GetActiveChangeJournal() { Unavailable("GetActiveChangeJournal"); }

uint32_t CountEcoQoSProcesses() { Unavailable("CountEcoQoSProcesses"); }
uint32_t CountDirectFlipConfiguredGames() { Unavailable("CountDirectFlipConfiguredGames"); }

} // namespace vegaflow
