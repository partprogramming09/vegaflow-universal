#include "steam_engine_optimizer.hpp"
#include "hardware_profiler.hpp"
#include "graphics_optimizer.hpp"
#include "dxvk_manager.hpp"
#include "sandbox_guard.hpp"

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

namespace vegaflow {

namespace {

bool ContainsInsensitive(std::string_view haystack, std::string_view needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }
    );
    return it != haystack.end();
}

bool ContainsInsensitiveW(std::wstring_view haystack, std::wstring_view needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(a) == std::towlower(b);
        }
    );
    return it != haystack.end();
}

bool EndsWithInsensitiveW(std::wstring_view str, std::wstring_view suffix) {
    if (suffix.size() > str.size()) return false;
    auto tail = str.substr(str.size() - suffix.size());
    return std::equal(
        tail.begin(), tail.end(),
        suffix.begin(), suffix.end(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(a) == std::towlower(b);
        }
    );
}

bool EqualsInsensitiveW(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(
        a.begin(), a.end(),
        b.begin(), b.end(),
        [](wchar_t ca, wchar_t cb) {
            return std::towlower(ca) == std::towlower(cb);
        }
    );
}

bool EqualsInsensitive(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(
        a.begin(), a.end(),
        b.begin(), b.end(),
        [](char ca, char cb) {
            return std::tolower(static_cast<unsigned char>(ca)) ==
                   std::tolower(static_cast<unsigned char>(cb));
        }
    );
}

std::string_view TrimWhitespace(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string FormatFloatTrimmed(float val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", val);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s.empty() ? "0" : s;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int req = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (req <= 0) return std::wstring(str.begin(), str.end());
    std::wstring out(req, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out.data(), req);
    return out;
}

std::string ReadFileToString(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) return "";
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool WriteStringToFile(const std::filesystem::path& path, const std::string& content, bool is_anticheat_protected = false) {
    return SandboxGuard::SafeWriteFile(path.wstring(), content, is_anticheat_protected);
}

bool QueryRegString(HKEY hRoot, LPCWSTR subKey, LPCWSTR valueName, std::wstring& outValue) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(hRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buf[1024]{};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    LONG res = RegQueryValueExW(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hKey);
    if (res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        outValue = buf;
        return true;
    }
    return false;
}

bool ExtractVdfValue(const std::string& line, const std::string& key, std::string& outValue) {
    std::string needle = "\"" + key + "\"";
    size_t p = line.find(needle);
    if (p == std::string::npos) return false;
    size_t q1 = line.find('\"', p + needle.size());
    if (q1 == std::string::npos) return false;
    size_t q2 = line.find('\"', q1 + 1);
    if (q2 == std::string::npos) return false;
    outValue = line.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

std::wstring NormalizeAlnumLowerW(const std::wstring& s) {
    std::wstring res;
    res.reserve(s.size());
    for (wchar_t ch : s) {
        if (iswalnum(ch)) {
            res.push_back(static_cast<wchar_t>(towlower(ch)));
        }
    }
    return res;
}

std::wstring ResolveUnrealEngineIniPath(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override) {

    if (!custom_config_dir_override.empty()) {
        std::filesystem::path p(custom_config_dir_override);
        if (p.filename() == L"Engine.ini") {
            return p.wstring();
        }
        return (p / L"Engine.ini").wstring();
    }

    std::wstring projectName;
    if (!game.primary_exe_path.empty()) {
        std::filesystem::path exePath(game.primary_exe_path);
        std::wstring exeStem = exePath.stem().wstring();
        const std::wstring suffix1 = L"-Win64-Shipping";
        const std::wstring suffix2 = L"-WinGDK-Shipping";
        if (EndsWithInsensitiveW(exeStem, suffix1)) {
            projectName = exeStem.substr(0, exeStem.size() - suffix1.size());
        } else if (EndsWithInsensitiveW(exeStem, suffix2)) {
            projectName = exeStem.substr(0, exeStem.size() - suffix2.size());
        } else {
            projectName = exeStem;
        }
    }

    if (projectName.empty() && !game.install_dir.empty()) {
        std::filesystem::path instPath(game.install_dir);
        projectName = instPath.filename().wstring();
    }

    if (projectName.empty()) {
        projectName = L"UnrealProject";
    }

    wchar_t localAppBuf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppBuf, MAX_PATH);
    std::filesystem::path localAppPath = (len > 0) ? std::filesystem::path(localAppBuf) : std::filesystem::path(L"C:\\");

    std::filesystem::path configBase = localAppPath / projectName / L"Saved" / L"Config";
    std::error_code ec;

    std::filesystem::path candidates[] = {
        configBase / L"WindowsNoEditor" / L"Engine.ini",
        configBase / L"WindowsClient" / L"Engine.ini",
        configBase / L"Windows" / L"Engine.ini"
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand, ec)) {
            return cand.wstring();
        }
    }

    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand.parent_path(), ec)) {
            return cand.wstring();
        }
    }

    return (configBase / L"WindowsNoEditor" / L"Engine.ini").wstring();
}

