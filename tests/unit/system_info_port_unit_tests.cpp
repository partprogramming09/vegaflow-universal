// Unit tests (F4.3): SystemInfo reads the machine through the SystemReads port (hardware, memory sample, tweaks, L3 cache,
// DIMM slots), so a fake machine gives the whole report on any host and nothing is cached between calls.

#include "session/system_info.hpp"
#include "fake_system_reads.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test147() {
    FakeSystemReads reads;
    reads.hardware.total_physical_ram_bytes = 16ULL << 30;
    reads.hardware.gpu_probe_ok = true; // the GPU part (and its memory budget) is only filled for a readable adapter
    reads.hardware.cpu_probe_ok = true; // L3 is only read for a readable CPU
    reads.hardware.cpu_topology.physical_cores = 6;
    reads.hardware.primary_gpu.luid = 0xABCD;
    reads.memory.total_physical_bytes = 16ULL << 30;
    reads.memory.vram.valid = true;
    reads.memory.vram.local_budget_bytes = 2ULL << 30;
    reads.tweaks.mmcss_gaming_optimized = true;
    reads.tweaks.memory_compression_enabled = true;
    reads.l3_bytes = 32ULL << 20;
    reads.dimm_slots = 2;

    // 1. Gather asks the port for the hardware (with the displays flag) and builds everything else from it.
    const auto snap = vegaflow::SystemInfo::Gather(reads, true);
    RecordResult(reads.detect_calls == 1 && reads.last_include_displays && reads.memory_calls == 1 && reads.last_luid == 0xABCD && reads.tweak_calls == 1 &&
                     snap.cpu.l3_cache_bytes == (32ULL << 20) && snap.gpu.dxgi_budget.valid && snap.gpu.dxgi_budget.local_budget_bytes == (2ULL << 30) &&
                     snap.os_kernel.mmcss_gaming_optimized && snap.os_kernel.memory_compression_enabled,
                 "Test 147.1: Gather reads hardware, memory (for the primary GPU), tweaks and L3 through the port");

    // 2. DIMM slots decide the channel text; the answer is not kept between calls (the adapter caches, the session does not).
    const bool dual = snap.ram.channel_topology.find("Dual-Channel") != std::string::npos && snap.ram.populated_dimm_slots == 2;
    reads.dimm_slots = 1;
    const auto single = vegaflow::SystemInfo::FromHardware(reads.hardware, reads, true);
    reads.dimm_slots = 0;
    const auto none = vegaflow::SystemInfo::FromHardware(reads.hardware, reads, true);
    RecordResult(dual && single.ram.channel_topology.find("Single-Channel") != std::string::npos && single.ram.populated_dimm_slots == 1 &&
                     none.ram.channel_topology == "No se pudo leer",
                 "Test 147.2: the DIMM count from the port picks Dual, Single or unreadable, on every call");

    // 3. Without live telemetry nothing live is asked: no memory sample, no tweak read, no DIMM read.
    const int mem_before = reads.memory_calls;
    const int tweak_before = reads.tweak_calls;
    const auto cold = vegaflow::SystemInfo::FromHardware(reads.hardware, reads, false);
    RecordResult(reads.memory_calls == mem_before && reads.tweak_calls == tweak_before && cold.ram.channel_topology == "No se pudo leer" && !cold.gpu.dxgi_budget.valid,
                 "Test 147.3: a snapshot without live telemetry asks the port for no memory sample or tweaks");

    // 4. The GUI's own sample and tweak state build the snapshot without a memory or tweak read.
    reads.dimm_slots = 2;
    vegaflow::SystemMemoryTelemetry own_mem{};
    own_mem.total_physical_bytes = 8ULL << 30;
    own_mem.vram.valid = true;
    own_mem.vram.local_budget_bytes = 1ULL << 30;
    vegaflow::SystemGraphicsTweaksState own_tweaks{};
    own_tweaks.mmcss_gaming_optimized = false;
    const int mem_b = reads.memory_calls;
    const int tweak_b = reads.tweak_calls;
    const auto gui = vegaflow::SystemInfo::FromHardware(reads.hardware, own_mem, own_tweaks, reads);
    RecordResult(reads.memory_calls == mem_b && reads.tweak_calls == tweak_b && gui.ram.total_physical_bytes == (8ULL << 30) && !gui.os_kernel.mmcss_gaming_optimized &&
                     gui.ram.populated_dimm_slots == 2,
                 "Test 147.4: the GUI's memory sample and tweak state are used as given; only L3 and DIMM slots come from the port");
}

} // namespace vftest
