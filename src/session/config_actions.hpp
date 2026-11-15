#pragma once
// What the edits of "Configuración" and "Programas" do: each one changes a copy of the configuration, is checked by the
// rules the daemon reads it with, and `config` follows only when config.json was written. Every answer is the sentence
// for the status bar; a refused edit leaves the file and the screen as they were.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "model/hardware.hpp"
#include "session/config.hpp"

namespace vegaflow::session {

class ConfigActions {
public:
    ConfigActions(app::AppConfig& config, app::ConfigStore& store, std::vector<SteamGameEntry>& games,
                  std::function<void(const std::string&)> audit)
        : config_(config), store_(store), games_(games), audit_(std::move(audit)) {}

    std::string SetNumber(const std::string& id, const std::string& text);
    std::string SetToggle(const std::string& id, bool on);
    std::string Reset(); // factory values for what Configuración shows; games, exceptions and the UMA flag stay

    std::string SetCategoryAction(const std::string& id, int action);
    std::string AddException(const std::string& exe);
    std::string SetExceptionAction(const std::string& exe, int action);
    std::string RemoveException(const std::string& exe);
    std::string AddManualGame(const std::string& exe);
    std::string RemoveManualGame(const std::string& exe);

private:
    std::string Save(const app::AppConfig& candidate, const std::string& done);
    std::string EditPrograms(const std::function<bool(app::AppConfig&, std::string*)>& change);

    app::AppConfig& config_;
    app::ConfigStore& store_;
    std::vector<SteamGameEntry>& games_;
    std::function<void(const std::string&)> audit_;
};

} // namespace vegaflow::session
