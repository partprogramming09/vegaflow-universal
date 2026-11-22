// E2.1: "Devolver todo" copies the user's value before it writes the factory one, so a custom value comes back.
// Cases 1-5, 7, 9-11 of the plan run on the in-memory RegistryPort; 6 and 8 also need a laptop integration run.

#include "session/settings_actions.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

#include <memory>
#include <string>

namespace vftest {

namespace {

namespace v = vegaflow;
const std::wstring kPrio = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
const std::wstring kMem = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
const std::wstring kDwm = L"SOFTWARE\\Microsoft\\Windows\\Dwm";

struct Rig {
    FakeRegistryPort reg;
    FakePowerPort power;
    v::SystemHardwareProfile hw{};
    v::ApuOptimizationPolicy policy{};
    std::unique_ptr<v::GraphicsOptimizer> gfx;
    std::unique_ptr<v::session::SettingsActions> act;
    Rig() {
        gfx = std::make_unique<v::GraphicsOptimizer>(hw, policy, reg, &power);
        act = std::make_unique<v::session::SettingsActions>(v::session::SettingsActionsDeps{*gfx, policy, nullptr, nullptr});
    }
    bool Is(const std::wstring& sub, const wchar_t* name, uint32_t want) {
        v::RegValue r;
        return reg.ReadValue(v::RegHive::LocalMachine, sub, name, r) && r.type == v::RegValueType::Dword && r.dword == want;
    }
    bool Absent(const std::wstring& sub, const wchar_t* name) { return !reg.HasValue(v::RegHive::LocalMachine, sub, name); }
    void Reset() { size_t a = 0, b = 0; act->ResetAll(&a, &b); }
};

} // namespace

void Test107() {
    constexpr auto LM = v::RegHive::LocalMachine;
    // 1: the user's 0x18, no copy yet.
    bool c1;
    { Rig r; r.reg.SeedValue(LM, kPrio, L"Win32PrioritySeparation", v::RegValue::MakeDword(0x18)); r.Reset(); c1 = r.Is(kPrio, L"Win32PrioritySeparation", 0x18); }
    // 2: VegaFlow put 0x26 over the user's 0x18.
    bool c2;
    { Rig r; r.reg.SeedValue(LM, kPrio, L"Win32PrioritySeparation", v::RegValue::MakeDword(0x18));
      r.gfx->KernelScheduler().SetDynamicPrioritySeparation(0x26, false); r.Reset(); c2 = r.Is(kPrio, L"Win32PrioritySeparation", 0x18); }
    // 3: keys absent at the start stay absent.
    bool c3;
    { Rig r; r.Reset(); c3 = r.Absent(kPrio, L"Win32PrioritySeparation") && r.Absent(kMem, L"DisablePagingExecutive") &&
                              r.Absent(kMem, L"LargeSystemCache") && r.Absent(kDwm, L"OverlayTestMode"); }
    // 4: the user's DisablePagingExecutive=1 (and LargeSystemCache=1) survive.
    bool c4;
    { Rig r; r.reg.SeedValue(LM, kMem, L"DisablePagingExecutive", v::RegValue::MakeDword(1));
      r.reg.SeedValue(LM, kMem, L"LargeSystemCache", v::RegValue::MakeDword(1)); r.Reset();
      c4 = r.Is(kMem, L"DisablePagingExecutive", 1) && r.Is(kMem, L"LargeSystemCache", 1); }
    // 5: the user's OverlayTestMode=5, no copy.
    bool c5;
    { Rig r; r.reg.SeedValue(LM, kDwm, L"OverlayTestMode", v::RegValue::MakeDword(5)); r.Reset(); c5 = r.Is(kDwm, L"OverlayTestMode", 5); }
    // 6: compression the user turned off stays off; one VegaFlow turned off comes back.
    bool c6;
    { Rig a; a.reg.memory_compression_enabled = false; a.Reset();
      Rig b; b.gfx->KernelScheduler().SetMemoryCompressionEnabled(false, false); b.Reset();
      c6 = !a.reg.memory_compression_enabled && b.reg.memory_compression_enabled; }
    // 7: twice in a row gives the same values and no extra copies.
    bool c7;
    { Rig r; r.reg.SeedValue(LM, kPrio, L"Win32PrioritySeparation", v::RegValue::MakeDword(0x18)); r.Reset();
      const auto vals = r.reg.values.size(); const bool comp = r.reg.memory_compression_enabled; r.Reset();
      c7 = r.Is(kPrio, L"Win32PrioritySeparation", 0x18) && r.reg.values.size() == vals && r.reg.backups.empty() && r.reg.memory_compression_enabled == comp; }
    // 8: no copy possible -> the factory value is not written and nothing is lost.
    bool c8;
    { Rig r; r.reg.SeedValue(LM, kPrio, L"Win32PrioritySeparation", v::RegValue::MakeDword(0x18));
      r.reg.fail_backup_subkeys.insert(FakeRegistryPort::Lower(kPrio));
      const bool ok = r.gfx->KernelScheduler().SetDynamicPrioritySeparation(0x26, false);  // F1.2: giving back (0x02) never writes
      c8 = !ok && r.Is(kPrio, L"Win32PrioritySeparation", 0x18); }
    // 9: dry_run writes nothing.
    bool c9;
    { Rig r; r.gfx->KernelScheduler().SetDynamicPrioritySeparation(0x02, true); r.gfx->KernelScheduler().SetKernelMemoryPagingShield(false, true);
      r.gfx->Display().SetMpoDisabled(false, true); r.gfx->KernelScheduler().SetMemoryCompressionEnabled(true, true);
      c9 = r.reg.writes.empty() && r.reg.deletes.empty() && r.reg.backups.empty(); }
    // 10: a REG_SZ where a DWORD is expected comes back as the same REG_SZ.
    bool c10;
    { Rig r; r.reg.SeedValue(LM, kPrio, L"Win32PrioritySeparation", v::RegValue::MakeString(L"custom")); r.Reset();
      v::RegValue got; c10 = r.reg.ReadValue(LM, kPrio, L"Win32PrioritySeparation", got) && got.type == v::RegValueType::String && got.str == L"custom"; }
    // 11: nothing to restore -> no write, "0 of 0".
    bool c11;
    { Rig r; size_t rev = 9, tot = 9; r.gfx->RestoreAllRegistryBackups(&rev, &tot); c11 = rev == 0 && tot == 0 && r.reg.writes.empty(); }

    const bool res[] = {c1, c2, c3, c4, c5, c6, c7, c8, c9, c10, c11};
    bool ok = true;
    std::string d;
    for (int i = 0; i < 11; ++i) { ok = ok && res[i]; d += "c" + std::to_string(i + 1) + (res[i] ? "=OK " : "=FAIL "); }
    RecordResult(ok, "Test 107: 'Devolver todo' copies the user's value first and puts it back (E2.1)", d);
}

} // namespace vftest