std::vector<std::wstring> ResolveAllUnrealEngineIniPaths(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override) {

    if (!custom_config_dir_override.empty()) {
        return { ResolveUnrealEngineIniPath(game, custom_config_dir_override) };
    }

    std::vector<std::wstring> paths;
    std::error_code ec;

    // 1. Default %LOCALAPPDATA%\<ProjectName>\Saved\Config\WindowsNoEditor\Engine.ini
    std::wstring defaultPath = ResolveUnrealEngineIniPath(game, L"");
    if (!defaultPath.empty()) {
        paths.push_back(defaultPath);
    }

    // Determine projectName and normalized game name
    std::wstring projectName;
    if (!game.primary_exe_path.empty()) {
        std::filesystem::path exePath(game.primary_exe_path);
        std::wstring exeStem = exePath.stem().wstring();
        const std::wstring suffix1 = L"-Win64-Shipping";
        const std::wstring suffix2 = L"-WinGDK-Shipping";
        if (EndsWithInsensitiveW(exeStem, suffix1)) {
            projectName = exeStem.substr(0, exeStem.size() - suffix1.size());
        } else if (EndsWithInsensitiveW(exeStem, suffix2)) {
            projectName = exeStem.substr(0, exeStem.size() - suffix2.size());
        } else {
            projectName = exeStem;
        }
    }
    if (projectName.empty() && !game.install_dir.empty()) {
        std::filesystem::path instPath(game.install_dir);
        projectName = instPath.filename().wstring();
    }
    std::wstring gameName = Utf8ToWide(game.name);

    std::wstring normProject = NormalizeAlnumLowerW(projectName);
    std::wstring normGame = NormalizeAlnumLowerW(gameName);

    auto matches_game = [&](const std::wstring& dirName) -> bool {
        std::wstring normDir = NormalizeAlnumLowerW(dirName);
        if (normDir.empty()) return false;

        auto prefix_len = [](const std::wstring& a, const std::wstring& b) -> size_t {
            size_t c = 0;
            size_t m = std::min(a.size(), b.size());
            while (c < m && a[c] == b[c]) c++;
            return c;
        };

        if (prefix_len(normDir, normProject) >= 8 || prefix_len(normDir, normGame) >= 8) {
            return true;
        }
        if (normProject.size() >= 5 && normDir.find(normProject) != std::wstring::npos) return true;
        if (normGame.size() >= 5 && normDir.find(normGame) != std::wstring::npos) return true;
        if (normDir.size() >= 5 && (normProject.find(normDir) != std::wstring::npos || normGame.find(normDir) != std::wstring::npos)) return true;
        return false;
    };

    // Candidates for "My Games" root folders
    std::vector<std::filesystem::path> myGamesRoots;

    // A. HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders -> Personal
    std::wstring personalPath;
    if (QueryRegString(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders", L"Personal", personalPath)) {
        wchar_t expanded[MAX_PATH]{};
        DWORD expLen = ExpandEnvironmentStringsW(personalPath.c_str(), expanded, MAX_PATH);
        if (expLen > 0 && expLen < MAX_PATH) {
            personalPath = expanded;
        }
        std::filesystem::path pPersonal(personalPath);
        if (std::filesystem::exists(pPersonal / L"My Games", ec)) {
            myGamesRoots.push_back(pPersonal / L"My Games");
        }
    }

    // B. Known %USERPROFILE% locations
    wchar_t userProfBuf[MAX_PATH]{};
    DWORD upLen = GetEnvironmentVariableW(L"USERPROFILE", userProfBuf, MAX_PATH);
    if (upLen > 0 && upLen < MAX_PATH) {
        std::filesystem::path up(userProfBuf);
        myGamesRoots.push_back(up / L"Documents" / L"My Games");
        myGamesRoots.push_back(up / L"OneDrive" / L"Documentos" / L"My Games");
        myGamesRoots.push_back(up / L"OneDrive" / L"Documents" / L"My Games");
    }

    // Scan each My Games root
    for (const auto& root : myGamesRoots) {
        if (!std::filesystem::exists(root, ec)) continue;

        for (const auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;

            std::wstring dirName = entry.path().filename().wstring();
            if (matches_game(dirName)) {
                // Recursively look for WindowsNoEditor, WindowsClient, Windows within Saved\Config
                for (auto it = std::filesystem::recursive_directory_iterator(
                         entry.path(), std::filesystem::directory_options::skip_permission_denied, ec);
                     it != std::filesystem::recursive_directory_iterator();) {
                    if (ec) break;
                    if (it.depth() > 4) {
                        it.pop(ec);
                        continue;
                    }

                    if (it->is_directory(ec)) {
                        std::wstring subName = it->path().filename().wstring();
                        if (_wcsicmp(subName.c_str(), L"WindowsNoEditor") == 0 ||
                            _wcsicmp(subName.c_str(), L"WindowsClient") == 0 ||
                            _wcsicmp(subName.c_str(), L"Windows") == 0) {
                            std::filesystem::path parent = it->path().parent_path();
                            if (_wcsicmp(parent.filename().wstring().c_str(), L"Config") == 0) {
                                std::filesystem::path candIni = it->path() / L"Engine.ini";
                                paths.push_back(candIni.wstring());
                            }
                        }
                    }
                    it.increment(ec);
                }
            }
        }
    }

    // Deduplicate
    std::vector<std::wstring> uniquePaths;
    for (const auto& p : paths) {
        bool dup = false;
        for (const auto& u : uniquePaths) {
            if (_wcsicmp(u.c_str(), p.c_str()) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            uniquePaths.push_back(p);
        }
    }

    return uniquePaths;
}

std::wstring ResolveUnityBootConfigPath(
    const SteamGameEntry& game,
    const std::wstring& custom_config_dir_override) {

    if (!custom_config_dir_override.empty()) {
        std::filesystem::path p(custom_config_dir_override);
        if (p.filename() == L"boot.config") {
            return p.wstring();
        }
        return (p / L"boot.config").wstring();
    }

    if (game.install_dir.empty()) {
        return L"";
    }

    std::error_code ec;
    // Check direct children first
    for (const auto& entry : std::filesystem::directory_iterator(game.install_dir, ec)) {
        if (entry.is_directory(ec)) {
            std::wstring dirname = entry.path().filename().wstring();
            if (EndsWithInsensitiveW(dirname, L"_Data")) {
                return (entry.path() / L"boot.config").wstring();
            }
        }
    }

    // Check recursive children up to depth 3
    for (auto it = std::filesystem::recursive_directory_iterator(
             game.install_dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator();) {
        if (ec) break;
        if (it.depth() > 3) {
            it.pop(ec);
            continue;
        }
        if (it->is_directory(ec)) {
            std::wstring dirname = it->path().filename().wstring();
            if (EndsWithInsensitiveW(dirname, L"_Data")) {
                return (it->path() / L"boot.config").wstring();
            }
        }
        it.increment(ec);
    }

    return L"";
}

} // anonymous namespace

uint32_t SteamEngineOptimizer::GetRunningSteamAppId() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return 0;
    }
    DWORD appId = 0;
    DWORD type = 0;
    DWORD size = sizeof(appId);
    LONG res = RegQueryValueExW(hKey, L"RunningAppID", nullptr, &type, reinterpret_cast<LPBYTE>(&appId), &size);
    RegCloseKey(hKey);
    if (res == ERROR_SUCCESS && type == REG_DWORD) {
        return static_cast<uint32_t>(appId);
    }
    return 0;
}

