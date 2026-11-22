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

// F1.6: no read of the original plan, no profile; and the original goes back to the plan it came from.
void Test128() {
    namespace v = vegaflow;
    v::ApuOptimizationPolicy pol{};

    FakePowerPort unreadable;
    unreadable.read_ok = false;
    v::PowerModeTweaks blind(unreadable);
    const bool refused = !blind.SetApuLaptopPowerProfile(true, pol, false) && unreadable.apply_count == 0 && !blind.IsApuPowerModeApplied();

    FakePowerPort plan;
    const std::string plan_a = plan.scheme_guid;
    const auto original_a = plan.current;
    v::PowerModeTweaks tweaks(plan);
    const bool applied = tweaks.SetApuLaptopPowerProfile(true, pol, false);
    // The user switches to another plan while the profile is on.
    const std::string plan_b = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
    const v::ApuPowerPlanValues values_b{ { 1, 1, 10, 0, 0, 100, 70 }, { 1, 1, 10, 0, 0, 100, 70 } };
    plan.scheme_guid = plan_b;
    plan.current = values_b;
    const bool restored = tweaks.RestoreIfApplied(pol, false);
    const bool plan_b_untouched = plan.current.ac == values_b.ac && plan.current.dc == values_b.dc;
    const bool plan_a_back = plan.other_plans.count(plan_a) == 1 && plan.other_plans[plan_a].ac == original_a.ac &&
                             plan.other_plans[plan_a].dc == original_a.dc;

    RecordResult(refused && applied && restored && plan_b_untouched && plan_a_back,
                 "Test 128: the power profile needs a read of the original, and the original returns to its own plan (F1.6)",
                 std::string("Refused=") + (refused ? "1" : "0") + ", Applied=" + (applied ? "1" : "0") + ", Restored=" + (restored ? "1" : "0") +
                     ", OtherPlanUntouched=" + (plan_b_untouched ? "1" : "0") + ", OriginalPlanBack=" + (plan_a_back ? "1" : "0"));
}

// F1.6: a setting the plan does not have reads as absent; it is not written, the other six are saved and given back.
void Test130() {
    namespace v = vegaflow;
    v::ApuOptimizationPolicy pol{};
    FakePowerPort plan;
    plan.current.ac[3] = v::kApuPowerSettingAbsent;
    plan.current.dc[3] = v::kApuPowerSettingAbsent;
    const auto before = plan.current;
    v::PowerModeTweaks tweaks(plan);
    const bool applied = tweaks.SetApuLaptopPowerProfile(true, pol, false);
    const bool skipped = plan.current.ac[3] == v::kApuPowerSettingAbsent && plan.current.dc[3] == v::kApuPowerSettingAbsent;
    const bool others_changed = plan.current.ac[0] == 0 && plan.current.ac[6] == 25;
    const bool back = tweaks.RestoreIfApplied(pol, false) && plan.current.ac == before.ac && plan.current.dc == before.dc;
    RecordResult(applied && skipped && others_changed && back,
                 "Test 130: a power setting the plan lacks is skipped, the other six are saved and given back (F1.6)",
                 std::string("Applied=") + (applied ? "1" : "0") + ", AbsentSkipped=" + (skipped ? "1" : "0") +
                     ", OthersChanged=" + (others_changed ? "1" : "0") + ", GivenBack=" + (back ? "1" : "0"));
}

} // namespace vftest
