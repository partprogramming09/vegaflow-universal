// Adversarial unit tests: the Ajustes switches (SettingsActions) with repeated clicks, random click sequences followed by
// "Volver todo", hostile ids, missing callbacks, a pool of 0 MB and a live policy that differs from the boot one.

#include "session/settings_actions.hpp"
#include "session/settings_catalog.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

void Attack(const std::string& name, const std::function<std::string()>& body) {
    try {
        const std::string why = body();
        RecordResult(why.empty(), name, why);
    } catch (const std::exception& e) {
        RecordResult(false, name, std::string("threw: ") + e.what());
    } catch (...) {
        RecordResult(false, name, "threw an unknown exception");
    }
}

const wchar_t* kAdapter = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";

std::string Narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Same(const vegaflow::RegValue& a, const vegaflow::RegValue& b) {
    return a.type == b.type && a.dword == b.dword && a.qword == b.qword && a.str == b.str && a.binary == b.binary;
}

struct Rig {
    FakeRegistryPort reg;
    FakePowerPort power;
    vegaflow::SystemHardwareProfile hw{};
    vegaflow::ApuOptimizationPolicy policy{};
    std::map<FakeRegistryPort::Key, vegaflow::RegValue> original;
    std::unique_ptr<vegaflow::GraphicsOptimizer> gfx;
    std::unique_ptr<gs::SettingsActions> act;
    std::vector<std::string> audit;

    explicit Rig(uint32_t pool_mb = 2048, bool with_callbacks = true, bool amd = true) {
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kAdapter, L"MatchingDeviceId", vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kAdapter, L"HardwareInformation.qwMemorySize", vegaflow::RegValue::MakeQword(512ULL << 20));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kAdapter, L"HardwareInformation.MemorySize", vegaflow::RegValue::MakeDword(512U << 20));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, std::wstring(kAdapter) + L"\\UMD", L"ShaderCache", vegaflow::RegValue::MakeBinary({0x31, 0x00}));
        original = reg.values;
        hw.primary_gpu.vendor = amd ? vegaflow::GpuVendor::AMD : vegaflow::GpuVendor::Unknown;
        policy.block_driver_tweaks = !amd;
        policy.force_high_perf_power_plan = true;
        policy.safe_vram_pool_mb = pool_mb;
        gfx = std::make_unique<vegaflow::GraphicsOptimizer>(hw, policy, reg, &power);
        gs::SettingsActionsDeps deps{*gfx, policy, nullptr, nullptr};
        if (with_callbacks) {
            deps.live_policy = [this]() { return policy; };
            deps.audit = [this](const std::string& s) { audit.push_back(s); };
        }
        act = std::make_unique<gs::SettingsActions>(deps);
    }

    // Keys that existed before any click and no longer hold their original value.
    std::string Damaged() const {
        std::string out;
        for (const auto& [k, v] : original) {
            const auto it = reg.values.find(k);
            if (it == reg.values.end()) out += Narrow(std::get<2>(k)) + "(missing) ";
            else if (!Same(it->second, v)) out += Narrow(std::get<2>(k)) + "(changed) ";
        }
        return out;
    }
};

std::vector<std::string> SwitchIds() {
    std::vector<std::string> ids;
    for (const auto& spec : vegaflow::SettingsCatalog()) {
        if (spec.user_switch) ids.push_back(spec.id);
    }
    return ids;
}

} // namespace

