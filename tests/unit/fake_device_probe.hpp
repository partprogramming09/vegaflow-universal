#pragma once

// In-memory fake DeviceProbe for Phase 3 unit tests.

#include "ports/device_probe.hpp"

#include <map>
#include <string>

namespace vftest {

class FakeDeviceProbe final : public vegaflow::DeviceProbe {
public:
    vegaflow::RawDeviceSnapshot snapshot{};
    vegaflow::GameDiskType default_disk = vegaflow::GameDiskType::Unknown;
    std::map<std::wstring, vegaflow::GameDiskType> disk_by_path;
    std::map<std::wstring, uint8_t> gpu_pref_by_path;

    vegaflow::RawDeviceSnapshot ProbeSystem(bool include_displays = false) override {
        auto copy = snapshot;
        if (!include_displays) {
            copy.displays.clear();
        }
        return copy;
    }

    vegaflow::GameDiskType ProbeDiskType(const std::wstring& path) override {
        auto it = disk_by_path.find(path);
        if (it != disk_by_path.end()) return it->second;
        return default_disk;
    }

    uint8_t QueryAppGpuPreference(const std::wstring& exe_full_path) override {
        auto it = gpu_pref_by_path.find(exe_full_path);
        if (it != gpu_pref_by_path.end()) return it->second;
        return 0;
    }

    uint64_t QueryAdapterSignature() override {
        return snapshot.adapter_signature;
    }
};

} // namespace vftest
