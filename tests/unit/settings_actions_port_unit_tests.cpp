// Unit tests (F4.3): SettingsActions drives the Ajustes switches through the TweakControls port, so a fake system gives the
// whole behavior (which write, which sentence, which audit line, never a dry run) on any host.

#include "session/settings_actions.hpp"
#include "fake_tweak_controls.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {
bool Has(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}
} // namespace

void Test148() {
    namespace gs = vegaflow::session;
    vegaflow::ApuOptimizationPolicy policy{};
    policy.safe_vram_pool_mb = 2048;

    // 1. A toggle flips the current position through the port, says the sentence, writes one audit line and never asks a dry run.
    {
        FakeTweakControls sys;
        std::vector<std::string> audit;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, nullptr, [&](const std::string& s) { audit.push_back(s); }, nullptr});
        const std::string on = act.ToggleMpo();
        const bool first = sys.state.mpo_disabled && on.find("Desactivado") != std::string::npos && audit.size() == 1;
        const std::string off = act.ToggleMpo();
        RecordResult(first && !sys.state.mpo_disabled && off.find("Restaurado") != std::string::npos && Has(sys.calls, "Mpo(1)") && Has(sys.calls, "Mpo(0)") &&
                         !sys.any_dry_run,
                     "Test 148.1: ToggleMpo flips the system twice through the port, with one audit line each and no dry run");
    }

    // 2. Set(id, on) leaves a row alone when it is already there (a second click never undoes) and flips it otherwise.
    {
        FakeTweakControls sys;
        sys.state.mpo_disabled = true;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, nullptr, nullptr, nullptr});
        const std::string same = act.Set("mpo_disabled", true);
        const bool unchanged = sys.calls.empty() && same.find("Ya estaba") != std::string::npos;
        act.Set("mpo_disabled", false);
        RecordResult(unchanged && !sys.state.mpo_disabled && Has(sys.calls, "Mpo(0)"),
                     "Test 148.2: Set leaves a row that is already in place and flips it when it differs");
    }

    // 3. The user's own "off" passes user_off=true; turning on passes false; an unknown id writes nothing.
    {
        FakeTweakControls sys;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, nullptr, nullptr, nullptr});
        act.Set("mmcss_gaming", true);
        act.Set("mmcss_gaming", false);
        act.Set("priority_separation", false);
        const size_t before = sys.calls.size();
        const std::string none = act.Set("no_such_switch", true);
        RecordResult(Has(sys.calls, "Mmcss(1)") && Has(sys.calls, "Mmcss(0,user_off)") && Has(sys.calls, "Priority2(1,user_off)") && sys.calls.size() == before &&
                         none.find("no tiene interruptor") != std::string::npos,
                     "Test 148.3: turning a row off passes user_off, turning it on does not, and an unknown id writes nothing");
    }

    // 4. Without an AMD driver or a power mode the switches say so and write nothing.
    {
        FakeTweakControls sys;
        sys.amd = false;
        sys.power = false;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, nullptr, nullptr, nullptr});
        const std::string a = act.ToggleShaderCache();
        const std::string b = act.ToggleVramOverride();
        const std::string c = act.ToggleApuPowerMode();
        RecordResult(sys.calls.empty() && a.find("no disponible") != std::string::npos && b.find("no disponible") != std::string::npos &&
                         c.find("no disponible") != std::string::npos,
                     "Test 148.4: AMD and power-mode switches say 'not available' and touch nothing when the port has no such family");
    }

    // 5. The VRAM override uses the live policy's pool, and refuses a zero pool without writing.
    {
        FakeTweakControls sys;
        vegaflow::ApuOptimizationPolicy live = policy;
        live.safe_vram_pool_mb = 3072;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, [&]() { return live; }, nullptr, nullptr});
        act.ToggleVramOverride();
        const bool used_live = Has(sys.calls, "VramOverride3072(1)") && sys.state.amd_vram_override_active;
        live.safe_vram_pool_mb = 0;
        sys.state.amd_vram_override_active = false;
        sys.calls.clear();
        const std::string refused = act.ToggleVramOverride();
        RecordResult(used_live && sys.calls.empty() && refused.find("no se cambió nada") != std::string::npos,
                     "Test 148.5: the VRAM override takes the live pool and a zero pool changes nothing");
    }

    // 6. A failed write is reported, and ResetAll gives every family back (no dry run) before the saved registry copies.
    {
        FakeTweakControls sys;
        sys.writes_succeed = false;
        gs::SettingsActions act(gs::SettingsActionsDeps{sys, policy, nullptr, nullptr, nullptr});
        const std::string fail = act.ToggleMpo();
        FakeTweakControls sys2;
        gs::SettingsActions act2(gs::SettingsActionsDeps{sys2, policy, nullptr, nullptr, nullptr});
        size_t rev = 0, tot = 0;
        act2.ResetAll(&rev, &tot);
        RecordResult(fail.find("Fallo") != std::string::npos && !sys2.any_dry_run && rev == 3 && tot == 5 && !sys2.calls.empty() &&
                         sys2.calls.back() == "RestoreAllBackups" && Has(sys2.calls, "ShaderCache(0)") && Has(sys2.calls, "VramRestore(1)") &&
                         Has(sys2.calls, "RevertMemCompression(1)") && Has(sys2.calls, "LaptopProfile(0)") && Has(sys2.calls, "Priority2(1)"),
                     "Test 148.6: a failed write is reported, and ResetAll gives every family back, then the saved copies, with no dry run");
    }
}

} // namespace vftest