std::wstring SteamEngineOptimizer::GetSteamInstallPath() {
    std::wstring path;
    if (QueryRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path) && !path.empty()) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
            return path;
        }
    }
    if (QueryRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", path) && !path.empty()) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec)) {
            return path;
        }
    }
    std::wstring fallback = L"C:\\Program Files (x86)\\Steam";
    std::error_code ec;
    if (std::filesystem::exists(fallback, ec) && std::filesystem::is_directory(fallback, ec)) {
        return fallback;
    }
    return L"";
}

std::vector<SteamGameEntry> SteamEngineOptimizer::ScanSteamLibrary() {
    std::vector<SteamGameEntry> games;
    std::wstring steam_path = GetSteamInstallPath();
    if (steam_path.empty()) {
        return games;
    }

    std::vector<std::filesystem::path> steamapps_dirs;
    std::error_code ec;
    std::filesystem::path primary_steamapps = std::filesystem::path(steam_path) / L"steamapps";
    if (std::filesystem::exists(primary_steamapps, ec)) {
        steamapps_dirs.push_back(primary_steamapps);
    }

    std::filesystem::path vdf_path = primary_steamapps / L"libraryfolders.vdf";
    if (std::filesystem::exists(vdf_path, ec)) {
        std::ifstream vdfFile(vdf_path);
        if (vdfFile.is_open()) {
            std::string line;
            while (std::getline(vdfFile, line)) {
                std::string pathVal;
                if (ExtractVdfValue(line, "path", pathVal)) {
                    std::string unescaped;
                    unescaped.reserve(pathVal.size());
                    for (size_t i = 0; i < pathVal.size(); ++i) {
                        if (pathVal[i] == '\\' && i + 1 < pathVal.size() && pathVal[i + 1] == '\\') {
                            unescaped.push_back('\\');
                            i++;
                        } else {
                            unescaped.push_back(pathVal[i]);
                        }
                    }
                    std::wstring wpath = Utf8ToWide(unescaped);
                    std::replace(wpath.begin(), wpath.end(), L'/', L'\\');
                    std::filesystem::path extra_steamapps = std::filesystem::path(wpath) / L"steamapps";
                    if (std::filesystem::exists(extra_steamapps, ec)) {
                        bool dup = false;
                        for (const auto& existing : steamapps_dirs) {
                            if (EqualsInsensitiveW(existing.wstring(), extra_steamapps.wstring())) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup) {
                            steamapps_dirs.push_back(extra_steamapps);
                        }
                    }
                }
            }
            vdfFile.close();
        }
    }

    for (const auto& sdir : steamapps_dirs) {
        for (const auto& entry : std::filesystem::directory_iterator(sdir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::wstring filename = entry.path().filename().wstring();
            if (filename.rfind(L"appmanifest_", 0) == 0 && EndsWithInsensitiveW(filename, L".acf")) {
                std::ifstream manifest(entry.path());
                if (!manifest.is_open()) continue;

                std::string mline;
                uint32_t app_id = 0;
                std::string name;
                std::string installdir;

                while (std::getline(manifest, mline)) {
                    std::string val;
                    if (app_id == 0 && ExtractVdfValue(mline, "appid", val)) {
                        try {
                            app_id = static_cast<uint32_t>(std::stoul(val));
                        } catch (...) {
                            app_id = 0;
                        }
                    }
                    if (name.empty()) {
                        ExtractVdfValue(mline, "name", name);
                    }
                    if (installdir.empty()) {
                        ExtractVdfValue(mline, "installdir", installdir);
                    }
                }
                manifest.close();

                if (app_id == 0 || app_id == 228980) continue;
                if (ContainsInsensitive(name, "Steamworks") ||
                    ContainsInsensitive(name, "Proton") ||
                    ContainsInsensitive(name, "Steam Linux Runtime")) {
                    continue;
                }
                if (installdir.empty()) continue;

                std::filesystem::path install_path = sdir / L"common" / Utf8ToWide(installdir);
                if (std::filesystem::exists(install_path, ec) && std::filesystem::is_directory(install_path, ec)) {
                    bool already_scanned = false;
                    for (const auto& g : games) {
                        if (g.app_id == app_id) {
                            already_scanned = true;
                            break;
                        }
                    }
                    if (!already_scanned) {
                        SteamGameEntry gameEntry = InspectGameDirectory(install_path.wstring(), app_id, name);
                        games.push_back(gameEntry);
                    }
                }
            }
        }
    }

    for (auto& game : games) {
        if (!game.primary_exe_path.empty()) {
            game.dxvk_installed = DxvkManager::IsDxvkInstalled(game.primary_exe_path);
        }
    }

    return games;
}

SteamGameEntry SteamEngineOptimizer::InspectGameDirectory(
    const std::wstring& install_dir,
    uint32_t app_id,
    const std::string& name) {

    SteamGameEntry entry{};
    entry.app_id = app_id;
    entry.name = name;
    entry.install_dir = install_dir;

    std::error_code ec;
    if (!std::filesystem::exists(install_dir, ec)) {
        return entry;
    }

    bool has_anticheat = false;
    bool has_unreal_clues = false;
    bool has_unity_clues = false;
    bool has_source_clues = false;

    std::wstring unreal_shipping_exe;
    std::wstring largest_exe_path;
    uintmax_t largest_exe_size = 0;
    uint32_t exe_count = 0;

    for (auto it = std::filesystem::recursive_directory_iterator(
             install_dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator();) {
        if (ec) break;
        if (it.depth() > 6) {
            it.pop(ec);
            continue;
        }

        const auto& path = it->path();
        std::wstring itemPathStr = path.wstring();
        std::wstring filename = path.filename().wstring();

        // 1. Anti-Cheat Check
        if (!has_anticheat) {
            if (ContainsInsensitiveW(itemPathStr, L"easyanticheat") ||
                ContainsInsensitiveW(itemPathStr, L"battleye") ||
                ContainsInsensitiveW(itemPathStr, L"vanguard") ||
                ContainsInsensitiveW(itemPathStr, L"vgk.sys") ||
                ContainsInsensitiveW(itemPathStr, L"randgrid.sys") ||
                ContainsInsensitiveW(itemPathStr, L"anticheatexpert") ||
                ContainsInsensitiveW(itemPathStr, L"hoyokprotect") ||
                ContainsInsensitiveW(itemPathStr, L"mhyprot") ||
                ContainsInsensitiveW(itemPathStr, L"mhypbase")) {
                has_anticheat = true;
            }
        }

        // 2. Engine Detection Clues
        if (EndsWithInsensitiveW(filename, L"-win64-shipping.exe") ||
            EndsWithInsensitiveW(filename, L"-wingdk-shipping.exe") ||
            ContainsInsensitiveW(itemPathStr, L"engine\\binaries") ||
            ContainsInsensitiveW(itemPathStr, L"engine/binaries") ||
            (EndsWithInsensitiveW(filename, L".pak") &&
             (ContainsInsensitiveW(itemPathStr, L"content\\paks") || ContainsInsensitiveW(itemPathStr, L"content/paks")))) {
            has_unreal_clues = true;
        }

        if (EqualsInsensitiveW(filename, L"unityplayer.dll") ||
            ContainsInsensitiveW(itemPathStr, L"managed\\unityengine.dll") ||
            ContainsInsensitiveW(itemPathStr, L"managed/unityengine.dll") ||
            (EqualsInsensitiveW(filename, L"boot.config") &&
             EndsWithInsensitiveW(path.parent_path().filename().wstring(), L"_data"))) {
            has_unity_clues = true;
        }

        if (EqualsInsensitiveW(filename, L"engine2.dll") ||
            ContainsInsensitiveW(itemPathStr, L"source2") ||
            EqualsInsensitiveW(filename, L"cs2.exe") ||
            EqualsInsensitiveW(filename, L"dota2.exe") ||
            EqualsInsensitiveW(filename, L"hl2.exe") ||
            EqualsInsensitiveW(filename, L"portal2.exe") ||
            EqualsInsensitiveW(filename, L"left4dead2.exe")) {
            has_source_clues = true;
        }

        // 3. Executable Inspection
        if (it->is_regular_file(ec) && EndsWithInsensitiveW(filename, L".exe")) {
            bool is_auxiliary =
                ContainsInsensitiveW(filename, L"crash") ||
                ContainsInsensitiveW(filename, L"unins") ||
                ContainsInsensitiveW(filename, L"redist") ||
                ContainsInsensitiveW(filename, L"dxsetup") ||
                ContainsInsensitiveW(filename, L"vcredist") ||
                ContainsInsensitiveW(filename, L"cefsubprocess") ||
                ContainsInsensitiveW(filename, L"easyanticheat") ||
                ContainsInsensitiveW(filename, L"beservice") ||
                ContainsInsensitiveW(filename, L"launcher_helper") ||
                ContainsInsensitiveW(filename, L"unitycrashhandler");

            if (!is_auxiliary) {
                exe_count++;
                if (EndsWithInsensitiveW(filename, L"-win64-shipping.exe") ||
                    EndsWithInsensitiveW(filename, L"-wingdk-shipping.exe")) {
                    unreal_shipping_exe = itemPathStr;
                }

                uintmax_t size = std::filesystem::file_size(path, ec);
                if (!ec && size > largest_exe_size) {
                    largest_exe_size = size;
                    largest_exe_path = itemPathStr;
                }
            }
        }

        it.increment(ec);
    }

    entry.has_anticheat = has_anticheat;

    if (has_unreal_clues) {
        entry.engine = GameEngineType::UnrealEngine4_5;
    } else if (has_unity_clues) {
        entry.engine = GameEngineType::Unity;
    } else if (has_source_clues) {
        entry.engine = GameEngineType::Source1_2;
    } else if (exe_count > 0) {
        entry.engine = GameEngineType::NativeDirectX_Vulkan;
    } else {
        entry.engine = GameEngineType::Unknown;
    }

    if (entry.engine == GameEngineType::UnrealEngine4_5 && !unreal_shipping_exe.empty()) {
        entry.primary_exe_path = unreal_shipping_exe;
    } else {
        entry.primary_exe_path = largest_exe_path;
    }

    entry.flip_model_applied = !entry.primary_exe_path.empty() &&
        GraphicsOptimizer::HasDxgiFlipModelUpgrade(entry.primary_exe_path);

    entry.dxvk_installed = !entry.primary_exe_path.empty() &&
        DxvkManager::IsDxvkInstalled(entry.primary_exe_path);

    if (entry.has_anticheat ||
        entry.engine == GameEngineType::Source1_2 ||
        entry.engine == GameEngineType::NativeDirectX_Vulkan) {
        entry.engine_preset_applied = entry.flip_model_applied;
    } else if (entry.engine == GameEngineType::UnrealEngine4_5) {
        auto iniPaths = ResolveAllUnrealEngineIniPaths(entry, L"");
        for (const auto& iniPath : iniPaths) {
            std::filesystem::path pIni(iniPath);
            std::filesystem::path pBak(iniPath + L".vegaflow.bak");
            if (std::filesystem::exists(pBak, ec)) {
                entry.engine_preset_applied = true;
                break;
            } else if (std::filesystem::exists(pIni, ec)) {
                std::string content = ReadFileToString(pIni);
                if (content.find("; [VegaFlowUMA_Start]") != std::string::npos) {
                    entry.engine_preset_applied = true;
                    break;
                }
            }
        }
    } else if (entry.engine == GameEngineType::Unity) {
        std::wstring bootPath = ResolveUnityBootConfigPath(entry, L"");
        if (!bootPath.empty()) {
            std::filesystem::path pBoot(bootPath);
            std::filesystem::path pBak(bootPath + L".vegaflow.bak");
            if (std::filesystem::exists(pBak, ec)) {
                entry.engine_preset_applied = true;
            } else if (std::filesystem::exists(pBoot, ec)) {
                std::string content = ReadFileToString(pBoot);
                entry.engine_preset_applied = (content.find("gfx-enable-native-gfx-jobs=1") != std::string::npos);
            }
        }
    }

    return entry;
}

std::string SteamEngineOptimizer::GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override) {
    uint32_t effective_pool_mb = 0;
    if (policy.tier == ApuTier::Tier1_EntryUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb_override, policy.safe_vram_pool_mb);
    } else if (policy.tier == ApuTier::Tier2_MidVegaUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override >= 3000) ? 4096 : vram_pool_mb_override;
    } else {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb_override;
    }

    std::string umaBlock;
    umaBlock += "\n\n; [VegaFlowUMA_Start]\n";
    umaBlock += "[SystemSettings]\n";
    umaBlock += "r.Streaming.PoolSize=" + std::to_string(effective_pool_mb) + "\n";
    umaBlock += "r.Streaming.LimitPoolSizeToVRAM=0\n";
    umaBlock += "r.Streaming.FullyLoadUsedTextures=1\n";
    umaBlock += "r.Streaming.FramesForFullUpdate=1\n";
    umaBlock += "r.Streaming.MaxNumTexturesToStreamPerFrame=120\n";
    umaBlock += "r.Streaming.AmortizeCPUToGPUCopy=0\n";
    umaBlock += "r.Streaming.Boost=1.5\n";
    umaBlock += "r.ShaderPipelineCache.Enabled=1\n";
    umaBlock += "r.ShaderPipelineCache.AsyncCompileRate=16\n";
    umaBlock += "r.OneFrameThreadLag=1\n";
    umaBlock += "r.GTSyncType=1\n";
    umaBlock += "r.SceneColorFringeQuality=0\n";
    umaBlock += "r.Tonemapper.GrainQuantization=0\n";
    umaBlock += "r.MotionBlurQuality=0\n";
    umaBlock += "r.DepthOfFieldQuality=0\n";
    umaBlock += "r.BloomQuality=1\n";
    umaBlock += "r.Shadow.FilterMethod=0\n";
    umaBlock += "r.Shadow.MaxResolution=" + std::to_string(policy.ue_max_csm_resolution) + "\n";
    umaBlock += "r.Shadow.MaxCSMResolution=" + std::to_string(policy.ue_max_csm_resolution) + "\n";
    umaBlock += "r.Shadow.CSM.MaxCascades=" + std::to_string(policy.ue_max_shadow_cascades) + "\n";
    umaBlock += "r.Shadow.RadiusThreshold=0.05\n";
    umaBlock += "r.Shadow.DistanceScale=0.6\n";
    umaBlock += "r.TranslucencyLightingVolumeDim=24\n";
    umaBlock += "r.VolumetricFog=" + std::string(policy.disable_volumetric_fog ? "0" : "1") + "\n";
    umaBlock += "r.SSR.Quality=0\n";
    umaBlock += "r.AmbientOcclusionLevels=0\n";
    umaBlock += "r.AOQuality=0\n";
    umaBlock += "r.DefaultFeature.AntiAliasing=1\n";
    umaBlock += "r.MaxAnisotropy=" + std::to_string(policy.max_anisotropy) + "\n";
    umaBlock += "r.RDG.AsyncCompute=" + std::string(policy.disable_dx12_async_compute ? "0" : "1") + "\n";
    umaBlock += "D3D12.AsyncCompute=" + std::string(policy.disable_dx12_async_compute ? "0" : "1") + "\n";
    umaBlock += "; [VegaFlowUMA_End]\n";
    return umaBlock;
}

