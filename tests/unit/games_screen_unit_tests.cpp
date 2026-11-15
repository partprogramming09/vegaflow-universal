// Unit tests: the model of the "Juegos" screen (phase G): one row per Steam game with what VegaFlow can do to it, the
// exact list of what a profile changes (taken from the code that writes it), and the "Me fue mal" exclusion.

#include "session/games_screen.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::GameEngineType;
using vegaflow::SteamGameEntry;
using vegaflow::app::AppConfig;
using vegaflow::app::ConfigStore;

SteamGameEntry Game(uint32_t id, const char* name, GameEngineType engine, const wchar_t* exe, bool anticheat = false, bool applied = false) {
    SteamGameEntry g;
    g.app_id = id;
    g.name = name;
    g.engine = engine;
    g.primary_exe_path = std::wstring(L"C:\\Steam\\steamapps\\common\\Game") + std::to_wstring(id) + L"\\" + exe;
    g.has_anticheat = anticheat;
    g.engine_preset_applied = applied;
    return g;
}

const gs::GameRow* Find(const gs::GamesScreenModel& m, uint32_t id) {
    for (const auto& r : m.rows) {
        if (r.app_id == id) return &r;
    }
    return nullptr;
}

} // namespace

void Test69() {
    vegaflow::ApuOptimizationPolicy policy{};
    policy.apply_visual_cuts = true;
    const std::vector<SteamGameEntry> games = {
        Game(4, "Stardew Valley", GameEngineType::NativeDirectX_Vulkan, L"Stardew.exe"),
        Game(3, "Elden Ring", GameEngineType::Unknown, L"eldenring.exe", true),
        Game(2, "Hollow Knight", GameEngineType::Unity, L"hollow_knight.exe", false, true),
        Game(1, "Octopath Traveler II", GameEngineType::UnrealEngine4_5, L"Octopath.exe"),
    };
    const auto m = gs::BuildGamesScreen(games, policy, 4096, {});

    // 1. One row per game, sorted by name, with the status, the buttons that apply and an honest sentence each.
    const auto* ue = Find(m, 1);
    const auto* unity = Find(m, 2);
    const auto* anti = Find(m, 3);
    const auto* native = Find(m, 4);
    bool sorted = m.rows.size() == 4;
    for (size_t i = 1; sorted && i < m.rows.size(); ++i) sorted = m.rows[i - 1].name <= m.rows[i].name;
    RecordResult(sorted && ue && unity && anti && native && ue->status == gs::GameStatus::NotApplied && ue->can_apply && !ue->can_restore &&
                     unity->status == gs::GameStatus::Applied && !unity->can_apply && unity->can_restore && anti->status == gs::GameStatus::Protected &&
                     !anti->can_apply && !anti->can_restore && native->status == gs::GameStatus::NoProfile && !native->can_apply && !native->can_restore &&
                     ue->engine == "Unreal Engine 4/5" && unity->engine == "Unity" && !ue->detail.empty() && !anti->detail.empty() && !native->detail.empty(),
                 "Test 69.1: rows are sorted by name; Unreal and Unity can take a profile, an anti-cheat game is protected, an unknown engine has no profile");

    // 2. What an Unreal profile changes is exactly what the writer writes: the registry value plus every key of the block.
    const std::string block = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(policy, 4096);
    std::set<std::string> expected;
    size_t pos = 0;
    while (pos < block.size()) {
        size_t eol = block.find('\n', pos);
        if (eol == std::string::npos) eol = block.size();
        const std::string line = block.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line[0] != ';' && line[0] != '[' && line.find('=') != std::string::npos) expected.insert(line);
    }
    std::set<std::string> shown(ue->changes.begin(), ue->changes.end());
    const bool has_registry = std::any_of(ue->changes.begin(), ue->changes.end(), [](const std::string& c) { return c.find("SwapEffectUpgradeEnable=1") != std::string::npos; });
    bool all_keys = !expected.empty();
    for (const auto& k : expected) all_keys = all_keys && shown.count(k) == 1;
    RecordResult(has_registry && all_keys && ue->changes.size() == expected.size() + 1,
                 "Test 69.2: an Unreal profile lists the registry value and exactly the keys GenerateUnrealUmaBlock writes");

    // 3. Same for Unity: the keys come from the one list the writer uses.
    std::set<std::string> unity_expected;
    for (const auto& [k, v] : vegaflow::SteamEngineOptimizer::UnityBootConfigKeys()) unity_expected.insert(k + "=" + v);
    std::set<std::string> unity_shown(unity->changes.begin(), unity->changes.end());
    bool unity_all = !unity_expected.empty();
    for (const auto& k : unity_expected) unity_all = unity_all && unity_shown.count(k) == 1;
    RecordResult(unity_all && unity->changes.size() == unity_expected.size() + 1, "Test 69.3: a Unity profile lists the registry value and exactly the keys of UnityBootConfigKeys");

    // 4. Games with nothing to change list nothing; no library gives an empty text.
    const auto empty = gs::BuildGamesScreen({}, policy, 4096, {});
    // The daemon writes the registry value for every game that opens (game_session.cpp), so that one line is listed; no file lines.
    const auto only_registry = [](const gs::GameRow* r) {
        return r->changes.size() == 1 && r->changes[0].find("SwapEffectUpgradeEnable=1") != std::string::npos && r->file_hint.empty() && r->backup_hint.empty();
    };
    RecordResult(only_registry(anti) && only_registry(native) && empty.rows.empty() && !empty.empty_text.empty(),
                 "Test 69.4: protected and profile-less games list only the registry value, no file; an empty library gives its text");

    // 5. Excluded games (config.json) are marked, matched by file name in any letter case, and take no profile.
    const auto ex = gs::BuildGamesScreen(games, policy, 4096, {L"OCTOPATH.EXE"});
    const auto* ue_ex = Find(ex, 1);
    RecordResult(ue_ex && ue_ex->status == gs::GameStatus::Excluded && ue_ex->excluded && !ue_ex->can_apply && ue_ex->changes.empty() && !Find(ex, 2)->excluded,
                 "Test 69.5: a game listed in excluded_game_exes shows as excluded (any letter case) and cannot take a profile");

    // 6. "Me fue mal" and "Volver a incluir" edit the excluded list: Rust-checked names, no duplicates, round trip through config.json.
    AppConfig c;
    std::string err;
    const bool added = gs::ExcludeGame(c, "C:\\Steam\\steamapps\\common\\Octopath\\Octopath.exe", &err) && c.process_policy.excluded_game_exes.size() == 1;
    const bool dup = !gs::ExcludeGame(c, "octopath.exe", &err) && c.process_policy.excluded_game_exes.size() == 1;
    const bool bad = !gs::ExcludeGame(c, "C:\\Windows\\explorer.exe", &err) && !err.empty() && c.process_policy.excluded_game_exes.size() == 1;
    FakeFileSystem fs;
    ConfigStore store(fs, L"C:\\VegaFlow\\config.json", true);
    AppConfig loaded = store.Load();
    gs::ExcludeGame(loaded, "Octopath.exe", &err);
    std::string why;
    const bool saved = store.Save(loaded, false, &why);
    ConfigStore again(fs, L"C:\\VegaFlow\\config.json", true);
    const AppConfig back = again.Load();
    const bool round = saved && back.process_policy.excluded_game_exes.size() == 1 && gs::BuildGamesScreen(games, policy, 4096, back.process_policy.excluded_game_exes).rows.size() == 4 &&
                       Find(gs::BuildGamesScreen(games, policy, 4096, back.process_policy.excluded_game_exes), 1)->excluded;
    AppConfig un = back;
    const bool included = gs::IncludeGame(un, "C:\\x\\OCTOPATH.exe") && un.process_policy.excluded_game_exes.empty() && !gs::IncludeGame(un, "Octopath.exe");
    RecordResult(added && dup && bad && round && included, "Test 69.6: excluding a game is Rust-checked, not duplicated, survives config.json, and can be undone");

    // 7. No text on the screen promises a gain.
    std::string all;
    for (const auto& r : m.rows) all += r.detail + " " + r.file_hint + " ";
    RecordResult(all.find("mejora") == std::string::npos && all.find("FPS") == std::string::npos && all.find("más rápido") == std::string::npos,
                 "Test 69.7: the Juegos texts promise no gain (no FPS, no 'mejora', no 'más rápido')");

    // 8. DXVK: offered for any game that may be changed (not anti-cheat, not excluded) and shows whether it is installed.
    std::vector<SteamGameEntry> dx = games;
    dx[3].dxvk_installed = true; // index 3 is Octopath (app id 1)
    dx[0].dxvk_installed = true; // index 0 is Stardew (app id 4), excluded below: it can still be removed
    const auto md = gs::BuildGamesScreen(dx, policy, 4096, {L"Stardew.exe"});
    // The text names dxvk.conf, which the writer replaces (with a copy of the user's).
    RecordResult(Find(md, 1)->dxvk_hint.find("dxvk.conf") != std::string::npos && Find(md, 1)->can_dxvk && Find(md, 1)->dxvk_installed && !Find(md, 1)->dxvk_hint.empty() && !Find(md, 3)->can_dxvk && Find(md, 4)->can_dxvk && Find(md, 4)->excluded &&
                     Find(md, 2)->can_dxvk && !Find(md, 2)->dxvk_installed,
                 "Test 69.8: DXVK is offered unless the game is anti-cheat (or excluded and without it), and shows whether it is installed");
}

} // namespace vftest
