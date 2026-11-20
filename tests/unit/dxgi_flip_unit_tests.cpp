// Unit test for E1.4: the windowed-game Flip upgrade adds only SwapEffectUpgradeEnable=1 and the HIGHDPIAWARE token,
// keeping every preference and compatibility flag the user already had for that executable.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

namespace {

const std::wstring kUgp = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
const std::wstring kLayers = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
const std::wstring kExe = L"C:\\Games\\Test\\Game.exe";

std::wstring Stored(FakeRegistryPort& reg, const std::wstring& subkey) {
    vegaflow::RegValue v;
    return reg.ReadValue(vegaflow::RegHive::CurrentUser, subkey, kExe, v) ? v.str : L"<none>";
}

// Seeds the two per-exe values (empty = absent), applies the upgrade and returns {preferences, layers}.
std::pair<std::wstring, std::wstring> ApplyOn(const std::wstring& ugp, const std::wstring& layers, size_t* writes_on_second = nullptr) {
    FakeRegistryPort reg;
    if (!ugp.empty()) reg.SeedValue(vegaflow::RegHive::CurrentUser, kUgp, kExe, vegaflow::RegValue::MakeString(ugp));
    if (!layers.empty()) reg.SeedValue(vegaflow::RegHive::CurrentUser, kLayers, kExe, vegaflow::RegValue::MakeString(layers));
    vegaflow::DxgiFlipTweaks flip(reg);
    flip.ApplyFlipModelUpgrade(kExe, false);
    if (writes_on_second) {
        const size_t before = reg.writes.size();
        flip.ApplyFlipModelUpgrade(kExe, false);
        *writes_on_second = reg.writes.size() - before;
    }
    return { Stored(reg, kUgp), Stored(reg, kLayers) };
}

} // namespace

void Test94() {
    size_t second_writes = 99;
    const auto fresh = ApplyOn(L"", L"", &second_writes);
    const bool fresh_ok = fresh.first == L"SwapEffectUpgradeEnable=1;" && fresh.second == L"~ HIGHDPIAWARE";
    const bool idempotent = second_writes == 0;

    // The user's GPU choice and HDR setting stay as they were.
    const auto user = ApplyOn(L"GpuPreference=1;AutoHDREnable=1;", L"");
    const bool prefs_kept = user.first == L"GpuPreference=1;AutoHDREnable=1;SwapEffectUpgradeEnable=1;";

    // Keys from older VegaFlow builds or from the user are not stripped; an existing 0 becomes 1 in place.
    const auto other = ApplyOn(L"SwapEffectUpgradeEnable=0;DirectFlip=1;", L"");
    const bool switched_in_place = other.first == L"SwapEffectUpgradeEnable=1;DirectFlip=1;";

    // Other compatibility flags survive; the token is appended once, never duplicated, never next to another DPI mode.
    const auto flags = ApplyOn(L"", L"~ RUNASADMIN");
    const auto already = ApplyOn(L"", L"~ highdpiaware RUNASADMIN");
    const auto unaware = ApplyOn(L"", L"~ DPIUNAWARE");
    const auto gdi = ApplyOn(L"", L"~ GDIDPISCALING DPIUNAWARE");
    const auto lookalike = ApplyOn(L"", L"~ HIGHDPIAWAREX");
    const bool layers_ok = flags.second == L"~ RUNASADMIN HIGHDPIAWARE" && already.second == L"~ highdpiaware RUNASADMIN" &&
                           unaware.second == L"~ DPIUNAWARE" && gdi.second == L"~ GDIDPISCALING DPIUNAWARE" &&
                           lookalike.second == L"~ HIGHDPIAWAREX HIGHDPIAWARE";

    // dry_run reports the merged value without writing.
    FakeRegistryPort dry_reg;
    dry_reg.SeedValue(vegaflow::RegHive::CurrentUser, kUgp, kExe, vegaflow::RegValue::MakeString(L"GpuPreference=1;"));
    std::wstring dry_pref;
    const bool dry_ok = vegaflow::DxgiFlipTweaks(dry_reg).ApplyFlipModelUpgrade(kExe, true, &dry_pref) &&
                        dry_pref == L"GpuPreference=1;SwapEffectUpgradeEnable=1;" && dry_reg.writes.empty();

    const bool ok = fresh_ok && idempotent && prefs_kept && switched_in_place && layers_ok && dry_ok;
    RecordResult(ok, "Test 94: Flip upgrade writes only SwapEffectUpgradeEnable and appends HIGHDPIAWARE without touching user flags",
                 "Fresh=" + std::string(fresh_ok ? "OK" : "FAIL") + ", Idempotent=" + (idempotent ? "OK" : "FAIL") +
                     ", PrefsKept=" + (prefs_kept ? "OK" : "FAIL") + ", InPlace=" + (switched_in_place ? "OK" : "FAIL") +
                     ", Layers=" + (layers_ok ? "OK" : "FAIL") + ", Dry=" + (dry_ok ? "OK" : "FAIL"));
}

} // namespace vftest
