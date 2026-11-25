#pragma once
// config.json reading and saving, and the merge of the Steam library into the custom game list.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "model/game.hpp"
#include "model/memory.hpp"
#include "model/steam.hpp"
#include "ports/file_system.hpp"

namespace vegaflow::app {

struct AppConfig {
    uint32_t poll_interval_ms = 1500;
    bool restrict_game_cpu_sets = false; // false: the game uses all threads and only the background is confined
    // E2.2: system tweaks the daemon applies by itself; off unless config.json says true (none was measured as a gain).
    bool auto_vram_reporting = false;
    bool auto_priority_separation = false;
    bool auto_game_dvr_off = false;
    bool auto_power_profile = false;
    bool auto_engine_presets = false; // F2.3: the 28 Unreal Engine values of Engine.ini (a game file), same rule
    vegaflow::MemoryPurgeConfig memory{};
    vegaflow::ProcessPolicyConfig process_policy{};
    std::vector<std::wstring> custom_game_exes;
    std::vector<std::wstring> user_custom_game_exes; // Original exes from config.json
};

// F4.4: what is wrong with config.json, the most serious first.
enum class ConfigProblem { None, UntrustedFolder, Unreadable, Corrupt, Rejected, NewerVersion };

// What reading config.json found. The text for the person is in Warning().
struct ConfigLoadReport {
    uint32_t status = 0;         // VF_CONFIG_* bits (bridge/rust_bridge.h)
    uint32_t rejected_keys = 0;  // VF_CONFIG_KEY_* bits
    bool folder_trusted = true;  // only administrators can write the folder (windows/config_location.hpp)
    bool unreadable = false;     // the file is there but could not be read, or is too large

    bool CanSave() const;
    std::string Warning() const; // empty when there is nothing to tell
    ConfigProblem Problem() const;
};

// Reads and saves one config.json through the FileSystem port; the rules are in the Rust core (policy/config_file.rs).
// Saving never overwrites a file it could not read whole: corrupt, with a rejected value, from a newer version,
// unreadable or in an untrusted folder. The person decides first.
class ConfigStore {
public:
    ConfigStore(vegaflow::FileSystem& fs, std::wstring path, bool folder_trusted);

    AppConfig Load();
    const ConfigLoadReport& Report() const { return report_; }
    const std::wstring& Path() const { return path_; }
    // F4.4, the person chose "factory values": copies the file to `.vegaflow.bak` (`.vegaflow.bak.1`... when one is there;
    // an older copy is never overwritten), checks the copy, then replaces the file with a clean one. False, with the file
    // untouched, when it cannot be read, the copy fails or differs, or the folder is not trusted. Call Load() after a
    // success to read the new state. `dry_run` checks only.
    bool ResetToFactory(bool dry_run = false, std::string* why = nullptr);
    // False, with nothing written, when the report blocks saving, the file changed into something that blocks it, a
    // value breaks its rule or the write fails; `why` then gets the reason for the person. `dry_run` checks only.
    bool Save(const AppConfig& cfg, bool dry_run = false, std::string* why = nullptr);

private:
    vegaflow::FileSystem& fs_;
    std::wstring path_;
    ConfigLoadReport report_;
    // The known settings as last read or written by VegaFlow: a file whose known settings differ was edited by hand.
    std::string last_known_;
};
void SyncSteamGamesToCustomExes(const std::vector<vegaflow::SteamGameEntry>& steam_games, AppConfig& cfg);

} // namespace vegaflow::app
