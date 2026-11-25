#pragma once
// What the switches of the "Ajustes" screen do: one switch sets a row of the catalog on or off through the TweakControls
// port (the optimizer behind it writes the registry and the power plan), and every answer is the sentence for the status bar.

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

#include "model/hardware.hpp"
#include "ports/tweak_controls.hpp"

namespace vegaflow::session {

struct SettingsActionsDeps {
    TweakControls& gfx;
    ApuOptimizationPolicy policy;                                // the boot policy
    std::function<ApuOptimizationPolicy()> live_policy;          // the same, with the video budget read now
    std::function<void(const std::string&)> audit;               // one line in the audit log
    std::function<std::string(bool)> set_engine_presets;         // F2.3: saves auto_engine_presets in config.json; returns the sentence
};

class SettingsActions {
public:
    explicit SettingsActions(SettingsActionsDeps deps) : d_(std::move(deps)) {}
    void SetPolicy(const ApuOptimizationPolicy& policy) { d_.policy = policy; } // after a GPU change

    // Sets the catalog row `id` to `turn_on`. Rows that flip (a toggle) change only when the current state differs, so a
    // second click never undoes by accident. An id with no switch changes nothing.
    std::string Set(const std::string& id, bool turn_on);

    std::string ToggleMpo();
    std::string ToggleMemCompression();
    std::string ToggleShaderCache();
    std::string ToggleVramOverride();
    std::string ToggleMmcssGaming();
    std::string ToggleApuPowerMode();

    // "Volver todo al valor de Windows": every setting of the catalog goes back, then the saved registry originals are put back.
    void ResetAll(size_t* reverted, size_t* total);

private:
    std::string Said(const std::string& msg, const char* audit_prefix);

    bool quiet_ = false; // Set audits its own line, so the toggle it calls must not add a second

    SettingsActionsDeps d_;
};

} // namespace vegaflow::session
