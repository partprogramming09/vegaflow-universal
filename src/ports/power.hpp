#pragma once
// Pure C++ power plan port for APU power/bus tweaks (no <windows.h>).

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

// Values of the seven active power plan indices managed by PowerModeTweaks:
//   0: PCIe ASPM, 1: PerfBoostMode, 2: CoreParkingMinCores, 3: ProcFreqMax,
//   4: ProcFreqMaxAlt, 5: ProcThrottleMax, 6: EnergyPerfPreference.
struct ApuPowerPlanValues {
    std::vector<uint32_t> ac;
    std::vector<uint32_t> dc;
};

class PowerPort {
public:
    virtual ~PowerPort() = default;

    virtual bool ReadActiveApuPlan(std::string& out_scheme_guid, ApuPowerPlanValues& out_values) = 0;
    virtual bool ApplyActiveApuPlan(const ApuPowerPlanValues& values) = 0;
    virtual bool QueryApuPowerOptimized() = 0;
};

} // namespace vegaflow
