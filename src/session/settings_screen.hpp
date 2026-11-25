#pragma once
// The model the "Ajustes del sistema" screen reads: what is visible on this machine, whether each setting is on
// right now, its evidence label and its (i) text. The screen only draws it and decides nothing (no <windows.h>).

#include "model/tweaks.hpp"
#include "session/settings_catalog.hpp"

#include <string>
#include <vector>

namespace vegaflow {

// E2.2: policy/memory_compression.rs through the bridge (0 allowed, 1 discouraged, 2 unknown).
enum class CompressionAdvice { Allowed, Discouraged, Unknown };

struct SettingsScreenInputs {
    CompressionAdvice compression_advice = CompressionAdvice::Unknown; // for the memory_compression_off row
    DeviceFacts device;
    bool show_experimental = false;
    bool auto_engine_presets = false; // F2.3: the config.json key behind the engine_presets row
    SystemGraphicsTweaksState tweaks; // what QuerySystemGraphicsTweaks() read
    bool memory_guard_active = false; // policy.apply_uma_memory_guard and the daemon is not in safe mode
    bool elevated = true;             // VegaFlow runs as administrator (the preview says what happens when it does not)
};

struct SettingRow {
    const SettingSpec* spec = nullptr;
    bool on = false;
    std::string evidence_label; // "Recomendado", "No recomendado", "Sin efecto medido" or "Sin medir"
    std::string tip;            // the (i) text
    std::string preview;        // what pressing the switch would do now, written without touching anything
    bool discouraged = false;   // E2.2: the machine's RAM says to leave this off (shown as "Desaconsejado")
    bool needs_confirm = false; // E2.2: turning it on must ask first (discouraged, or the RAM could not be read)
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
// "What would happen on your PC": the state now, the exact write, whether it can be undone. Nothing is changed to build it.
std::string SettingPreview(const SettingSpec& spec, bool on_now, bool elevated);
SettingsScreenModel BuildSettingsScreen(const SettingsScreenInputs& in);

} // namespace vegaflow
