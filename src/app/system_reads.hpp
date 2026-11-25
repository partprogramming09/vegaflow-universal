#pragma once
// The real SystemReads: Windows probes through detect/ and optimize/ (header only, so the test suite can use it too).

#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "ports/system_reads.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "windows/system_counters.hpp"

namespace vegaflow::app {

class WindowsSystemReads final : public SystemReads {
public:
    SystemHardwareProfile DetectHardware(bool include_displays) override { return HardwareProfiler::DetectHardware(include_displays); }

    SystemMemoryTelemetry SampleMemory(uint64_t target_luid) override {
        MemoryManager mem_mgr;
        mem_mgr.SetTargetLuid(target_luid);
        return mem_mgr.SampleTelemetry();
    }

    SystemGraphicsTweaksState QueryTweaks(const SystemHardwareProfile& hw, const ApuOptimizationPolicy& policy) override {
        WindowsRegistryPort reg;
        WindowsPowerPort power;
        GraphicsOptimizer opt(hw, policy, reg, &power);
        return opt.QuerySystemGraphicsTweaks();
    }

    uint64_t ReadL3CacheBytes() override { return ::vegaflow::ReadL3CacheBytes(); }

    // The DIMMs cannot change while Windows runs: the firmware table is read once, not on every GUI poll.
    uint32_t CountPopulatedDimmSlots() override {
        static const uint32_t slots = ::vegaflow::CountPopulatedDimmSlots();
        return slots;
    }
};

} // namespace vegaflow::app
