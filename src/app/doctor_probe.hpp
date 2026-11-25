#pragma once
// The real DoctorProbe: reads the disks, shader caches, hardware profile, config.json and process list of this machine.

#include "ports/doctor_probe.hpp"

namespace vegaflow::app {

class WindowsDoctorProbe final : public DoctorProbe {
public:
    bool IsElevated() override;
    DoctorDisk ProbeSystemDrive() override;
    std::optional<DoctorDisk> ProbeActiveGameDrive() override;
    std::vector<DoctorCacheDir> ProbeShaderCaches() override;
    std::vector<std::string> ProfileReasons() override;
    std::string ConfigWarning() override;
    std::vector<std::string> RunningProcesses() override;
    std::string ComputerName() override;
    std::string DomainName() override;
};

} // namespace vegaflow::app
