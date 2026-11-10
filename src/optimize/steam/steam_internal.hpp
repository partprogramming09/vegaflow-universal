#pragma once
// Internal helpers shared by the implementation files of this module. Not part of the public API.

#include "optimize/steam/steam_engine_optimizer.hpp"
#include "windows/registry.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vegaflow::steam_internal {

bool EndsWithInsensitiveW(std::wstring_view str, std::wstring_view suffix);

bool EqualsInsensitiveW(std::wstring_view a, std::wstring_view b);

std::wstring Utf8ToWide(const std::string& str);

std::string ReadFileToString(const std::filesystem::path& path);

std::wstring ResolveUnrealEngineIniPath(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override);

std::vector<std::wstring> ResolveAllUnrealEngineIniPaths(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override);

std::wstring ResolveUnityBootConfigPath(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override);

} // namespace vegaflow::steam_internal
