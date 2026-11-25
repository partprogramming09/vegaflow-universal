#pragma once
// The hardware decisions Rust takes (device_profile.rs, topology partition), converted from and to the C++ model types.
// No Windows call: the processor group of the running thread comes in as an argument. detect/HardwareProfiler forwards here.

#include "model/hardware.hpp"

#include <cstdint>

namespace vegaflow {

// The policy (tier, VRAM pool, DXVK threads, reasons...) of a hardware profile; `live_dxgi_local_budget_bytes` 0 = not read.
ApuOptimizationPolicy BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0);

// Splits the CPU sets into the game's and the background's, and fills core counts and CCX/hybrid flags.
// `current_group` is the processor group of the running thread (0 on a machine with up to 64 logical processors).
void PartitionCpuSets(CpuTopologyProfile& cpu_topology, uint16_t current_group);

// The Rust id of a system architecture (VF_ARCH_*).
uint8_t SystemArchToFfi(SystemArch a);

} // namespace vegaflow
