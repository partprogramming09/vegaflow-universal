// Unit test for F5.3 and F5.4: the Epic manifests in tests/vectors/epic/ and the GOG entries in tests/vectors/gog_games.tsv
// give the folders the shared vectors expect (the Rust parsers are tested on the same files in cargo test).

#include "fake_registry_power.hpp"
#include "windows/game_launchers.hpp"
#include "../test_support.hpp"

#include <filesystem>
#include <fstream>
#include <map>

namespace vftest {

namespace {

std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); } // the vectors are ASCII

std::vector<std::string> Padded(std::vector<std::string> cells, size_t n) {
    cells.resize(std::max(cells.size(), n));
    return cells;
}

bool Has(const std::vector<vegaflow::LauncherGameFolder>& games, const std::string& dir, bool anticheat) {
    for (const auto& g : games) {
        if (_wcsicmp(g.dir.c_str(), W(dir).c_str()) == 0 && g.anticheat == anticheat) return true;
    }
    return false;
}

} // namespace

void Test160() {
    std::string failures;
    std::error_code ec;

    // --- Epic: every manifest of the vector file is copied into a manifests folder, plus things that are not manifests.
    const std::filesystem::path manifests = std::filesystem::temp_directory_path(ec) / L"vegaflow_test_epic_manifests";
    std::filesystem::remove_all(manifests, ec);
    std::filesystem::create_directories(manifests, ec);
    std::map<std::string, std::vector<std::string>> epic_rows;
    ForEachTsvRow("tests/vectors/epic_manifests.tsv", [&](const std::vector<std::string>& col, const std::string&) {
        const auto c = Padded(col, 4);
        epic_rows[c[0]] = c;
        std::filesystem::copy_file("tests/vectors/epic/" + c[0], manifests / c[0], ec);
    });
    std::filesystem::create_directories(manifests / L"folder_named.item", ec); // a folder is not a manifest
    std::ofstream(manifests / L"notes.txt") << R"({"AppCategories":["games"],"InstallLocation":"C:/Games/Txt"})"; // not *.item
    const auto epic = vegaflow::ReadEpicGames(manifests.wstring());
    size_t expected_ok = 0;
    for (const auto& [file, c] : epic_rows) {
        if (c[1] != "ok") {
            // A refused manifest adds nothing: its text must not show up as a folder anywhere.
            continue;
        }
        ++expected_ok;
        // eac_only_on_disk shares its folder with eac_launcher, so the merged entry carries the flag.
        const bool anticheat = c[3] == "1" || file == "eac_only_on_disk.item";
        if (!Has(epic, c[2], anticheat)) failures += " [epic " + file + " not read as " + c[2] + "]";
    }
    // Two ok manifests share a folder (eac_launcher and eac_only_on_disk): one entry, anti-cheat if either says so.
    if (epic.size() != expected_ok - 1) failures += " [epic count " + std::to_string(epic.size()) + " vs " + std::to_string(expected_ok - 1) + "]";
    if (!Has(epic, "C:\\Program Files\\Epic Games\\Shooter", true)) failures += " [shared folder keeps the anti-cheat flag]";
    for (const auto& g : epic) {
        if (g.dir.find(L"..") != std::wstring::npos || g.dir.find(L'/') != std::wstring::npos) failures += " [epic folder not plain]";
    }
    if (!vegaflow::ReadEpicGames((manifests / L"missing").wstring()).empty()) failures += " [missing folder gives games]";
    if (!vegaflow::ReadEpicGames(L"").empty()) failures += " [empty folder gives games]";
    std::filesystem::remove_all(manifests, ec);

    // --- GOG: every row of the vector file becomes a registry entry under HKLM\...\WOW6432Node\GOG.com\Games.
    FakeRegistryPort reg;
    size_t gog_ok = 0, gog_rows = 0;
    std::vector<std::vector<std::string>> ok_rows;
    ForEachTsvRow("tests/vectors/gog_games.tsv", [&](const std::vector<std::string>& col, const std::string&) {
        const auto c = Padded(col, 7);
        const std::wstring sub = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\" + W(c[0]);
        reg.SeedValue(vegaflow::RegHive::LocalMachine, sub, L"gameID", vegaflow::RegValue::MakeString(W(c[1])));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, sub, L"path", vegaflow::RegValue::MakeString(W(c[2])));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, sub, L"exe", vegaflow::RegValue::MakeString(W(c[3])));
        ++gog_rows;
        if (c[4] == "ok") { ++gog_ok; ok_rows.push_back(c); }
    });
    // A value of the wrong type (REG_DWORD where a path is expected) is a refused entry, not a crash.
    reg.SeedValue(vegaflow::RegHive::LocalMachine, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\999", L"gameID", vegaflow::RegValue::MakeString(L"999"));
    reg.SeedValue(vegaflow::RegHive::LocalMachine, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\999", L"path", vegaflow::RegValue::MakeDword(7));
    const auto gog = vegaflow::ReadGogGames(reg);
    if (gog.size() != gog_ok) failures += " [gog count " + std::to_string(gog.size()) + " vs " + std::to_string(gog_ok) + "]";
    for (const auto& c : ok_rows) {
        if (!Has(gog, c[5], c[6] == "1")) failures += " [gog " + c[0] + " not read as " + c[5] + "]";
    }
    FakeRegistryPort empty;
    if (!vegaflow::ReadGogGames(empty).empty()) failures += " [no GOG key gives games]";
    if (!reg.writes.empty() || !reg.deletes.empty()) failures += " [the reader wrote to the registry]";

    RecordResult(failures.empty() && epic_rows.size() >= 17 && gog_rows >= 16,
                 "Test 160: Epic manifests and GOG entries give the folders the shared vectors expect",
                 std::to_string(epic_rows.size()) + " Epic and " + std::to_string(gog_rows) + " GOG vectors" +
                     (failures.empty() ? ", 0 mismatches" : "; mismatches:" + failures));
}

} // namespace vftest
