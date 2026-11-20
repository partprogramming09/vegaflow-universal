#pragma once
// The table behind the "Ajustes del sistema" screen: one row per system setting, with its family, the hardware it
// applies to, whether it is experimental, what was measured about it, and the exact change it makes. The screen, the
// (i) text and the "Recomendado" label are all generated from these rows, never written apart (no <windows.h>).

#include "model/hardware.hpp"

#include <string>
#include <vector>

namespace vegaflow {

enum class SettingFamily { SharedMemory, AmdDriver, Windows };

// What docs/measurements/verification-status.md records for the setting on the test machine.
enum class Evidence { NotMeasured, NoEffect, Gain, Loss };

struct SettingSpec {
    const char* id = "";
    SettingFamily family = SettingFamily::Windows;
    const char* name = "";   // plain-language title
    const char* why = "";    // one sentence: what it is for
    bool experimental = false; // hidden until the user turns experimental features on
    Evidence evidence = Evidence::NotMeasured;
    bool needs_amd = false;        // only when the primary GPU is AMD
    bool needs_integrated = false; // only on integrated (shared-memory) graphics
    const char* target = "";  // exact registry key and value, or the power-plan setting, or what is watched
    const char* change = "";  // exact value written, or what the watcher does
    bool undoable = true;     // false for one-shot actions that leave no saved original (nothing to undo)
    bool applied_by_default = false; // the daemon applies it without the user asking (see 14-fase-g.md, pending decision)
    bool user_switch = true;  // the person can turn it on and off; false when VegaFlow decides by itself (memory guard)
};

// What the screen needs to know about the machine; filled from the device profile (phase 3).
struct DeviceFacts {
    GpuVendor vendor = GpuVendor::Unknown;
    GpuConnection connection = GpuConnection::Unknown;
    // The optimizer only creates the AMD driver family when this is true (policy.block_driver_tweaks is false).
    bool driver_tweaks_allowed = false;
};

struct FamilyView {
    SettingFamily family = SettingFamily::Windows;
    std::string title;
    std::string scope;
    std::vector<const SettingSpec*> rows;
};

const std::vector<SettingSpec>& SettingsCatalog();
const SettingSpec* FindSetting(const std::string& id);

bool Applies(const SettingSpec& spec, const DeviceFacts& device);
bool IsRecommended(const SettingSpec& spec);    // measured gain only
bool IsNotRecommended(const SettingSpec& spec); // measured loss only

// Families with at least one visible row, in screen order. Experimental rows appear only with show_experimental.
std::vector<FamilyView> VisibleFamilies(const DeviceFacts& device, bool show_experimental);

// The text behind the (i) button: what changes exactly and that it can be undone. Built from the row.
std::string ChangeDescription(const SettingSpec& spec);

} // namespace vegaflow
