#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/dxvk_manager.hpp"
#include "windows/steam_paths.hpp"

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
    return FindSteamInstallPath();
}

std::vector<SteamGameEntry> SteamEngineOptimizer::ScanSteamLibrary() {
    std::vector<SteamGameEntry> games;
    std::error_code ec;
    std::vector<std::filesystem::path> steamapps_dirs;
    for (const auto& library : ReadSteamLibraryFolders(FindSteamInstallPath())) {
        std::filesystem::path steamapps = std::filesystem::path(library) / L"steamapps";
        if (std::filesystem::exists(steamapps, ec)) steamapps_dirs.push_back(steamapps);
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
