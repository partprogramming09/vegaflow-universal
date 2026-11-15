#pragma once
// What the switches of the "Ajustes" screen do: one switch sets a row of the catalog on or off through the graphics
// optimizer (and so through its registry and power ports), and every answer is the sentence for the status bar.

#include <cstddef>
#include <functional>
#include <string>

#include "model/hardware.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"

namespace vegaflow::session {

struct SettingsActionsDeps {
    GraphicsOptimizer& gfx;
    ApuOptimizationPolicy policy;                                // the boot policy
    std::function<ApuOptimizationPolicy()> live_policy;          // the same, with the video budget read now
    std::function<void(const std::string&)> audit;               // one line in the audit log
};

class SettingsActions {
public:
    explicit SettingsActions(SettingsActionsDeps deps) : d_(std::move(deps)) {}

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

    SettingsActionsDeps d_;
};

} // namespace vegaflow::session
