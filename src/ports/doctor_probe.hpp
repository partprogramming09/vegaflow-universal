#pragma once
// Port for the reads `--doctor` makes on the machine. Every answer is plain UTF-8 data (model/doctor.hpp), so a test
// gives a fake machine; the real one is app/doctor_probe.cpp. Probe methods may throw: session/doctor drops that section.

#include "model/doctor.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vegaflow {

class DoctorProbe {
public:
    virtual ~DoctorProbe() = default;

    virtual bool IsElevated() = 0;
    virtual DoctorDisk ProbeSystemDrive() = 0;
    // The drive of the game that is open now; nothing when no game is open.
    virtual std::optional<DoctorDisk> ProbeActiveGameDrive() = 0;
    virtual std::vector<DoctorCacheDir> ProbeShaderCaches() = 0;
    // The plain sentences of the rules that decided the hardware profile (F2.7).
    virtual std::vector<std::string> ProfileReasons() = 0;
    // ConfigLoadReport::Warning() of config.json as it reads now; empty when it read cleanly.
    virtual std::string ConfigWarning() = 0;
    virtual std::vector<std::string> RunningProcesses() = 0;
    virtual std::string ComputerName() = 0;
    virtual std::string DomainName() = 0;
};

} // namespace vegaflow