EngineOptimizationReport SteamEngineOptimizer::ApplyEngineUmaPreset(
    const SteamGameEntry& game,
    uint32_t vram_pool_mb,
    bool dry_run,
    const std::wstring& custom_config_dir_override) {

    EngineOptimizationReport report{};
    std::error_code ec;

    if (!game.primary_exe_path.empty()) {
        GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.primary_exe_path, dry_run);
    }

    if (game.has_anticheat) {
        report.success = true;
        report.blocked_by_anticheat = true;
        report.status_message = "Anti-Cheat detectado: Solo se aplico DXGI Flip Model seguro (archivos del motor intactos)";
        return report;
    }

    if (game.engine == GameEngineType::UnrealEngine4_5) {
        auto ini_paths = ResolveAllUnrealEngineIniPaths(game, custom_config_dir_override);
        if (ini_paths.empty()) {
            ini_paths.push_back(ResolveUnrealEngineIniPath(game, custom_config_dir_override));
        }
        report.config_modified_path = ini_paths.front();

        if (dry_run) {
            report.success = true;
            report.status_message = "Dry-run: Preset UMA Unreal Engine 4/5 validado";
            return report;
        }

        const auto policy = HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware());
        uint32_t effective_pool_mb = 0;
        if (policy.tier == ApuTier::Tier1_EntryUMA) {
            effective_pool_mb = (vram_pool_mb == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb, policy.safe_vram_pool_mb);
        } else if (policy.tier == ApuTier::Tier2_MidVegaUMA) {
            effective_pool_mb = (vram_pool_mb == 0 || vram_pool_mb >= 3000) ? 4096 : vram_pool_mb;
        } else {
            effective_pool_mb = (vram_pool_mb == 0 || vram_pool_mb == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb;
        }
        std::string umaBlock = GenerateUnrealUmaBlock(policy, vram_pool_mb);

        bool any_applied = false;
        for (const auto& engine_ini_path : ini_paths) {
            std::filesystem::path pIni(engine_ini_path);
            std::filesystem::path pBak(engine_ini_path + L".vegaflow.bak");

            std::filesystem::create_directories(pIni.parent_path(), ec);

            if (!std::filesystem::exists(pBak, ec)) {
                if (std::filesystem::exists(pIni, ec)) {
                    report.backup_created = SandboxGuard::SafeCopyFile(
                        pIni.wstring(),
                        pBak.wstring(),
                        true,
                        game.has_anticheat);
                } else {
                    WriteStringToFile(pBak, "; VegaFlow Empty Backup\n", game.has_anticheat);
                    report.backup_created = true;
                }
            }

            std::string existingContent;
            if (std::filesystem::exists(pIni, ec)) {
                existingContent = ReadFileToString(pIni);
            }

            const std::string tagStart = "; [VegaFlowUMA_Start]";
            const std::string tagEnd = "; [VegaFlowUMA_End]";
            size_t posStart = existingContent.find(tagStart);
            while (posStart != std::string::npos) {
                size_t posEnd = existingContent.find(tagEnd, posStart);
                if (posEnd != std::string::npos) {
                    size_t removeLen = (posEnd + tagEnd.size()) - posStart;
                    if (posStart + removeLen < existingContent.size() && existingContent[posStart + removeLen] == '\n') {
                        removeLen++;
                    }
                    existingContent.erase(posStart, removeLen);
                } else {
                    existingContent.erase(posStart);
                    break;
                }
                posStart = existingContent.find(tagStart);
            }

            // Remove any untagged lingering upscaling lines
            std::stringstream ss(existingContent);
            std::string line;
            std::string cleanedContent;
            while (std::getline(ss, line)) {
                std::string trimmed = line;
                while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ')) trimmed.pop_back();
                if (trimmed.rfind("r.ScreenPercentage=", 0) == 0 ||
                    trimmed.rfind("r.TemporalAA.Upsampling=", 0) == 0 ||
                    trimmed.rfind("r.DefaultFeature.AntiAliasing=", 0) == 0) {
                    continue;
                }
                cleanedContent += line + "\n";
            }
            while (!cleanedContent.empty() &&
                   (cleanedContent.back() == '\r' || cleanedContent.back() == '\n' || cleanedContent.back() == ' ')) {
                cleanedContent.pop_back();
            }
            existingContent = std::move(cleanedContent);

            std::string finalContent = existingContent + umaBlock;
            if (WriteStringToFile(pIni, finalContent, game.has_anticheat)) {
                any_applied = true;
            }
        }

        if (!any_applied) {
            report.success = false;
            report.status_message = "No se pudo abrir ningun Engine.ini para escritura";
            return report;
        }

        report.success = true;
        report.status_message = "Preset UMA Unreal Engine aplicado (PoolVRAM=" +
            std::to_string(effective_pool_mb) + "MB, AsyncShaders=ON, FlipModel=ON, 60FPS-VegaTuned)";
        return report;
    }

    if (game.engine == GameEngineType::Unity) {
        std::wstring boot_config_path = ResolveUnityBootConfigPath(game, custom_config_dir_override);
        if (boot_config_path.empty()) {
            report.success = false;
            report.status_message = "No se encontro carpeta *_Data ni boot.config para Unity";
            return report;
        }
        std::filesystem::path pBoot(boot_config_path);
        std::filesystem::path pBak(boot_config_path + L".vegaflow.bak");
        report.config_modified_path = boot_config_path;

        if (dry_run) {
            report.success = true;
            report.status_message = "Dry-run: Preset UMA Unity Engine validado";
            return report;
        }

        std::filesystem::create_directories(pBoot.parent_path(), ec);

        if (!std::filesystem::exists(pBak, ec)) {
            if (std::filesystem::exists(pBoot, ec)) {
                report.backup_created = SandboxGuard::SafeCopyFile(
                    pBoot.wstring(),
                    pBak.wstring(),
                    true,
                    game.has_anticheat);
            } else {
                WriteStringToFile(pBak, "; VegaFlow Empty Backup\n", game.has_anticheat);
                report.backup_created = true;
            }
        }

        std::vector<std::pair<std::string, std::string>> keysToSet = {
            {"gfx-enable-gfx-jobs", "1"},
            {"gfx-enable-native-gfx-jobs", "1"},
            {"gfx-enable-mt-rendering", "1"},
            {"wait-for-native-debugger", "0"},
            {"hdr-display-enabled", "0"},
            {"gc-max-time-slice", "3"}
        };

        std::vector<std::string> lines;
        if (std::filesystem::exists(pBoot, ec)) {
            std::ifstream in(pBoot);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
            }
            in.close();
        }

        for (const auto& [k, v] : keysToSet) {
            bool found = false;
            for (auto& l : lines) {
                if (l.rfind(k + "=", 0) == 0) {
                    l = k + "=" + v;
                    found = true;
                    break;
                }
            }
            if (!found) {
                lines.push_back(k + "=" + v);
            }
        }

        std::stringstream ss;
        for (const auto& l : lines) {
            ss << l << "\n";
        }

        if (!WriteStringToFile(pBoot, ss.str(), game.has_anticheat)) {
            report.success = false;
            report.status_message = "No se pudo abrir boot.config para escritura";
            return report;
        }

        report.success = true;
        report.status_message = "Preset UMA Unity aplicado (NativeGfxJobs=1, GfxJobs=1, GC-Slice=3ms, FlipModel=ON)";
        return report;
    }

    // Para Source1_2, NativeDirectX_Vulkan o Unknown
    report.success = true;
    report.status_message = "Optimización DXGI Flip Model + Alta Prioridad GPU aplicada al ejecutable del juego";
    return report;
}

