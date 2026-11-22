// Unit test: shutdown puts the power plan back only if VegaFlow applied the GPU-priority profile.
// With auto_power_profile off (factory default) the user's own plan must not be touched at exit.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test118() {
    namespace v = vegaflow;
    v::ApuOptimizationPolicy pol{};

    // Never applied: the shutdown writes nothing to the plan.
    FakePowerPort untouched;
    v::PowerModeTweaks idle(untouched);
    const bool idle_restored = idle.RestoreIfApplied(pol, false);
    const bool leaves_plan = !idle_restored && untouched.apply_count == 0;

    // Applied: the shutdown puts the original plan back.
    FakePowerPort ours;
    const auto before = ours.current;
    v::PowerModeTweaks active(ours);
    active.SetApuLaptopPowerProfile(true, pol, false);
    const bool restored = active.RestoreIfApplied(pol, false) && !active.IsApuPowerModeApplied() &&
                          ours.current.ac == before.ac && ours.current.dc == before.dc;

    // Second shutdown call: nothing left to undo.
    const int applies = ours.apply_count;
    active.RestoreIfApplied(pol, false);
    const bool twice_ok = ours.apply_count == applies;

    RecordResult(leaves_plan && restored && twice_ok,
                 "Test 118: shutdown restores the power plan only if VegaFlow applied its profile",
                 std::string("UserPlanKept=") + (leaves_plan ? "1" : "0") + ", Restored=" + (restored ? "1" : "0") +
                     ", Twice=" + (twice_ok ? "1" : "0"));
}

} // namespace vftest
