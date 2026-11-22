#pragma once
// Pure C++ power plan port for APU power/bus tweaks (no <windows.h>).

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

// Values of the seven active power plan indices managed by PowerModeTweaks:
//   0: PCIe ASPM, 1: PerfBoostMode, 2: CoreParkingMinCores, 3: ProcFreqMax,
//   4: ProcFreqMaxAlt, 5: ProcThrottleMax, 6: EnergyPerfPreference.
// Stands for a setting the plan does not have: it is never written and never restored. shortcut: 0xFFFFFFFF is also a
// legal MHz value, but none of the seven settings ever holds it in practice; switch to std::optional if one can.
inline constexpr uint32_t kApuPowerSettingAbsent = 0xFFFFFFFFu;

struct ApuPowerPlanValues {
    std::vector<uint32_t> ac;
    std::vector<uint32_t> dc;
};

class PowerPort {
public:
    virtual ~PowerPort() = default;

    virtual bool ReadActiveApuPlan(std::string& out_scheme_guid, ApuPowerPlanValues& out_values) = 0;
    virtual bool ApplyActiveApuPlan(const ApuPowerPlanValues& values) = 0;
    // Writes the values into the plan with this GUID (the one VegaFlow saved them from), which may no longer be the
    // active plan; the active plan is only re-activated when it is that one.
    virtual bool ApplyApuPlan(const std::string& scheme_guid, const ApuPowerPlanValues& values) = 0;
    virtual bool QueryApuPowerOptimized() = 0;
};

} // namespace vegaflow
