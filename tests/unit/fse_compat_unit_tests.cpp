// Unit test for E1.4: turning Game Bar capture off no longer turns Fullscreen Optimizations off; that is a separate,
// experimental compatibility switch the daemon never applies by itself.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "session/settings_catalog.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test96() {
    using vegaflow::RegHive;
    const std::wstring store = L"System\\GameConfigStore";
    const std::vector<std::wstring> fse = {L"gamedvr_fsebehaviormode", L"gamedvr_honoruserfsebehaviormode",
                                           L"gamedvr_dxgihonorfsewindowscompatible", L"gamedvr_efsefeatureflags"};

    // Capture off: GameDVR_Enabled and AppCaptureEnabled only.
    FakeRegistryPort reg;
    vegaflow::DisplayTweaks display(reg);
    const bool capture_ok = display.SetGameDvrCaptureDisabled(true, false) && display.QueryGameDvrDisabled();
    bool fse_untouched = true;
    for (const auto& n : fse) fse_untouched = fse_untouched && !reg.TouchedName(n);
    const bool capture_writes = reg.TouchedName(L"gamedvr_enabled") && reg.TouchedName(L"appcaptureenabled");

    // The compatibility switch writes the four FSE values and puts the user's originals back when turned off.
    FakeRegistryPort reg2;
    reg2.SeedValue(RegHive::CurrentUser, store, L"GameDVR_FSEBehaviorMode", vegaflow::RegValue::MakeDword(0));
    vegaflow::DisplayTweaks display2(reg2);
    const bool was_off = !display2.QueryFullscreenOptimizationsDisabled();
    const bool on = display2.SetFullscreenOptimizationsDisabled(true, false) && display2.QueryFullscreenOptimizationsDisabled();
    bool all_four = true;
    for (const auto& n : fse) all_four = all_four && reg2.TouchedName(n);
    const bool off = display2.SetFullscreenOptimizationsDisabled(false, false) && !display2.QueryFullscreenOptimizationsDisabled() &&
                     reg2.HasValue(RegHive::CurrentUser, store, L"GameDVR_FSEBehaviorMode") &&
                     !reg2.HasValue(RegHive::CurrentUser, store, L"GameDVR_EFSEFeatureFlags") &&
                     !reg2.TouchedName(L"gamedvr_enabled");
    FakeRegistryPort reg3;
    const bool dry_writes_nothing = vegaflow::DisplayTweaks(reg3).SetFullscreenOptimizationsDisabled(true, true) && reg3.writes.empty();

    // Catalog: a hidden, unmeasured row that the daemon never applies by itself; the Game Bar row no longer names FSE.
    const auto* compat = vegaflow::FindSetting("fse_compat");
    const auto* dvr = vegaflow::FindSetting("game_dvr");
    const bool catalog_ok = compat && compat->experimental && !compat->applied_by_default && compat->user_switch &&
                            compat->evidence == vegaflow::Evidence::NotMeasured && dvr &&
                            std::string(dvr->target).find("FSE") == std::string::npos &&
                            std::string(compat->target).find("GameDVR_FSEBehaviorMode") != std::string::npos;

    const bool ok = capture_ok && fse_untouched && capture_writes && was_off && on && all_four && off && dry_writes_nothing && catalog_ok;
    RecordResult(ok, "Test 96: Game Bar capture and Fullscreen Optimizations are separate; FSE is an opt-in compatibility row",
                 std::string("Capture=") + (capture_ok && capture_writes ? "OK" : "FAIL") + ", FseUntouched=" + (fse_untouched ? "OK" : "FAIL") +
                     ", CompatOn=" + (was_off && on && all_four ? "OK" : "FAIL") + ", CompatOff=" + (off ? "OK" : "FAIL") +
                     ", Dry=" + (dry_writes_nothing ? "OK" : "FAIL") + ", Catalog=" + (catalog_ok ? "OK" : "FAIL"));
}

} // namespace vftest
