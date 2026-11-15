#pragma once
// Model of the "Juegos" screen: one row per Steam game with what VegaFlow can do to it, the exact list of what a
// profile changes (taken from the code that writes it, not retyped) and the exclusion behind "Me fue mal en este
// juego". Applying and restoring stay in the optimizer; this model only decides what to show and which buttons apply.

#include <cstdint>
#include <string>
#include <vector>

#include "model/hardware.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "session/config.hpp"

namespace vegaflow::session {

enum class GameStatus {
    NotApplied, // Unreal or Unity, no anti-cheat: a profile can be applied
    Applied,    // the profile is in the game's files
    Protected,  // anti-cheat: VegaFlow does not touch the game's files
    NoProfile,  // no profile exists for this engine
    Excluded,   // in excluded_game_exes: VegaFlow ignores it
};

struct GameRow {
    uint32_t app_id = 0;
    std::string name;
    std::string engine;    // Spanish label
    GameStatus status = GameStatus::NoProfile;
    std::string exe;       // file name, UTF-8
    std::string detail;    // one honest sentence about this game
    std::string file_hint; // which file a profile changes ("" when none)
    std::string backup_hint;
    std::vector<std::string> changes; // "key=value" lines, plus the registry value
    bool excluded = false;
    bool can_apply = false;
    bool can_restore = false;
    bool dxvk_installed = false;
    bool can_dxvk = false; // install or remove DXVK: not for excluded or anti-cheat games, needs the game's program
    std::string dxvk_hint;
};

struct GamesScreenModel {
    std::vector<GameRow> rows; // sorted by name
    std::string empty_text;
};

const char* StatusLabel(GameStatus status);

// `excluded` is process_policy.excluded_game_exes. `vram_pool_mb` is what ApplyEngineUmaPreset gets.
GamesScreenModel BuildGamesScreen(const std::vector<SteamGameEntry>& games, const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb,
                                  const std::vector<std::wstring>& excluded);

// Add / remove a game from excluded_game_exes by file name (any folder part is dropped, letter case ignored). The Rust
// rules decide which names are valid; ConfigStore::Save stays the authority. false = nothing changed; `error` says why
// when the name was the problem. The caller saves.
// True when the game's program name is in the excluded list (any letter case). A game with no program is never excluded.
bool IsExcludedGame(const std::vector<std::wstring>& excluded, const SteamGameEntry& game);

bool ExcludeGame(app::AppConfig& cfg, const std::string& exe_or_path, std::string* error = nullptr);
bool IncludeGame(app::AppConfig& cfg, const std::string& exe_or_path);

} // namespace vegaflow::session
