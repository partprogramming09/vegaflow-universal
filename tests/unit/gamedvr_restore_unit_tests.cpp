// Unit test: shutting down (daemon cleanup, "Devolver todo") only gives GameDVR back if VegaFlow turned it off.
// Since E2.2 the daemon never turns it off unless auto_game_dvr_off is set, so a user's own 0 must stay 0.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test117() {
    using vegaflow::RegHive;
    using vegaflow::RegValue;
    const std::wstring store = L"System\\GameConfigStore";
    const std::wstring dvr = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
    auto dword = [](FakeRegistryPort& reg, const std::wstring& key, const std::wstring& name) -> int64_t {
        RegValue v;
        return reg.ReadValue(RegHive::CurrentUser, key, name, v) ? static_cast<int64_t>(v.dword) : -1;
    };

    // The user turned GameDVR off by hand and VegaFlow never touched it: the cleanup leaves it off and writes nothing.
    FakeRegistryPort user_off;
    user_off.SeedValue(RegHive::CurrentUser, store, L"GameDVR_Enabled", RegValue::MakeDword(0));
    user_off.SeedValue(RegHive::CurrentUser, dvr, L"AppCaptureEnabled", RegValue::MakeDword(0));
    vegaflow::DisplayTweaks(user_off).RestoreGameDvrCapture();
    const bool keeps_user_off = dword(user_off, store, L"GameDVR_Enabled") == 0 &&
                                dword(user_off, dvr, L"AppCaptureEnabled") == 0 && user_off.writes.empty();

    // VegaFlow turned it off (auto_game_dvr_off): the cleanup puts back what the user had.
    FakeRegistryPort ours;
    ours.SeedValue(RegHive::CurrentUser, store, L"GameDVR_Enabled", RegValue::MakeDword(1));
    vegaflow::DisplayTweaks display(ours);
    display.SetGameDvrCaptureDisabled(true);
    display.RestoreGameDvrCapture();
    const bool gives_back = dword(ours, store, L"GameDVR_Enabled") == 1 && dword(ours, dvr, L"AppCaptureEnabled") == -1;

    // Same call twice: the second finds no copy and changes nothing.
    display.RestoreGameDvrCapture();
    const bool twice_ok = dword(ours, store, L"GameDVR_Enabled") == 1;

    // dry_run changes nothing even with a copy pending.
    FakeRegistryPort dry;
    dry.SeedValue(RegHive::CurrentUser, store, L"GameDVR_Enabled", RegValue::MakeDword(1));
    vegaflow::DisplayTweaks dry_display(dry);
    dry_display.SetGameDvrCaptureDisabled(true);
    dry_display.RestoreGameDvrCapture(true);
    const bool dry_ok = dword(dry, store, L"GameDVR_Enabled") == 0;

    RecordResult(keeps_user_off && gives_back && twice_ok && dry_ok,
                 "Test 117: shutdown gives GameDVR back only when VegaFlow turned it off",
                 std::string("UserOffKept=") + (keeps_user_off ? "1" : "0") + ", GivenBack=" + (gives_back ? "1" : "0") +
                     ", Twice=" + (twice_ok ? "1" : "0") + ", DryRun=" + (dry_ok ? "1" : "0"));
}

} // namespace vftest
