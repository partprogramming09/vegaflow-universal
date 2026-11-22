// Unit test (F1.2): giving back GameDVR, MMCSS, the quantum and the paging shield never writes a factory value over
// the user's own. With VegaFlow's copy the user's value returns; with none VegaFlow never touched it, so nothing is written.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/graphics/graphics_internal.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test122() {
    namespace v = vegaflow;
    using v::RegHive;
    using v::RegValue;
    const std::wstring store = L"System\\GameConfigStore";
    const std::wstring dvr = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
    const std::wstring prio = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
    const std::wstring mem = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
    const std::wstring mm = v::graphics_internal::kMmcssProfilePath;
    const std::wstring games = v::graphics_internal::kMmcssGamesPath;
    auto dword = [](FakeRegistryPort& reg, RegHive h, const std::wstring& key, const std::wstring& name) -> int64_t {
        RegValue val;
        return reg.ReadValue(h, key, name, val) ? static_cast<int64_t>(val.dword) : -1;
    };
    constexpr auto LM = RegHive::LocalMachine;
    constexpr auto CU = RegHive::CurrentUser;

    // No copy: the user's own values stay and nothing is written (GameDVR 0, quantum 0x18, MMCSS 20/2, shield 1/1).
    FakeRegistryPort own;
    own.SeedValue(CU, store, L"GameDVR_Enabled", RegValue::MakeDword(0));
    own.SeedValue(CU, dvr, L"AppCaptureEnabled", RegValue::MakeDword(0));
    own.SeedValue(LM, prio, L"Win32PrioritySeparation", RegValue::MakeDword(0x18));
    own.SeedValue(LM, mem, L"DisablePagingExecutive", RegValue::MakeDword(1));
    own.SeedValue(LM, mem, L"LargeSystemCache", RegValue::MakeDword(1));
    own.SeedValue(LM, mm, L"SystemResponsiveness", RegValue::MakeDword(0));
    own.SeedValue(LM, games, L"Priority", RegValue::MakeDword(8));
    v::DisplayTweaks(own).SetGameDvrCaptureDisabled(false);
    v::MmcssTweaks(own).SetGamingProfileOptimized(false);
    v::KernelSchedulerTweaks(own).SetDynamicPrioritySeparation(0x02);
    v::KernelSchedulerTweaks(own).SetKernelMemoryPagingShield(false);
    const bool no_copy_keeps = own.writes.empty() && own.backups.empty() && dword(own, CU, store, L"GameDVR_Enabled") == 0 &&
                               dword(own, LM, prio, L"Win32PrioritySeparation") == 0x18 && dword(own, LM, mem, L"DisablePagingExecutive") == 1 &&
                               dword(own, LM, mm, L"SystemResponsiveness") == 0 && dword(own, LM, games, L"Priority") == 8;

    // With a copy: VegaFlow's change goes back to the user's value, and a second give-back changes nothing more.
    FakeRegistryPort ours;
    ours.SeedValue(CU, store, L"GameDVR_Enabled", RegValue::MakeDword(1));
    ours.SeedValue(LM, prio, L"Win32PrioritySeparation", RegValue::MakeDword(0x18));
    ours.SeedValue(LM, mm, L"SystemResponsiveness", RegValue::MakeDword(20));
    v::DisplayTweaks(ours).SetGameDvrCaptureDisabled(true);
    v::KernelSchedulerTweaks(ours).SetDynamicPrioritySeparation(0x26);
    v::MmcssTweaks(ours).SetGamingProfileOptimized(true);
    const bool changed = dword(ours, CU, store, L"GameDVR_Enabled") == 0 && dword(ours, LM, prio, L"Win32PrioritySeparation") == 0x26;
    for (int i = 0; i < 2; ++i) {
        v::DisplayTweaks(ours).SetGameDvrCaptureDisabled(false);
        v::KernelSchedulerTweaks(ours).SetDynamicPrioritySeparation(0x02);
        v::MmcssTweaks(ours).SetGamingProfileOptimized(false);
    }
    const bool with_copy_returns = changed && dword(ours, CU, store, L"GameDVR_Enabled") == 1 &&
                                   dword(ours, LM, prio, L"Win32PrioritySeparation") == 0x18 &&
                                   dword(ours, LM, mm, L"SystemResponsiveness") == 20;

    RecordResult(no_copy_keeps && with_copy_returns, "Test 122: giving back never writes a factory value without VegaFlow's copy (F1.2)",
                 std::string("NoCopyKeeps=") + (no_copy_keeps ? "1" : "0") + ", WithCopyReturns=" + (with_copy_returns ? "1" : "0"));
}

} // namespace vftest