EngineOptimizationReport SteamEngineOptimizer::RestoreEngineOriginalConfig(
    const SteamGameEntry& game,
    bool dry_run,
    const std::wstring& custom_config_dir_override) {

    EngineOptimizationReport report{};
    std::error_code ec;

    if (game.engine == GameEngineType::UnrealEngine4_5) {
        auto ini_paths = ResolveAllUnrealEngineIniPaths(game, custom_config_dir_override);
        if (ini_paths.empty()) {
            ini_paths.push_back(ResolveUnrealEngineIniPath(game, custom_config_dir_override));
        }
        report.config_modified_path = ini_paths.front();

        bool any_restored = false;
        for (const auto& engine_ini_path : ini_paths) {
            std::filesystem::path pIni(engine_ini_path);
            std::filesystem::path pBak(engine_ini_path + L".vegaflow.bak");

            if (std::filesystem::exists(pBak, ec)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }

                std::string bakContent = ReadFileToString(pBak);
                if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
                    SandboxGuard::SafeRemoveFile(pIni.wstring(), game.has_anticheat);
                } else {
                    SandboxGuard::SafeCopyFile(
                        pBak.wstring(),
                        pIni.wstring(),
                        true,
                        game.has_anticheat);
                }
                SandboxGuard::SafeRemoveFile(pBak.wstring(), game.has_anticheat);
                any_restored = true;
                continue;
            }

            if (std::filesystem::exists(pIni, ec)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }

                std::string content = ReadFileToString(pIni);

                const std::string tagStart = "; [VegaFlowUMA_Start]";
                const std::string tagEnd = "; [VegaFlowUMA_End]";
                size_t posStart = content.find(tagStart);
                if (posStart != std::string::npos) {
                    while (posStart != std::string::npos) {
                        size_t posEnd = content.find(tagEnd, posStart);
                        if (posEnd != std::string::npos) {
                            size_t removeLen = (posEnd + tagEnd.size()) - posStart;
                            if (posStart + removeLen < content.size() && content[posStart + removeLen] == '\n') {
                                removeLen++;
                            }
                            content.erase(posStart, removeLen);
                        } else {
                            content.erase(posStart);
                            break;
                        }
                        posStart = content.find(tagStart);
                    }
                    while (!content.empty() &&
                           (content.back() == '\r' || content.back() == '\n' || content.back() == ' ')) {
                        content.pop_back();
                    }
                    if (!content.empty()) content += "\n";
                    WriteStringToFile(pIni, content, game.has_anticheat);
                    any_restored = true;
                }
            }
        }

        report.success = true;
        if (dry_run) {
            report.status_message = "Dry-run: Restauracion de Engine.ini desde backup validada";
        } else if (any_restored) {
            report.status_message = "Configuracion original de Unreal Engine restaurada desde .vegaflow.bak";
        } else {
            report.status_message = "No habia configuracion modificada de Unreal Engine que restaurar";
        }
        return report;
    }

    if (game.engine == GameEngineType::Unity) {
        std::wstring boot_config_path = ResolveUnityBootConfigPath(game, custom_config_dir_override);
        if (boot_config_path.empty()) {
            report.success = true;
            report.status_message = "No se encontro ruta de configuracion para Unity";
            return report;
        }
        std::filesystem::path pBoot(boot_config_path);
        std::filesystem::path pBak(boot_config_path + L".vegaflow.bak");
        report.config_modified_path = boot_config_path;

        if (std::filesystem::exists(pBak, ec)) {
            if (dry_run) {
                report.success = true;
                report.status_message = "Dry-run: Restauracion de boot.config desde backup validada";
                return report;
            }

            std::string bakContent = ReadFileToString(pBak);
            if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
                SandboxGuard::SafeRemoveFile(pBoot.wstring(), game.has_anticheat);
            } else {
                SandboxGuard::SafeCopyFile(
                    pBak.wstring(),
                    pBoot.wstring(),
                    true,
                    game.has_anticheat);
            }
            SandboxGuard::SafeRemoveFile(pBak.wstring(), game.has_anticheat);

            report.success = true;
            report.status_message = "Configuracion original de Unity restaurada desde .vegaflow.bak";
            return report;
        }

        report.success = true;
        report.status_message = "No habia copia de seguridad de Unity para restaurar";
        return report;
    }

    report.success = true;
    report.status_message = "No hay configuracion de archivos de motor que restaurar para este tipo de juego";
    return report;
}

