#pragma once
// Where Steam is installed and which library folders it lists. The library list is read by the Rust core
// (policy/steam_library.rs); this adapter only finds and reads the file.

#include <string>
#include <vector>

namespace vegaflow {

// Steam's install folder from the registry (HKCU SteamPath, then HKLM InstallPath), or the default folder if it exists.
// Empty if none exists.
std::wstring FindSteamInstallPath();

// The library folders listed in <steam_install>\steamapps\libraryfolders.vdf, in file order, with steam_install itself
// first and no duplicates (case-insensitive). Empty if steam_install is empty.
std::vector<std::wstring> ReadSteamLibraryFolders(const std::wstring& steam_install);

} // namespace vegaflow
