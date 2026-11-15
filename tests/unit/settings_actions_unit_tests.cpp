// Unit tests: the switches of the Ajustes screen (phase G). Each row of the catalog is turned on through SettingsActions
// with an in-memory registry and power plan, and what it writes is compared with what the row says it changes.

#include "session/restore.hpp"
#include "session/settings_actions.hpp"
#include "session/settings_catalog.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}

bool Same(const vegaflow::RegValue& a, const vegaflow::RegValue& b) {
    return a.type == b.type && a.dword == b.dword && a.qword == b.qword && a.str == b.str && a.binary == b.binary;
}

} // namespace

void Test73() {
    // Test 73: every switch of the catalog is wired, and each registry value it writes is named in its row's text
    const std::wstring kDisplay0000 = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    FakeRegistryPort reg;
    FakePowerPort power;
    reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
    reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000 + L"\\UMD", L"ShaderCache", vegaflow::RegValue::MakeBinary({0x31, 0x00}));
    vegaflow::SystemHardwareProfile hw{};
    hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
    vegaflow::ApuOptimizationPolicy policy{};
    policy.block_driver_tweaks = false;
    policy.force_high_perf_power_plan = true;
    policy.safe_vram_pool_mb = 2048;
    vegaflow::GraphicsOptimizer gfx(hw, policy, reg, &power);
    std::vector<std::string> audit;
    gs::SettingsActions act(gs::SettingsActionsDeps{gfx, policy, [&]() { return policy; }, [&](const std::string& s) { audit.push_back(s); }});

    // 1. Each switchable row changes something (registry or power plan), and every value name written is in the row's target text.
    std::string unwired, undescribed;
    for (const auto& spec : vegaflow::SettingsCatalog()) {
        if (!spec.user_switch) continue;
        const size_t w0 = reg.writes.size();
        const int p0 = power.apply_count;
        const bool mc0 = reg.memory_compression_enabled; // MMAgent is not a registry value
        std::string msg = act.Set(spec.id, true);
        if (reg.writes.size() == w0 && power.apply_count == p0 && reg.memory_compression_enabled == mc0) msg = act.Set(spec.id, false); // already on: turning off must write
        const bool wrote = reg.writes.size() > w0 || power.apply_count > p0 || reg.memory_compression_enabled != mc0;
        if (!wrote) {
            unwired += std::string(spec.id) + " (" + msg + ") ";
            continue;
        }
        const std::string target = Lower(spec.target);
        for (size_t i = w0; i < reg.writes.size(); ++i) {
            const std::string name = Lower(Narrow(reg.writes[i].name));
            if (target.find(name) == std::string::npos) undescribed += std::string(spec.id) + ":" + name + " ";
        }
    }
    RecordResult(unwired.empty(), "Test 73.1: every switchable row of the catalog changes the registry, the power plan or MMAgent",
                 unwired.empty() ? "all wired" : "no change for: " + unwired);
    RecordResult(undescribed.empty(), "Test 73.2: every registry value a switch writes is named in the target text of its row",
                 undescribed.empty() ? "all named" : "written but not named: " + undescribed);

    // 2. A row with no switch changes nothing; a second click on the state already set changes nothing either.
    const size_t w_before = reg.writes.size();
    const int p_before = power.apply_count;
    const std::string none = act.Set("memory_guard", true) + act.Set("no_such_row", true);
    const bool idle = reg.writes.size() == w_before && power.apply_count == p_before;
    act.Set("mpo_disabled", false); // known state: off
    const size_t w_off = reg.writes.size();
    const std::string again = act.Set("mpo_disabled", false);
    RecordResult(idle && none.find("no tiene interruptor") != std::string::npos && reg.writes.size() == w_off && again.find("Ya estaba") != std::string::npos,
                 "Test 73.3: a row without a switch and a repeated click change nothing, and say so");

    // 4. "Volver todo al valor de Windows" puts back what the switches changed: the registry holds again what it held before.
    FakeRegistryPort reg2;
    FakePowerPort power2;
    reg2.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
    reg2.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000 + L"\\UMD", L"ShaderCache", vegaflow::RegValue::MakeBinary({0x31, 0x00}));
    const auto original = reg2.values;
    vegaflow::GraphicsOptimizer gfx2(hw, policy, reg2, &power2);
    gs::SettingsActions act2(gs::SettingsActionsDeps{gfx2, policy, [&]() { return policy; }, nullptr});
    for (const auto& spec : vegaflow::SettingsCatalog()) {
        if (spec.user_switch) act2.Set(spec.id, true);
    }
    bool changed = reg2.values.size() != original.size();
    for (const auto& kv : reg2.values) {
        const auto it = original.find(kv.first);
        changed = changed || it == original.end() || !Same(it->second, kv.second);
    }
    size_t rev = 0, tot = 0;
    act2.ResetAll(&rev, &tot);
    std::string left;
    for (const auto& kv : reg2.values) {
        const auto it = original.find(kv.first);
        if (it == original.end() || !Same(it->second, kv.second)) left += Narrow(std::get<2>(kv.first)) + " ";
    }
    RecordResult(changed && left.empty(), "Test 73.4: ResetAll puts the registry back as it was before the switches",
                 left.empty() ? "back to the original" : "still different: " + left);

    // 5. On a machine where the VRAM override was never turned on, "Volver todo" must not invent a graphics-memory size:
    // it only puts back what it saved. (It does write Windows' own defaults for the other settings.)
    FakeRegistryPort reg3;
    FakePowerPort power3;
    reg3.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
    reg3.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"HardwareInformation.qwMemorySize", vegaflow::RegValue::MakeQword(2048ULL << 20));
    reg3.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000 + L"\\UMD", L"ShaderCache", vegaflow::RegValue::MakeBinary({0x31, 0x00}));
    const std::wstring kDx = L"SOFTWARE\\Microsoft\\DirectX\\abc";
    reg3.SeedValue(vegaflow::RegHive::LocalMachine, kDx, L"VendorId", vegaflow::RegValue::MakeDword(0x1002));
    reg3.SeedValue(vegaflow::RegHive::LocalMachine, kDx, L"DedicatedVideoMemory", vegaflow::RegValue::MakeQword(2048ULL << 20));
    vegaflow::GraphicsOptimizer gfx3(hw, policy, reg3, &power3);
    gs::SettingsActions act3(gs::SettingsActionsDeps{gfx3, policy, [&]() { return policy; }, nullptr});
    size_t r3 = 0, t3 = 0;
    act3.ResetAll(&r3, &t3);
    const auto& q = reg3.values.find(FakeRegistryPort::MakeKey(vegaflow::RegHive::LocalMachine, kDisplay0000, L"HardwareInformation.qwMemorySize"));
    const auto dxq = reg3.values.find(FakeRegistryPort::MakeKey(vegaflow::RegHive::LocalMachine, kDx, L"DedicatedVideoMemory"));
    bool invented = q == reg3.values.end() || q->second.qword != (2048ULL << 20) || dxq == reg3.values.end() || dxq->second.qword != (2048ULL << 20);
    for (const auto& w : reg3.writes) {
        const std::string n = Lower(Narrow(w.name));
        invented = invented || n == "hardwareinformation.memorysize" || n == "hardwareinformation.qwmemorysize" || n == "dedicatedvideomemory";
    }
    RecordResult(!invented && r3 == 0, "Test 73.5: \"Volver todo\" on a machine where the VRAM override was never set writes no graphics-memory size");

    // 6. VRAM reporting: with the live policy's pool it writes that size and, when turned off, puts the saved original back.
    FakeRegistryPort reg4;
    FakePowerPort power4;
    const auto Adapter = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    reg4.SeedValue(vegaflow::RegHive::LocalMachine, Adapter, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
    reg4.SeedValue(vegaflow::RegHive::LocalMachine, Adapter, L"HardwareInformation.qwMemorySize", vegaflow::RegValue::MakeQword(512ULL << 20));
    reg4.SeedValue(vegaflow::RegHive::LocalMachine, Adapter, L"HardwareInformation.MemorySize", vegaflow::RegValue::MakeDword(512U << 20));
    vegaflow::GraphicsOptimizer gfx4(hw, policy, reg4, &power4);
    vegaflow::ApuOptimizationPolicy live = policy;
    live.safe_vram_pool_mb = 3072; // differs from the boot policy (2048): the size must come from the live one
    live.cpu_stapm_cap_mhz = 15000;
    std::vector<std::string> audit4;
    gs::SettingsActions act4(gs::SettingsActionsDeps{gfx4, policy, [&]() { return live; }, [&](const std::string& l) { audit4.push_back(l); }});
    const auto key_qw = FakeRegistryPort::MakeKey(vegaflow::RegHive::LocalMachine, Adapter, L"HardwareInformation.qwMemorySize");
    const std::string on_msg = act4.Set("vram_reporting", true);
    const bool wrote_live = reg4.values[key_qw].qword == (3072ULL << 20) && on_msg.find("3072") != std::string::npos;
    act4.Set("vram_reporting", false);
    const bool original_back = reg4.values[key_qw].qword == (512ULL << 20);
    act4.Set("power_profile", true);
    bool power_text = false;
    // The power plan the daemon applied at boot is the one in force, so the answer names that ceiling, not the live policy's.
    const std::string boot_mhz = std::to_string(policy.cpu_stapm_cap_mhz) + " MHz";
    for (const auto& l : audit4) {
        power_text = power_text || (policy.cpu_stapm_cap_mhz != 0 && l.find(boot_mhz) != std::string::npos && l.find("15000 MHz") == std::string::npos) ||
                     (policy.cpu_stapm_cap_mhz == 0 && l.find("15000 MHz") == std::string::npos && l.find("Control Térmico") != std::string::npos);
    }
    RecordResult(wrote_live && original_back && power_text && audit4.size() >= 3,
                 "Test 73.6: VRAM reporting writes the live policy's size, turning it off puts the saved original back, and each answer is audited");

    // 7. With no saved original (an override from before copies were kept, or a BIOS that reports a lot) turning it off writes
    // nothing and says why; it does not blame the permissions alone and does not invent a size.
    FakeRegistryPort reg5;
    FakePowerPort power5;
    reg5.SeedValue(vegaflow::RegHive::LocalMachine, Adapter, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
    reg5.SeedValue(vegaflow::RegHive::LocalMachine, Adapter, L"HardwareInformation.qwMemorySize", vegaflow::RegValue::MakeQword(4096ULL << 20));
    vegaflow::GraphicsOptimizer gfx5(hw, policy, reg5, &power5);
    gs::SettingsActions act5(gs::SettingsActionsDeps{gfx5, policy, [&]() { return policy; }, nullptr});
    const std::string off_msg = act5.Set("vram_reporting", false);
    RecordResult(reg5.writes.empty() && off_msg.find("no tiene guardado") != std::string::npos,
                 "Test 73.7: turning VRAM reporting off with no saved original writes nothing and says there is no saved value");
}

void Test75() {
    // Test 75: the sentence after "Volver todo": the journal's summary, else the registry count, else "0 de 0"
    vegaflow::RestoreReport journal;
    journal.total_count = 3;
    journal.summary_message = "Se devolvieron 3 de 3.";
    const bool j = gs::RestoreAllMessage(journal, 9, 9) == "Se devolvieron 3 de 3.";
    const bool r = gs::RestoreAllMessage({}, 2, 5) == "Se devolvieron 2 de 5.";
    const bool none = gs::RestoreAllMessage({}, 0, 0).find("0 de 0") != std::string::npos;
    RecordResult(j && r && none, "Test 75: the Volver todo sentence names the journal summary, else the registry count, else 0 of 0");
}

} // namespace vftest