const char* SteamEngineOptimizer::EngineTypeToString(GameEngineType type) {
    switch (type) {
        case GameEngineType::UnrealEngine4_5:
            return "Unreal Engine 4/5";
        case GameEngineType::Unity:
            return "Unity";
        case GameEngineType::Source1_2:
            return "Source 1/2";
        case GameEngineType::NativeDirectX_Vulkan:
            return "Native DirectX/Vulkan";
        default:
            return "Unknown";
    }
}

bool SteamEngineOptimizer::ApplyApuShadowConfig(
    const std::filesystem::path& iniPath,
    const ApuShadowConfig& config,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    // 1. Validar la ruta mediante SandboxGuard (seguridad anti-cheat a nivel kernel)
    if (!SandboxGuard::CanModifyGamePath(targetDir)) {
        return false;
    }
    if (SandboxGuard::PathContainsAntiCheatSignature(iniPath.wstring())) {
        return false;
    }

    auto val = SandboxGuard::ValidatePath(iniPath.wstring(), SandboxAction::Write, false);
    if (!val.allowed) {
        return false;
    }

    // 2. Validación de parámetros de configuración
    if (config.max_cascades > 16 || config.distance_scale < 0.0f ||
        config.radius_threshold < 0.0f || config.volumetric_fog > 1) {
        return false;
    }

    // Si dryRun == true, valida permisos y sintaxis sin tocar disco
    if (dryRun) {
        return true;
    }

    std::error_code ec;

    // 3. Crear respaldo .bak si no existe antes de modificar
    std::filesystem::path bakPath = iniPath.wstring() + L".bak";
    if (!std::filesystem::exists(bakPath, ec)) {
        if (std::filesystem::exists(iniPath, ec)) {
            if (!SandboxGuard::SafeCopyFile(iniPath.wstring(), bakPath.wstring(), true, false)) {
                return false;
            }
        } else {
            if (!SandboxGuard::SafeWriteFile(bakPath.wstring(), "; VegaFlow Empty Backup\n", false)) {
                return false;
            }
        }
    }

    // 4. Leer contenido existente
    std::string existingContent;
    if (std::filesystem::exists(iniPath, ec)) {
        existingContent = ReadFileToString(iniPath);
    }

    struct ShadowEntry {
        std::string key;
        std::string val;
        bool applied = false;
    };

    std::vector<ShadowEntry> shadowKeys = {
        {"r.Shadow.CSM.MaxCascades", std::to_string(config.max_cascades), false},
        {"r.Shadow.DistanceScale", FormatFloatTrimmed(config.distance_scale), false},
        {"r.Shadow.RadiusThreshold", FormatFloatTrimmed(config.radius_threshold), false},
        {"r.AOHalfRes", config.half_res_ao ? "1" : "0", false},
        {"r.VolumetricFog", std::to_string(config.volumetric_fog), false}
    };

    std::stringstream in(existingContent);
    std::string rawLine;
    std::vector<std::string> lines;
    while (std::getline(in, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }
        lines.push_back(rawLine);
    }

    std::vector<std::string> outputLines;
    bool inSystemSettings = false;
    bool foundSystemSettings = false;

    for (const auto& line : lines) {
        std::string_view trimmed = TrimWhitespace(line);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
            if (inSystemSettings) {
                // Al salir de [SystemSettings], inyectar las claves que falten
                for (auto& entry : shadowKeys) {
                    if (!entry.applied) {
                        outputLines.push_back(entry.key + "=" + entry.val);
                        entry.applied = true;
                    }
                }
                inSystemSettings = false;
            }

            if (EqualsInsensitive(secName, "SystemSettings")) {
                inSystemSettings = true;
                foundSystemSettings = true;
            }
            outputLines.push_back(line);
            continue;
        }

        if (inSystemSettings) {
            size_t eqPos = trimmed.find('=');
            if (eqPos != std::string_view::npos) {
                std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                bool matched = false;
                for (auto& entry : shadowKeys) {
                    if (EqualsInsensitive(keyPart, entry.key)) {
                        outputLines.push_back(entry.key + "=" + entry.val);
                        entry.applied = true;
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    continue;
                }
            }
        }

        outputLines.push_back(line);
    }

    if (inSystemSettings) {
        for (auto& entry : shadowKeys) {
            if (!entry.applied) {
                outputLines.push_back(entry.key + "=" + entry.val);
                entry.applied = true;
            }
        }
    }

    if (!foundSystemSettings) {
        if (!outputLines.empty() && !outputLines.back().empty()) {
            outputLines.push_back("");
        }
        outputLines.push_back("[SystemSettings]");
        for (auto& entry : shadowKeys) {
            outputLines.push_back(entry.key + "=" + entry.val);
            entry.applied = true;
        }
    }

    std::ostringstream out;
    for (const auto& l : outputLines) {
        out << l << "\n";
    }

    return SandboxGuard::SafeWriteFile(iniPath.wstring(), out.str(), false);
}

