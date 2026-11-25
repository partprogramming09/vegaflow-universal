#pragma once
// What the buttons of the "Juegos" screen do: apply or restore a game's profile, exclude it ("Me fue mal en este juego")
// or include it again, install or remove DXVK, and read the library again. The windows, files and registry are reached
// through the objects given here, so a test can drive them with fakes. Every method returns the sentence for the status bar.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "model/hardware.hpp"
#include "model/steam.hpp"
#include "ports/game_files.hpp"
#include "session/config.hpp"
#include "session/games_screen.hpp"

namespace vegaflow::session {

struct GameActionsDeps {
    GameFiles& files;
    app::AppConfig& config;
    app::ConfigStore& store;
    std::vector<SteamGameEntry>& games;              // the library as scanned; the actions keep its flags current
    ApuOptimizationPolicy policy;                    // the one the profile is built from
    std::function<std::vector<SteamGameEntry>()> scan;                  // reads the Steam library again
    std::function<SteamGameEntry(const SteamGameEntry&)> inspect;       // reads one game's folder again (fresh flags)
    std::function<void(const std::string&)> audit;                      // one line in the audit log
};

class GameActions {
public:
    explicit GameActions(GameActionsDeps deps) : d_(std::move(deps)) {}
    void SetPolicy(const ApuOptimizationPolicy& policy) { d_.policy = policy; } // after a GPU change

    GamesScreenModel Poll() const;
    // Reads the files again: the daemon writes game files when a game opens, so what the scan found goes stale. 0 = every game.
    void RefreshFlags(uint32_t only_app_id);

    std::string Rescan();
    std::string Apply(uint32_t app_id);
    std::string Restore(uint32_t app_id);
    std::string Exclude(uint32_t app_id);
    std::string Include(uint32_t app_id);
    std::string ToggleDxvk(uint32_t app_id);

private:
    SteamGameEntry* Find(uint32_t app_id);
    std::string Said(const std::string& msg);
    std::string NotFound(uint32_t app_id);

    GameActionsDeps d_;
};

} // namespace vegaflow::session
