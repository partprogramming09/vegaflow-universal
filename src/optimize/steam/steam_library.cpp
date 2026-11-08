#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/dxvk_manager.hpp"

namespace vegaflow {

using namespace steam_internal;

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

} // namespace

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

} // namespace vegaflow