bool SteamEngineOptimizer::RestoreApuShadowConfig(
    const std::filesystem::path& iniPath,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    // 1. Validar la ruta mediante SandboxGuard
    if (!SandboxGuard::CanModifyGamePath(targetDir)) {
        return false;
    }
    if (SandboxGuard::PathContainsAntiCheatSignature(iniPath.wstring())) {
        return false;
    }

    auto val = SandboxGuard::ValidatePath(iniPath.wstring(), SandboxAction::Write, false);
    if (!val.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path bakPath = iniPath.wstring() + L".bak";
    bool bakExists = std::filesystem::exists(bakPath, ec);
    bool iniExists = std::filesystem::exists(iniPath, ec);

    if (!bakExists && !iniExists) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    // 2. Restaurar desde .bak si existe
    if (bakExists) {
        std::string bakContent = ReadFileToString(bakPath);
        bool restoredOk = false;
        if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
            restoredOk = SandboxGuard::SafeRemoveFile(iniPath.wstring(), false);
        } else {
            restoredOk = SandboxGuard::SafeCopyFile(bakPath.wstring(), iniPath.wstring(), true, false);
        }
        if (restoredOk) {
            SandboxGuard::SafeRemoveFile(bakPath.wstring(), false);
            return true;
        }
        return false;
    }

    // 3. Si no hay archivo .bak pero iniPath existe, eliminar las claves inyectadas de [SystemSettings]
    if (iniExists) {
        std::string content = ReadFileToString(iniPath);
        std::stringstream in(content);
        std::string rawLine;
        std::vector<std::string> lines;
        while (std::getline(in, rawLine)) {
            if (!rawLine.empty() && rawLine.back() == '\r') {
                rawLine.pop_back();
            }
            lines.push_back(rawLine);
        }

        const std::vector<std::string> targetKeys = {
            "r.Shadow.CSM.MaxCascades",
            "r.Shadow.DistanceScale",
            "r.Shadow.RadiusThreshold",
            "r.AOHalfRes",
            "r.VolumetricFog"
        };

        std::vector<std::string> outputLines;
        bool inSystemSettings = false;

        for (const auto& line : lines) {
            std::string_view trimmed = TrimWhitespace(line);
            if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
                std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
                inSystemSettings = EqualsInsensitive(secName, "SystemSettings");
                outputLines.push_back(line);
                continue;
            }

            if (inSystemSettings) {
                size_t eqPos = trimmed.find('=');
                if (eqPos != std::string_view::npos) {
                    std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                    bool matched = false;
                    for (const auto& k : targetKeys) {
                        if (EqualsInsensitive(keyPart, k)) {
                            matched = true;
                            break;
                        }
                    }
                    if (matched) {
                        continue;
                    }
                }
            }

            outputLines.push_back(line);
        }

        std::ostringstream out;
        for (const auto& l : outputLines) {
            out << l << "\n";
        }

        return SandboxGuard::SafeWriteFile(iniPath.wstring(), out.str(), false);
    }

    return false;
}

