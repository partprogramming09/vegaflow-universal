#pragma once
// F4.4: what happens when config.json is corrupt, from a newer version, has a rejected value or cannot be read. VegaFlow
// never overwrites it by itself (ConfigStore); the person chooses, through the callbacks, so the logic is testable.

#include <functional>
#include <string>

#include "session/config.hpp"

namespace vegaflow::app {

enum class ConfigChoice { Dismiss, FactoryReset, OpenFile };

struct ConfigRecoveryOffer {
    bool factory_reset = false; // the file can be read and copied, so it can be replaced
    bool open_file = false;
};

ConfigRecoveryOffer RecoveryOffer(const ConfigLoadReport& report);

struct ConfigRecoveryDeps {
    std::function<ConfigChoice(const ConfigLoadReport&, const ConfigRecoveryOffer&)> ask;
    std::function<bool(const std::wstring&)> open_file; // opens the file for editing (Windows, wired by app/)
};

// Asks only when there is something to choose. "Factory values" copies the file first, then reloads `config` and the
// store's report. Returns the sentence for the status bar and the log, empty when nothing was asked. `dry_run` writes nothing.
std::string ResolveConfigProblem(ConfigStore& store, AppConfig& config, const ConfigRecoveryDeps& deps, bool dry_run = false);

} // namespace vegaflow::app
