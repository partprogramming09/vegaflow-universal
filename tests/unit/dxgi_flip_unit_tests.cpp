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

    // Layers refused after the preference was written: the journal entry must stay committed, because only committed
    // entries are reverted and the preference change would otherwise stay on with no way back. Nothing written: failed.
    FakeRegistryPort part;
    part.fail_write_subkeys.insert(FakeRegistryPort::Lower(kLayers));
    const bool part_result = vegaflow::DxgiFlipTweaks(part).ApplyFlipModelUpgrade(kExe, false);
    FakeRegistryPort none;
    none.fail_write_subkeys.insert(FakeRegistryPort::Lower(kUgp));
    const bool none_result = vegaflow::DxgiFlipTweaks(none).ApplyFlipModelUpgrade(kExe, false);
    const bool partial_ok = !part_result && part.dxgi_journal_results == std::vector<bool>{true} &&
                            !none_result && none.dxgi_journal_results == std::vector<bool>{false} &&
                            !none.HasValue(vegaflow::RegHive::CurrentUser, kLayers, kExe);

    const bool ok = fresh_ok && idempotent && prefs_kept && switched_in_place && layers_ok && dry_ok && partial_ok;
    RecordResult(ok, "Test 94: Flip upgrade writes only SwapEffectUpgradeEnable and appends HIGHDPIAWARE without touching user flags",
                 "Fresh=" + std::string(fresh_ok ? "OK" : "FAIL") + ", Idempotent=" + (idempotent ? "OK" : "FAIL") +
                     ", PrefsKept=" + (prefs_kept ? "OK" : "FAIL") + ", InPlace=" + (switched_in_place ? "OK" : "FAIL") +
                     ", Layers=" + (layers_ok ? "OK" : "FAIL") + ", Dry=" + (dry_ok ? "OK" : "FAIL") + ", Partial=" + (partial_ok ? "OK" : "FAIL"));
}

// 0.1.x wrote "SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;" for every game: the migration takes out only
// that exact signature's GPU and HDR part, journaled, and leaves anything the user or Windows wrote.
void Test97() {
    using vegaflow::RegHive;
    using vegaflow::RegValue;
    const std::wstring legacy = L"SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;";
    auto seeded = [&]() {
        FakeRegistryPort reg;
        reg.SeedValue(RegHive::CurrentUser, kUgp, L"C:\\a.exe", RegValue::MakeString(legacy));
        reg.SeedValue(RegHive::CurrentUser, kUgp, L"C:\\b.exe", RegValue::MakeString(L"AppStatus=4096;" + legacy));
        reg.SeedValue(RegHive::CurrentUser, kUgp, L"C:\\c.exe", RegValue::MakeString(L"GpuPreference=2;"));
        reg.SeedValue(RegHive::CurrentUser, kUgp, L"C:\\d.exe", RegValue::MakeString(L"AutoHDREnable=0;SwapEffectUpgradeEnable=1;GpuPreference=2;"));
        reg.SeedValue(RegHive::CurrentUser, kUgp, L"C:\\e.exe", RegValue::MakeString(L"AppStatus=0;"));
        return reg;
    };
    auto value = [](FakeRegistryPort& reg, const std::wstring& exe) {
        RegValue v;
        return reg.ReadValue(RegHive::CurrentUser, kUgp, exe, v) ? v.str : L"<none>";
    };

    FakeRegistryPort reg = seeded();
    vegaflow::DxgiFlipTweaks flip(reg);
    const size_t migrated = flip.MigrateLegacyPreferences(false);
    const bool values_ok = value(reg, L"C:\\a.exe") == L"SwapEffectUpgradeEnable=1;" &&
                           value(reg, L"C:\\b.exe") == L"AppStatus=4096;SwapEffectUpgradeEnable=1;" &&
                           value(reg, L"C:\\c.exe") == L"GpuPreference=2;" &&
                           value(reg, L"C:\\d.exe") == L"AutoHDREnable=0;SwapEffectUpgradeEnable=1;GpuPreference=2;" &&
                           value(reg, L"C:\\e.exe") == L"AppStatus=0;";
    const bool journaled = reg.dxgi_journal_results == std::vector<bool>{true, true};
    const size_t writes_after = reg.writes.size();
    const bool second_noop = flip.MigrateLegacyPreferences(false) == 0 && reg.writes.size() == writes_after;

    FakeRegistryPort dry = seeded();
    const bool dry_ok = vegaflow::DxgiFlipTweaks(dry).MigrateLegacyPreferences(true) == 2 && dry.writes.empty();
    FakeRegistryPort empty;
    const bool empty_ok = vegaflow::DxgiFlipTweaks(empty).MigrateLegacyPreferences(false) == 0;

    const bool ok = migrated == 2 && values_ok && journaled && second_noop && dry_ok && empty_ok;
    RecordResult(ok, "Test 97: the 0.1.x GPU/HDR signature is migrated away, journaled, and nothing else is touched",
                 "Migrated=" + std::to_string(migrated) + ", Values=" + (values_ok ? "OK" : "FAIL") + ", Journal=" +
                     (journaled ? "OK" : "FAIL") + ", Repeat=" + (second_noop ? "OK" : "FAIL") + ", Dry=" + (dry_ok ? "OK" : "FAIL") +
                     ", Empty=" + (empty_ok ? "OK" : "FAIL"));
}

} // namespace vftest
