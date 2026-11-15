#pragma once

#include "model/hardware.hpp"
#include "windows/nt_defs.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

class HardwareProfiler {
public:
    static SystemHardwareProfile DetectHardware(bool include_displays = false);
    static const char* VendorToString(GpuVendor v);
    static const char* MemArchToString(MemoryArchitecture m);
    static const char* ConnectionToString(GpuConnection c);
    static const char* DiskTypeToString(GameDiskType d);
    static const char* SystemArchToString(SystemArch a);
    static ApuOptimizationPolicy BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0);
    static const char* ApuTierToString(ApuTier tier);
    static const char* ArchGenToString(GpuArchitectureGen gen);
    static void PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology);
};

} // namespace vegaflow
