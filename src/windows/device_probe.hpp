#pragma once

// Windows implementation of DeviceProbe using DXCore, DXGI fallback, SetupAPI, Storage IOCTLs, and Win32 power/arch APIs.

#include "ports/device_probe.hpp"

namespace vegaflow {

class WindowsDeviceProbe final : public DeviceProbe {
public:
    RawDeviceSnapshot ProbeSystem(bool include_displays = false) override;
    GameDiskType ProbeDiskType(const std::wstring& path) override;
    uint8_t QueryAppGpuPreference(const std::wstring& exe_full_path) override;
    uint64_t QueryAdapterSignature() override;
};

} // namespace vegaflow