bool SteamEngineOptimizer::QueryApuShadowConfig(
    const std::filesystem::path& iniPath,
    ApuShadowConfig& currentConfig) {

    if (iniPath.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(iniPath, ec) || !std::filesystem::is_regular_file(iniPath, ec)) {
        return false;
    }

    std::string content;
    if (!SandboxGuard::SafeReadFile(iniPath.wstring(), content)) {
        return false;
    }

    std::stringstream in(content);
    std::string rawLine;
    bool inSystemSettings = false;
    uint32_t keysFound = 0;

    while (std::getline(in, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }
        std::string_view trimmed = TrimWhitespace(rawLine);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
            inSystemSettings = EqualsInsensitive(secName, "SystemSettings");
            continue;
        }

        if (inSystemSettings) {
            size_t eqPos = trimmed.find('=');
            if (eqPos != std::string_view::npos) {
                std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                std::string_view valPart = TrimWhitespace(trimmed.substr(eqPos + 1));
                std::string valStr(valPart);

                if (EqualsInsensitive(keyPart, "r.Shadow.CSM.MaxCascades")) {
                    try {
                        currentConfig.max_cascades = static_cast<uint32_t>(std::stoul(valStr));
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.Shadow.DistanceScale")) {
                    try {
                        currentConfig.distance_scale = std::stof(valStr);
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.Shadow.RadiusThreshold")) {
                    try {
                        currentConfig.radius_threshold = std::stof(valStr);
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.AOHalfRes")) {
                    currentConfig.half_res_ao = (valStr == "1" || EqualsInsensitive(valStr, "true"));
                    keysFound++;
                } else if (EqualsInsensitive(keyPart, "r.VolumetricFog")) {
                    try {
                        currentConfig.volumetric_fog = static_cast<uint32_t>(std::stoul(valStr));
                        keysFound++;
                    } catch (...) {}
                }
            }
        }
    }

    return (keysFound > 0);
}

} // namespace vegaflow
