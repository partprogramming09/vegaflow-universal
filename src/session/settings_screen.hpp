#pragma once
// The model the "Ajustes del sistema" screen reads: what is visible on this machine, whether each setting is on
// right now, its evidence label and its (i) text. The screen only draws it and decides nothing (no <windows.h>).

#include "optimize/graphics/graphics_optimizer.hpp"
#include "session/settings_catalog.hpp"

#include <string>
#include <vector>

namespace vegaflow {

struct SettingsScreenInputs {
    DeviceFacts device;
    bool show_experimental = false;
    SystemGraphicsTweaksState tweaks; // what QuerySystemGraphicsTweaks() read
    bool memory_guard_active = false; // policy.apply_uma_memory_guard and the daemon is not in safe mode
};

struct SettingRow {
    const SettingSpec* spec = nullptr;
    bool on = false;
    std::string evidence_label; // "Recomendado", "No recomendado", "Sin efecto medido" or "Sin medir"
    std::string tip;            // the (i) text
};

struct SettingsFamilyModel {
    SettingFamily family = SettingFamily::Windows;
    std::string title;
    std::string scope;
    std::vector<SettingRow> rows;
};

struct SettingsScreenModel {
    std::vector<SettingsFamilyModel> families;
    bool experimental_notice = false; // show the warning banner (experimental features are on)
};

bool SettingIsOn(const SettingSpec& spec, const SettingsScreenInputs& in);
std::string EvidenceLabel(const SettingSpec& spec);
SettingsScreenModel BuildSettingsScreen(const SettingsScreenInputs& in);

} // namespace vegaflow
