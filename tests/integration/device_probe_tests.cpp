// Integration test for Phase 3: live WindowsDeviceProbe on real Windows hardware
// (DXCore IsIntegrated/IsDetachable, SetupAPI RemovalPolicy, Storage SeekPenalty+BusType without write access, power/arch).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "detect/hardware_profiler.hpp"
#include "windows/device_probe.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

void Test52() {
    vegaflow::WindowsDeviceProbe probe;
    const auto snap = probe.ProbeSystem(true);
    const auto hw = vegaflow::HardwareProfiler::DetectHardware(true);
    const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);

    wchar_t exe_buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    const auto disk_type = probe.ProbeDiskType(exe_buf);

    const bool gpu_ok = hw.gpu_probe_ok && !snap.gpus.empty();
    const bool dxcore_ok = hw.dxcore_available;
    // Laptop (Ryzen 5 4600H) must keep classifying as Tier 2 Integrated Vega.
    const bool is_ryzen_4600h = (hw.cpu_topology.brand_string.find("4600H") != std::string::npos);
    const bool tier_ok = !is_ryzen_4600h ||
        (pol.tier == vegaflow::ApuTier::Tier2_MidVegaUMA &&
         pol.gpu_connection == vegaflow::GpuConnection::Integrated &&
         pol.arch_gen == vegaflow::GpuArchitectureGen::AMD_GCN5_Vega &&
         pol.apply_visual_cuts &&
         pol.apply_uma_memory_guard);

    const bool arch_ok = (hw.system_arch != vegaflow::SystemArch::Unknown);
    const bool power_ok = hw.power_status.known;
    const bool disk_ok = (disk_type != vegaflow::GameDiskType::Unknown);

    DWORD session_id = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
    const bool display_ok = (session_id == 0) || !hw.displays.empty();

    const bool passed = gpu_ok && dxcore_ok && tier_ok && arch_ok && power_ok && disk_ok && display_ok;
    const std::string details =
        "DXCore=" + std::string(dxcore_ok ? "YES" : "NO") +
        ", Conn=" + vegaflow::HardwareProfiler::ConnectionToString(pol.gpu_connection) +
        ", Arch=" + vegaflow::HardwareProfiler::ArchGenToString(pol.arch_gen) +
        ", Tier=" + vegaflow::HardwareProfiler::ApuTierToString(pol.tier) +
        ", Disk=" + vegaflow::HardwareProfiler::DiskTypeToString(disk_type) +
        ", SysArch=" + vegaflow::HardwareProfiler::SystemArchToString(hw.system_arch) +
        ", Power=" + std::string(hw.power_status.ac_online ? "AC" : "Battery") +
        ", Displays=" + std::to_string(hw.displays.size());

    RecordResult(passed, "Test 52: Live WindowsDeviceProbe (DXCore, SetupAPI, Storage dwDesiredAccess=0, Power, Arch, Displays)", details);
}

} // namespace vftest
