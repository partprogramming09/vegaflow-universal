#pragma once

// Port for probing system hardware (GPUs via DXCore/DXGI + SetupAPI, disk bus/seek, battery, arch, displays).

#include "model/hardware.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

struct RawDeviceSnapshot {
    bool gpu_probe_ok = false;
    bool dxcore_available = false;
    std::vector<GpuProfile> gpus;
    uint64_t total_physical_ram_bytes = 0;
    bool ram_probe_ok = false;
    CpuTopologyProfile cpu_topology;
    bool cpu_probe_ok = false;
    uint32_t cpu_family = 0;
    uint32_t cpu_model = 0;
    PowerSupplyStatus power_status{};
    WindowsVersionInfo windows_version{};
    SystemArch system_arch = SystemArch::Unknown;
    std::vector<DisplayMonitorInfo> displays;
    uint64_t adapter_signature = 0;
    uint16_t current_processor_group = 0; // group of the processor the probing thread ran on (CPU Sets are split by it)
};

class DeviceProbe {
public:
    virtual ~DeviceProbe() = default;
    virtual RawDeviceSnapshot ProbeSystem(bool include_displays = false) = 0;
    virtual GameDiskType ProbeDiskType(const std::wstring& path) = 0;
    virtual uint8_t QueryAppGpuPreference(const std::wstring& exe_full_path) = 0; // 0=default, 1=power saving, 2=high perf
    virtual uint64_t QueryAdapterSignature() = 0;
};

} // namespace vegaflow
