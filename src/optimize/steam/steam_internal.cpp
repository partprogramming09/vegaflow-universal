#include "optimize/steam/steam_internal.hpp"

namespace vegaflow::steam_internal {

namespace {

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

} // namespace

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

} // namespace vegaflow::steam_internal
