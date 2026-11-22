#pragma once
// Model of the "Configuración" screen: the settings of config.json that the daemon reads, as rows with the value in
// the unit the person reads. Parsing of what the person types lives here (tested); the allowed ranges stay in the
// Rust core and are checked by ConfigStore::Save.

#include <string>
#include <vector>

#include "session/config.hpp"

namespace vegaflow::session {

enum class ConfigRowKind { Toggle, Number };

struct ConfigRow {
    std::string id;
    ConfigRowKind kind = ConfigRowKind::Number;
    std::string name;
    std::string help;
    std::string value; // Number: the text shown in the field
    std::string unit;  // Number: "segundos", "MB", "MB/s"
    bool on = false;   // Toggle
    std::string chip;  // "No recomendado" or empty
    bool applies_live = true; // E2.7: false when the daemon reads it only at start (policy/config_rules.rs applies_live)
};

struct ConfigGroup {
    std::string title;
    std::string note;
    std::vector<ConfigRow> rows;
};

struct ConfigScreenModel {
    std::vector<ConfigGroup> groups;
};

ConfigScreenModel BuildConfigScreen(const app::AppConfig& cfg);

// Sets one number from what the person typed. False, with `error` in plain Spanish and `cfg` untouched, when the text is
// not a whole number (seconds of the poll interval also take one comma or dot and up to three decimals) or the id is unknown.
bool ApplyConfigNumber(app::AppConfig& cfg, const std::string& id, const std::string& text, std::string* error = nullptr);
bool ApplyConfigToggle(app::AppConfig& cfg, const std::string& id, bool on);

} // namespace vegaflow::session
