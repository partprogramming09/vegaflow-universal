#include "session/settings_screen.hpp"

namespace vegaflow {

bool SettingIsOn(const SettingSpec& spec, const SettingsScreenInputs& in) {
    const std::string id = spec.id;
    const SystemGraphicsTweaksState& t = in.tweaks;
    if (id == "memory_guard") return in.memory_guard_active;
    if (id == "vram_reporting") return t.amd_vram_override_active;
    if (id == "amd_driver_profile") return t.amd_shader_cache_forced;
    if (id == "priority_separation") return t.win32_priority_separation != 2; // 2 is the Windows default
    if (id == "paging_shield") return t.kernel_paging_shield_active;
    if (id == "mpo_disabled") return t.mpo_disabled;
    if (id == "mmcss_gaming") return t.mmcss_gaming_optimized;
    if (id == "game_dvr") return t.gamedvr_disabled;
    if (id == "memory_compression") return t.memory_compression_enabled;
    if (id == "power_profile") return t.apu_power_optimized;
    return false; // a catalog row without a case here is shown as off, never as applied
}

std::string EvidenceLabel(const SettingSpec& spec) {
    switch (spec.evidence) {
    case Evidence::Gain: return "Recomendado";
    case Evidence::Loss: return "No recomendado";
    case Evidence::NoEffect: return "Sin efecto medido";
    case Evidence::NotMeasured: break;
    }
    return "Sin medir";
}

SettingsScreenModel BuildSettingsScreen(const SettingsScreenInputs& in) {
    SettingsScreenModel model;
    model.experimental_notice = in.show_experimental;
    for (const FamilyView& view : VisibleFamilies(in.device, in.show_experimental)) {
        SettingsFamilyModel fam;
        fam.family = view.family;
        fam.title = view.title;
        fam.scope = view.scope;
        for (const SettingSpec* spec : view.rows) {
            fam.rows.push_back({spec, SettingIsOn(*spec, in), EvidenceLabel(*spec), ChangeDescription(*spec)});
        }
        model.families.push_back(std::move(fam));
    }
    return model;
}

} // namespace vegaflow
