#pragma once
// Port for the live reads SystemInfo needs from the machine: the hardware profile, a memory sample, the tweak state, the L3
// cache and the DIMM slots. The real one is app/system_reads.hpp; tests give plain data. Nothing here writes.

#include "model/hardware.hpp"
#include "model/tweaks.hpp"
#include "ports/memory_probe.hpp"

#include <cstdint>

namespace vegaflow {

class SystemReads {
public:
    virtual ~SystemReads() = default;

    virtual SystemHardwareProfile DetectHardware(bool include_displays) = 0;
    // One sample: the VRAM budget of the adapter `target_luid` (0 = the first one) and the RAM lists.
    virtual SystemMemoryTelemetry SampleMemory(uint64_t target_luid) = 0;
    virtual SystemGraphicsTweaksState QueryTweaks(const SystemHardwareProfile& hw, const ApuOptimizationPolicy& policy) = 0;
    virtual uint64_t ReadL3CacheBytes() = 0;       // 0 when it cannot be read
    virtual uint32_t CountPopulatedDimmSlots() = 0; // 0 when the firmware table cannot be read
};

} // namespace vegaflow
