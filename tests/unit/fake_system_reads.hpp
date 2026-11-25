#pragma once

// In-memory SystemReads: answers from plain fields and counts the questions, so SystemInfo can be built on any host.

#include "ports/system_reads.hpp"

namespace vftest {

class FakeSystemReads final : public vegaflow::SystemReads {
public:
    vegaflow::SystemHardwareProfile hardware{};
    vegaflow::SystemMemoryTelemetry memory{};
    vegaflow::SystemGraphicsTweaksState tweaks{};
    uint64_t l3_bytes = 16ULL << 20;
    uint32_t dimm_slots = 2;

    int detect_calls = 0;
    bool last_include_displays = false;
    int memory_calls = 0;
    uint64_t last_luid = ~0ULL;
    int tweak_calls = 0;

    vegaflow::SystemHardwareProfile DetectHardware(bool include_displays) override {
        ++detect_calls;
        last_include_displays = include_displays;
        return hardware;
    }
    vegaflow::SystemMemoryTelemetry SampleMemory(uint64_t target_luid) override {
        ++memory_calls;
        last_luid = target_luid;
        return memory;
    }
    vegaflow::SystemGraphicsTweaksState QueryTweaks(const vegaflow::SystemHardwareProfile&, const vegaflow::ApuOptimizationPolicy&) override {
        ++tweak_calls;
        return tweaks;
    }
    uint64_t ReadL3CacheBytes() override { return l3_bytes; }
    uint32_t CountPopulatedDimmSlots() override { return dimm_slots; }
};

} // namespace vftest