void Test79() {
    // Test 79: attacks on the Ajustes switches
    Attack("Test 79.1: the catalog has switchable rows (the attacks below are not vacuous)", [&]() -> std::string { return SwitchIds().size() >= 5 ? "" : "only " + std::to_string(SwitchIds().size()); });

    Attack("Test 79.2: turning a switch on three times and then 'Volver todo' returns every original registry value", [&]() -> std::string {
        for (const auto& id : SwitchIds()) {
            Rig r;
            for (int i = 0; i < 3; ++i) r.act->Set(id, true);
            size_t rev = 0, tot = 0;
            r.act->ResetAll(&rev, &tot);
            const std::string bad = r.Damaged();
            if (!bad.empty()) return "Set('" + id + "', true) x3 then ResetAll left: " + bad;
            if (!r.reg.memory_compression_enabled) return "memory compression left off after '" + id + "'";
        }
        return "";
    });

    Attack("Test 79.3: on, off, on, off for every switch ends where it started, with the original values back", [&]() -> std::string {
        for (const auto& id : SwitchIds()) {
            Rig r;
            r.act->Set(id, true);
            r.act->Set(id, false);
            r.act->Set(id, true);
            r.act->Set(id, false);
            size_t rev = 0, tot = 0;
            r.act->ResetAll(&rev, &tot);
            const std::string bad = r.Damaged();
            if (!bad.empty()) return "'" + id + "': " + bad;
        }
        return "";
    });

    Attack("Test 79.4: the same click twice (on-on, off-off) writes nothing the second time and answers 'Ya estaba' for the toggles", [&]() -> std::string {
        for (const std::string& id : {std::string("vram_reporting"), std::string("amd_driver_profile"), std::string("mpo_disabled"), std::string("memory_compression"), std::string("power_profile")}) {
            for (const bool on : {true, false}) {
                Rig r;
                r.act->Set(id, on);
                const size_t w = r.reg.writes.size();
                const int p = r.power.apply_count;
                const bool mc = r.reg.memory_compression_enabled;
                const std::string again = r.act->Set(id, on);
                if (r.reg.writes.size() != w || r.power.apply_count != p || r.reg.memory_compression_enabled != mc) return "'" + id + "' " + (on ? "on" : "off") + " twice wrote again";
                if (again.find("Ya estaba") == std::string::npos) return "'" + id + "' second " + (on ? "on" : "off") + " answered '" + again + "'";
            }
        }
        return "";
    });

    Attack("Test 79.5: hostile ids (empty, spaces, other case, NUL, huge, a path) write nothing and answer that there is no switch", [&]() -> std::string {
        Rig r;
        const std::vector<std::string> ids = {"", " ", " mpo_disabled", "mpo_disabled ", "MPO_DISABLED", std::string("mpo_disabled\0x", 14), std::string(100000, 'x'), "..\\..\\x", "memory_guard", "null", "0"};
        for (const auto& id : ids) {
            for (const bool on : {true, false}) {
                const std::string msg = r.act->Set(id, on);
                if (msg.find("no tiene interruptor") == std::string::npos) return "id '" + id.substr(0, 20) + "' answered '" + msg + "'";
            }
        }
        if (!r.reg.writes.empty() || r.power.apply_count != 0) return "something was written for a hostile id";
        return "";
    });

    Attack("Test 79.6: ResetAll with null counters does not crash and still puts the registry back", [&]() -> std::string {
        Rig r;
        for (const auto& id : SwitchIds()) r.act->Set(id, true);
        r.act->ResetAll(nullptr, nullptr);
        size_t rev = 99;
        r.act->ResetAll(&rev, nullptr);
        r.act->ResetAll(nullptr, &rev);
        const std::string bad = r.Damaged();
        return bad.empty() ? "" : bad;
    });

    Attack("Test 79.7: ResetAll twice: the second reports 0 of 0 and leaves every original value as it was", [&]() -> std::string {
        Rig r;
        for (const auto& id : SwitchIds()) r.act->Set(id, true);
        size_t rev = 0, tot = 0;
        r.act->ResetAll(&rev, &tot);
        const auto after_first = r.reg.values;
        size_t rev2 = 99, tot2 = 99;
        r.act->ResetAll(&rev2, &tot2);
        if (rev2 != 0 || tot2 != 0) return "second ResetAll reported " + std::to_string(rev2) + " of " + std::to_string(tot2);
        // The second call writes Windows' own defaults for settings that were never touched (documented in Test 73.5);
        // what must hold is that nothing that existed before the clicks is damaged and that nothing is restored twice.
        for (const auto& [k, v] : after_first) {
            const auto it = r.reg.values.find(k);
            if (it == r.reg.values.end() || !Same(it->second, v)) return "second ResetAll changed " + Narrow(std::get<2>(k));
        }
        return r.Damaged();
    });

    Attack("Test 79.8: with a pool of 0 MB the VRAM switch never writes a graphics-memory size of 0", [&]() -> std::string {
        Rig r(0);
        const std::string msg = r.act->Set("vram_reporting", true);
        for (const auto& [k, v] : r.reg.values) {
            const std::string n = Lower(Narrow(std::get<2>(k)));
            if ((n == "hardwareinformation.qwmemorysize" && v.qword == 0) || (n == "hardwareinformation.memorysize" && v.dword == 0)) return "wrote a 0 size for " + n + "; message '" + msg + "'";
        }
        if (msg.find("(0 MB") != std::string::npos) return "message announces a 0 MB pool: " + msg;
        return "";
    });

    Attack("Test 79.9: with the largest pool the VRAM switch writes a size that fits what it announces", [&]() -> std::string {
        Rig r(UINT32_MAX);
        const std::string msg = r.act->Set("vram_reporting", true);
        const auto it = r.reg.values.find(FakeRegistryPort::MakeKey(vegaflow::RegHive::LocalMachine, kAdapter, L"HardwareInformation.qwMemorySize"));
        if (it == r.reg.values.end()) return "no qword written";
        if (it->second.qword != (static_cast<uint64_t>(UINT32_MAX) << 20)) return "qword " + std::to_string(it->second.qword) + " for pool UINT32_MAX MB; message " + msg;
        const auto dw = r.reg.values.find(FakeRegistryPort::MakeKey(vegaflow::RegHive::LocalMachine, kAdapter, L"HardwareInformation.MemorySize"));
        if (dw == r.reg.values.end() || dw->second.dword != 0xFFFFFFFFU) return "32-bit size is not clamped to 0xFFFFFFFF for a pool of UINT32_MAX MB";
        return "";
    });

    Attack("Test 79.10: random click sequences (fixed seed) followed by 'Volver todo' always restore the originals; every answer is audited at least once and non-empty", [&]() -> std::string {
        const auto ids = SwitchIds();
        uint64_t x = 0x9E3779B97F4A7C15ULL;
        const auto next = [&]() {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        };
        for (int round = 0; round < 150; ++round) {
            Rig r;
            std::string trail;
            for (int i = 0; i < 25; ++i) {
                const std::string& id = ids[next() % ids.size()];
                const bool on = next() % 2;
                trail += id + (on ? "+ " : "- ");
                const size_t audits = r.audit.size();
                const std::string msg = r.act->Set(id, on);
                if (msg.empty()) return "empty answer after: " + trail;
                if (r.audit.size() < audits + 1) return "answer not audited after: " + trail;
            }
            size_t rev = 0, tot = 0;
            r.act->ResetAll(&rev, &tot);
            const std::string bad = r.Damaged();
            if (!bad.empty()) return "sequence [" + trail + "] then ResetAll left: " + bad;
            if (!r.reg.memory_compression_enabled) return "memory compression off after [" + trail + "]";
        }
        return "";
    });

    Attack("Test 79.14: one click on a switch leaves one audit line, not two", [&]() -> std::string {
        for (const std::string& id : {std::string("mpo_disabled"), std::string("memory_compression"), std::string("vram_reporting"), std::string("amd_driver_profile"), std::string("power_profile")}) {
            Rig r;
            r.act->Set(id, id == "memory_compression" ? false : true);
            if (r.audit.size() != 1) {
                std::string lines;
                for (const auto& l : r.audit) lines += "[" + l + "] ";
                return "'" + id + "' left " + std::to_string(r.audit.size()) + " audit lines: " + lines;
            }
        }
        return "";
    });

    Attack("Test 79.15: no answer of a switch promises FPS, 'mejora' or 'más rápido'", [&]() -> std::string {
        Rig r;
        for (const auto& id : SwitchIds()) {
            for (const bool on : {true, false, true}) {
                std::string t = Lower(r.act->Set(id, on));
                if (t.find("fps") != std::string::npos || t.find("mejora") != std::string::npos || t.find("más rápido") != std::string::npos || t.find("rápid") != std::string::npos) return "'" + id + "' answers: " + t;
            }
        }
        return "";
    });

    Attack("Test 79.11: 'Volver todo' on a clean machine never writes a graphics-memory size", [&]() -> std::string {
        Rig r;
        size_t rev = 0, tot = 0;
        r.act->ResetAll(&rev, &tot);
        for (const auto& w : r.reg.writes) {
            const std::string n = Lower(Narrow(w.name));
            if (n == "hardwareinformation.memorysize" || n == "hardwareinformation.qwmemorysize" || n == "dedicatedvideomemory") return "wrote " + n;
        }
        return r.Damaged().empty() ? "" : "originals damaged: " + r.Damaged();
    });

    Attack("Test 79.12: missing live_policy and audit callbacks are tolerated on every switch, and an Intel-like machine answers 'no disponible' without writing AMD values", [&]() -> std::string {
        Rig r(2048, false, true);
        for (const auto& id : SwitchIds()) {
            if (r.act->Set(id, true).empty()) return "empty answer for " + id;
            if (r.act->Set(id, false).empty()) return "empty answer for " + id;
        }
        Rig no_amd(2048, true, false);
        const std::string a = no_amd.act->Set("vram_reporting", true);
        const std::string b = no_amd.act->Set("amd_driver_profile", true);
        if (a.find("no disponible") == std::string::npos || b.find("no disponible") == std::string::npos) return "answers: '" + a + "' / '" + b + "'";
        for (const auto& w : no_amd.reg.writes) {
            const std::string n = Lower(Narrow(w.name));
            if (n.find("qwmemorysize") != std::string::npos || n == "shadercache" || n == "dedicatedvideomemory") return "AMD value written on a non-AMD machine: " + n;
        }
        return "";
    });

    Attack("Test 79.13: the power-profile answer states the ceiling that was actually applied", [&]() -> std::string {
        Rig r;
        r.policy.cpu_stapm_cap_mhz = 0; // boot policy: no ceiling
        vegaflow::ApuOptimizationPolicy live = r.policy;
        live.cpu_stapm_cap_mhz = 15000; // the live policy says 15000
        gs::SettingsActions act(gs::SettingsActionsDeps{*r.gfx, r.policy, [&]() { return live; }, nullptr});
        const std::string msg = act.Set("power_profile", true);
        const uint32_t applied = r.power.current.ac.size() > 3 ? r.power.current.ac[3] : 0xFFFFFFFF;
        const bool says_cap = msg.find("15000") != std::string::npos;
        if (says_cap && applied != 15000) return "message says '" + msg + "' but the plan holds a ceiling of " + std::to_string(applied) + " MHz";
        return "";
    });
}

} // namespace vftest
