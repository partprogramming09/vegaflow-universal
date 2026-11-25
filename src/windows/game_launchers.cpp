#include "windows/game_launchers.hpp"
#include "bridge/ffi_callback_guard.hpp"
#include "bridge/rust_bridge.h"
#include "ports/text_convert.hpp"
#include "windows/registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace vegaflow {

namespace {

constexpr size_t kMaxManifestBytes = size_t{1} << 20; // the Rust core refuses more; do not even read it
constexpr size_t kMaxGames = 512;

void AddUnique(std::vector<LauncherGameFolder>& list, std::wstring dir, bool anticheat) {
    if (list.size() >= kMaxGames) return;
    for (auto& existing : list) {
        if (_wcsicmp(existing.dir.c_str(), dir.c_str()) == 0) {
            existing.anticheat = existing.anticheat || anticheat; // one entry naming an anti-cheat is enough
            return;
        }
    }
    list.push_back({std::move(dir), anticheat});
}

// Called from Rust: an exception must not unwind through it.
void OnDir(void* ctx, const char* utf8, size_t len) noexcept {
    RunCallbackGuarded([&] {
        auto* c = static_cast<std::vector<std::wstring>*>(ctx);
        std::wstring wide = app::Utf8ToWide(std::string(utf8, len));
        if (!wide.empty()) c->push_back(std::move(wide));
    });
}

// The one folder an accepted entry reports, or empty. A thrown callback gives empty too: never a partial answer.
std::wstring FolderOf(uint32_t status, const std::vector<std::wstring>& reported) {
    if (ConsumeCallbackFailure()) return L"";
    if ((status != VF_LAUNCHER_GAME && status != VF_LAUNCHER_GAME_ANTICHEAT) || reported.size() != 1) return L"";
    return reported.front();
}

} // namespace

std::wstring DefaultEpicManifestsDir() {
    const wchar_t* program_data = _wgetenv(L"ProgramData");
    if (!program_data || !*program_data) return L"";
    return (std::filesystem::path(program_data) / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests").wstring();
}

std::vector<LauncherGameFolder> ReadEpicGames(const std::wstring& manifests_dir) {
    std::vector<LauncherGameFolder> games;
    if (manifests_dir.empty()) return games;

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    std::filesystem::directory_iterator it(manifests_dir, ec);
    // increment(ec) instead of ++ (which throws): a listing cut by an error keeps the games found so far.
    for (const std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) continue; // a folder or link named *.item is not a manifest
        std::wstring ext = it->path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (ext == L".item") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::string json(kMaxManifestBytes + 1, '\0');
        in.read(json.data(), static_cast<std::streamsize>(json.size()));
        json.resize(static_cast<size_t>(in.gcount()));
        if (json.size() > kMaxManifestBytes) continue;

        std::vector<std::wstring> reported;
        const uint32_t status = vf_rust_epic_manifest_install_dir(json.data(), json.size(), OnDir, &reported);
        std::wstring dir = FolderOf(status, reported);
        if (!dir.empty()) AddUnique(games, std::move(dir), status == VF_LAUNCHER_GAME_ANTICHEAT);
    }
    return games;
}

std::vector<LauncherGameFolder> ReadGogGames(RegistryPort& registry) {
    std::vector<LauncherGameFolder> games;
    const std::wstring base = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games";
    auto text = [&](const std::wstring& sub, const wchar_t* name) {
        RegValue v;
        if (!registry.ReadValue(RegHive::LocalMachine, sub, name, v) || v.type != RegValueType::String) return std::string();
        return app::WideToUtf8(v.str);
    };
    for (const auto& key : registry.EnumSubkeys(RegHive::LocalMachine, base)) {
        const std::wstring sub = base + L"\\" + key;
        const std::string key_u8 = app::WideToUtf8(key);
        const std::string id = text(sub, L"gameID"), path = text(sub, L"path"), exe = text(sub, L"exe");
        std::vector<std::wstring> reported;
        const uint32_t status = vf_rust_gog_install_dir(key_u8.data(), key_u8.size(), id.data(), id.size(), path.data(),
                                                        path.size(), exe.data(), exe.size(), OnDir, &reported);
        std::wstring dir = FolderOf(status, reported);
        if (!dir.empty()) AddUnique(games, std::move(dir), status == VF_LAUNCHER_GAME_ANTICHEAT);
    }
    return games;
}

} // namespace vegaflow
