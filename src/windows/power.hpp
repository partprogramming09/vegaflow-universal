#pragma once
// Windows PowerPlan adapter implementing PowerPort via powrprof.dll.

#include "ports/power.hpp"

namespace vegaflow {

class WindowsPowerPort final : public PowerPort {
public:
    bool ReadActiveApuPlan(std::string& out_scheme_guid, ApuPowerPlanValues& out_values) override;
    bool ApplyActiveApuPlan(const ApuPowerPlanValues& values) override;
    bool ApplyApuPlan(const std::string& scheme_guid, const ApuPowerPlanValues& values) override;
    bool QueryApuPowerOptimized() override;
};

} // namespace vegaflow
