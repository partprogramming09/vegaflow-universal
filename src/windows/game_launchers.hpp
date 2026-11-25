#pragma once
// Games installed through Epic and GOG. What a manifest or registry entry means is decided by the Rust core
// (policy/epic_manifest.rs, policy/gog_registry.rs); this adapter lists the manifest files and reads the registry.

#include "ports/registry.hpp"

#include <string>
#include <vector>

namespace vegaflow {

struct LauncherGameFolder {
    std::wstring dir;   // backslashes, no trailing separator
    bool anticheat;     // the manifest or entry names an anti-cheat: VegaFlow never modifies this game
};

// %ProgramData%\Epic\EpicGamesLauncher\Data\Manifests, or empty if ProgramData is unknown.
std::wstring DefaultEpicManifestsDir();

// The one place where two entries for the same folder (case-insensitive) become one: adds `game` to `list`, or, if the folder
// is already there, keeps one entry whose anti-cheat flag is set if either entry has it. Epic and GOG lists are merged with it
// in either order, so a folder that any launcher says has an anti-cheat is never a writable root. Does nothing past 512 games.
void MergeLauncherGame(std::vector<LauncherGameFolder>& list, LauncherGameFolder game);
void MergeLauncherGames(std::vector<LauncherGameFolder>& list, const std::vector<LauncherGameFolder>& more);

// The games listed by the *.item files in `manifests_dir`, in file-name order, no duplicate folders (case-insensitive).
// A broken, oversized or refused manifest is skipped. Empty if the folder is missing.
std::vector<LauncherGameFolder> ReadEpicGames(const std::wstring& manifests_dir);

// The games under HKLM\SOFTWARE\WOW6432Node\GOG.com\Games, no duplicate folders. An entry the Rust core refuses is skipped.
std::vector<LauncherGameFolder> ReadGogGames(RegistryPort& registry);

} // namespace vegaflow
