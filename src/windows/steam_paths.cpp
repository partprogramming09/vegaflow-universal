#include "windows/steam_paths.hpp"
#include "bridge/rust_bridge.h"
#include "windows/unique_handle.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace vegaflow {

namespace {

bool QueryRegString(HKEY root, const wchar_t* sub_key, const wchar_t* value_name, std::wstring& out) {
    UniqueRegKey key;
    if (RegOpenKeyExW(root, sub_key, 0, KEY_READ, key.put()) != ERROR_SUCCESS) return false;
    wchar_t buf[1024]{};
    DWORD type = 0;
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegQueryValueExW(key.get(), value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS) return false;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
    out = buf;
    return true;
}

bool IsDirectory(const std::wstring& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec);
}

std::wstring Utf8ToWidePath(const char* utf8, size_t len) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, static_cast<int>(len), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, static_cast<int>(len), out.data(), n);
    return out;
}

void AddUnique(std::vector<std::wstring>& list, std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', L'\\');
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    for (const auto& existing : list) {
        if (_wcsicmp(existing.c_str(), path.c_str()) == 0) return;
    }
    list.push_back(std::move(path));
}

} // namespace

std::wstring FindSteamInstallPath() {
    std::wstring path;
    if (QueryRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", path)) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (IsDirectory(path)) return path;
    }
    if (QueryRegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", path)) {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        if (IsDirectory(path)) return path;
    }
    const std::wstring fallback = L"C:\\Program Files (x86)\\Steam";
    return IsDirectory(fallback) ? fallback : L"";
}

std::vector<std::wstring> ReadSteamLibraryFolders(const std::wstring& steam_install) {
    std::vector<std::wstring> folders;
    if (steam_install.empty()) return folders;
    AddUnique(folders, steam_install);

    std::ifstream file(std::filesystem::path(steam_install) / L"steamapps" / L"libraryfolders.vdf", std::ios::binary);
    if (!file) return folders;
    const std::string vdf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Called from Rust: an exception must not unwind through it.
    auto on_path = [](void* ctx, const char* utf8, size_t len) noexcept {
        try {
            std::wstring wide = Utf8ToWidePath(utf8, len);
            if (!wide.empty()) AddUnique(*static_cast<std::vector<std::wstring>*>(ctx), std::move(wide));
        } catch (...) {
        }
    };
    vf_rust_steam_library_paths(vdf.data(), vdf.size(), on_path, &folders);
    return folders;
}

} // namespace vegaflow
