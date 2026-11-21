// Adversarial unit tests: the "Juegos" model (BuildGamesScreen) and the excluded-list edits (ExcludeGame / IncludeGame)
// with hostile inputs: empty and huge libraries, every flag combination, paths as exe names, Unicode, case, duplicates.

#include "session/games_screen.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::GameEngineType;
using vegaflow::SteamGameEntry;
using vegaflow::app::AppConfig;

// Runs one attack; the lambda returns "" when the contract held, else the smallest description of what broke.
void Attack(const std::string& name, const std::function<std::string()>& body) {
    try {
        const std::string why = body();
        RecordResult(why.empty(), name, why);
    } catch (const std::exception& e) {
        RecordResult(false, name, std::string("threw: ") + e.what());
    } catch (...) {
        RecordResult(false, name, "threw an unknown exception");
    }
}

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool Promises(const std::string& text) {
    const std::string t = LowerAscii(text);
    return t.find("fps") != std::string::npos || t.find("mejora") != std::string::npos || t.find("más rápido") != std::string::npos ||
           t.find("mas rapido") != std::string::npos || t.find("rápid") != std::string::npos;
}

SteamGameEntry Game(uint32_t id, const std::string& name, GameEngineType engine, const std::wstring& exe, bool anticheat = false,
                    bool applied = false, bool dxvk = false) {
    SteamGameEntry g;
    g.app_id = id;
    g.name = name;
    g.engine = engine;
    g.install_dir = L"C:\\Steam\\steamapps\\common\\G" + std::to_wstring(id);
    g.primary_exe_path = exe.empty() ? L"" : g.install_dir + L"\\" + exe;
    g.has_anticheat = anticheat;
    g.engine_preset_applied = applied;
    g.dxvk_installed = dxvk;
    return g;
}

const vegaflow::ApuOptimizationPolicy& Policy() {
    static vegaflow::ApuOptimizationPolicy p = [] {
        vegaflow::ApuOptimizationPolicy x{};
        x.apply_visual_cuts = true;
        return x;
    }();
    return p;
}

std::string Describe(const SteamGameEntry& g, bool excluded) {
    return std::string("engine=") + std::to_string(static_cast<int>(g.engine)) + " anticheat=" + (g.has_anticheat ? "1" : "0") +
           " applied=" + (g.engine_preset_applied ? "1" : "0") + " dxvk=" + (g.dxvk_installed ? "1" : "0") +
           " exe=" + (g.primary_exe_path.empty() ? "empty" : "set") + " excluded=" + (excluded ? "1" : "0");
}

} // namespace

void Test76() {
    // Test 76: attacks on the Juegos model and the excluded list
    const GameEngineType engines[] = {GameEngineType::Unknown, GameEngineType::UnrealEngine4_5, GameEngineType::Unity, GameEngineType::Source1_2,
                                      GameEngineType::NativeDirectX_Vulkan};

    Attack("Test 76.1: an empty library gives no rows and a non-empty sentence", [&]() -> std::string {
        const auto m = gs::BuildGamesScreen({}, Policy(), 0, {});
        if (!m.rows.empty()) return "rows not empty";
        if (m.empty_text.empty()) return "empty_text is empty";
        return "";
    });

    Attack("Test 76.2: every flag combination keeps the invariants (anti-cheat never gets files, buttons agree with status, texts not empty)", [&]() -> std::string {
        for (const auto engine : engines) {
            for (int bits = 0; bits < 32; ++bits) {
                const bool anticheat = bits & 1, applied = bits & 2, dxvk = bits & 4, no_exe = bits & 8, excluded = bits & 16;
                const SteamGameEntry g = Game(7, "G", engine, no_exe ? L"" : L"Game.exe", anticheat, applied, dxvk);
                const std::vector<std::wstring> ex = excluded ? std::vector<std::wstring>{L"game.exe"} : std::vector<std::wstring>{};
                const auto m = gs::BuildGamesScreen({g}, Policy(), 4096, ex);
                if (m.rows.size() != 1) return "row count " + Describe(g, excluded);
                const auto& r = m.rows[0];
                const std::string d = Describe(g, excluded);
                const bool really_excluded = excluded && !no_exe;
                if (r.detail.empty()) return "empty detail: " + d;
                if (r.engine.empty()) return "empty engine: " + d;
                if (std::string(gs::StatusLabel(r.status)).empty()) return "empty status label: " + d;
                if (r.excluded != really_excluded) return "excluded flag wrong: " + d;
                if (r.can_apply && r.can_restore) return "can_apply and can_restore both true: " + d;
                if (r.can_apply && (r.status != gs::GameStatus::NotApplied || anticheat || r.excluded || no_exe)) return "can_apply where it must not: " + d;
                if (anticheat && (r.can_apply || r.can_dxvk || r.can_restore)) return "anti-cheat game offered a file action: " + d;
                if (anticheat && !r.excluded && r.status != gs::GameStatus::Protected) return "anti-cheat not Protected: " + d;
                if (anticheat && !r.file_hint.empty()) return "anti-cheat game names a file a profile changes: " + d;
                if (anticheat) {
                    for (const auto& c : r.changes) {
                        if (c.rfind("Registro,", 0) != 0) return "anti-cheat game lists a non-registry change '" + c + "': " + d;
                    }
                }
                if (r.can_dxvk && no_exe) return "can_dxvk without a game program: " + d;
                if (r.excluded && r.can_apply) return "excluded game can_apply: " + d;
                if (r.excluded) {
                    for (const auto& c : r.changes) return "excluded game lists a change '" + c + "' (it says VegaFlow writes nothing): " + d;
                }
                if (r.can_dxvk && r.dxvk_hint.empty()) return "can_dxvk with no hint: " + d;
                if (!r.can_dxvk && !r.dxvk_hint.empty()) return "dxvk hint without the button: " + d;
                std::string all = r.detail + r.file_hint + r.backup_hint + r.dxvk_hint + m.empty_text;
                for (const auto& c : r.changes) all += c;
                if (Promises(all)) return "text promises a gain: " + d;
            }
        }
        return "";
    });

    Attack("Test 76.3: a game with no program found does not list changes while its own sentence says VegaFlow changes nothing in it", [&]() -> std::string {
        // Unreal, no anti-cheat, not excluded, primary_exe_path empty: status NoProfile, can_apply false.
        const SteamGameEntry g = Game(9, "Unreal sin exe", GameEngineType::UnrealEngine4_5, L"");
        const auto r = gs::BuildGamesScreen({g}, Policy(), 4096, {}).rows.at(0);
        if (r.can_apply) return "can_apply true with no exe";
        if (!r.changes.empty() || !r.file_hint.empty() || !r.backup_hint.empty()) {
            return "detail says '" + r.detail + "' but file_hint='" + r.file_hint + "' and " + std::to_string(r.changes.size()) + " changes are listed";
        }
        return "";
    });

    Attack("Test 76.4: 5000 games (duplicate names and app ids included) give 5000 rows sorted by name", [&]() -> std::string {
        std::vector<SteamGameEntry> games;
        for (uint32_t i = 0; i < 5000; ++i) {
            games.push_back(Game(i % 700, "Juego " + std::to_string((i * 7919) % 1000), engines[i % 5], L"g" + std::to_wstring(i) + L".exe", i % 11 == 0, i % 3 == 0, i % 5 == 0));
        }
        const auto m = gs::BuildGamesScreen(games, Policy(), 4096, {L"g1.exe", L"G2.EXE"});
        if (m.rows.size() != 5000) return "rows " + std::to_string(m.rows.size());
        for (size_t i = 1; i < m.rows.size(); ++i) {
            if (m.rows[i].name < m.rows[i - 1].name) return "not sorted at " + std::to_string(i);
        }
        return "";
    });

    Attack("Test 76.5: the model is a pure function: the same inputs twice give the same rows", [&]() -> std::string {
        const std::vector<SteamGameEntry> games = {Game(1, "B", GameEngineType::Unity, L"b.exe"), Game(2, "A", GameEngineType::UnrealEngine4_5, L"a.exe", false, true)};
        const auto a = gs::BuildGamesScreen(games, Policy(), 2048, {L"b.exe"});
        const auto b = gs::BuildGamesScreen(games, Policy(), 2048, {L"b.exe"});
        if (a.rows.size() != b.rows.size()) return "sizes differ";
        for (size_t i = 0; i < a.rows.size(); ++i) {
            if (a.rows[i].detail != b.rows[i].detail || a.rows[i].changes != b.rows[i].changes || a.rows[i].status != b.rows[i].status) return "rows differ";
        }
        return "";
    });

    Attack("Test 76.6: exe paths with folders, trailing separator, case and Unicode are matched by file name only", [&]() -> std::string {
        const auto row = [&](const std::wstring& exe_path, const std::vector<std::wstring>& ex) {
            SteamGameEntry g = Game(1, "X", GameEngineType::Unity, L"");
            g.primary_exe_path = exe_path;
            return gs::BuildGamesScreen({g}, Policy(), 4096, ex).rows.at(0);
        };
        if (!row(L"D:/games/x/Game.exe", {L"GAME.exe"}).excluded) return "forward slashes not handled";
        if (row(L"C:\\a\\Game.exe\\", {L"Game.exe"}).excluded) return "a folder path with trailing separator counted as the exe";
        if (row(L"C:\\a\\Game.exe", {L"C:\\a\\Game.exe"}).excluded) return "an excluded entry with a folder part matched (the Rust rules never allow one)";
        if (row(L"C:\\a\\Game.exe", {L"Game.exe.", L"Game.ex", L"xGame.exe"}).excluded) return "near-miss names matched";
        const auto empty_exe = row(L"", {L"", L"x.exe"});
        if (empty_exe.excluded) return "a game with no exe was excluded by an empty entry";
        if (empty_exe.detail.empty()) return "empty detail for a game with no exe";
        return "";
    });

    Attack("Test 76.7: names that Rust refuses are refused by ExcludeGame, with a sentence and the list untouched", [&]() -> std::string {
        const std::vector<std::string> bad = {"",
                                              " ",
                                              "\\",
                                              "C:\\Games\\",
                                              "C:\\Windows\\explorer.exe",
                                              "..\\explorer.exe",
                                              "EXPLORER.EXE",
                                              "ExPlOrEr.ExE",
                                              "vegaflow_daemon.exe",
                                              "svchost.exe",
                                              "con.exe",
                                              "NUL.exe",
                                              "com1.exe",
                                              "lpt9.exe",
                                              "x.exe ",
                                              " x.exe",
                                              "x.exe.",
                                              "x.exe. ",
                                              "x.txt",
                                              "x",
                                              ".exe",
                                              "x.exe:stream",
                                              "x*.exe",
                                              "x?.exe",
                                              "x\".exe",
                                              "x<y>.exe",
                                              "x|y.exe",
                                              std::string("x\x01y.exe"),
                                              "x\ny.exe",
                                              "x.exe\n",
                                              std::string("a\0b.exe", 7),
                                              std::string(252, 'a') + ".exe",
                                              ".",
                                              ".."};
        for (const auto& name : bad) {
            AppConfig cfg;
            std::string err = "stale";
            if (gs::ExcludeGame(cfg, name, &err)) return "accepted: " + (name.size() > 40 ? name.substr(0, 40) + "..." : name);
            if (!cfg.process_policy.excluded_game_exes.empty()) return "list changed by a refused name";
            if (err.empty() || err == "stale") return "refused with no sentence: " + name;
            if (gs::ExcludeGame(cfg, name, nullptr)) return "accepted with a null error pointer: " + name;
        }
        return "";
    });

    Attack("Test 76.8: valid names with folders, case, Unicode and the longest length are accepted once", [&]() -> std::string {
        struct Case {
            std::string input;
            std::wstring stored;
        };
        const std::vector<Case> good = {{"..\\x.exe", L"x.exe"},
                                        {"C:\\Games\\Foo\\Bar.EXE", L"Bar.EXE"},
                                        {"A/B/c.exe", L"c.exe"},
                                        {"\\\\server\\share\\dir\\net.exe", L"net.exe"},
                                        {"Ünï código.exe", L"Ünï código.exe"},
                                        {"日本語.exe", L"日本語.exe"},
                                        {std::string(251, 'a') + ".exe", std::wstring(251, L'a') + L".exe"},
                                        {"a b.exe", L"a b.exe"}};
        for (const auto& c : good) {
            AppConfig cfg;
            std::string err;
            if (!gs::ExcludeGame(cfg, c.input, &err)) return "refused a valid name: " + c.input.substr(0, 30) + " (" + err + ")";
            if (cfg.process_policy.excluded_game_exes.size() != 1 || cfg.process_policy.excluded_game_exes[0] != c.stored) return "stored the wrong name for " + c.input.substr(0, 30);
            if (gs::ExcludeGame(cfg, c.input, &err)) return "excluded twice: " + c.input.substr(0, 30);
            if (err.empty()) return "duplicate refused with no sentence";
            if (cfg.process_policy.excluded_game_exes.size() != 1) return "duplicate added";
        }
        return "";
    });

    Attack("Test 76.9: a duplicate in another letter case (ASCII or not) is refused; Include removes it from any spelling", [&]() -> std::string {
        AppConfig cfg;
        std::string err;
        if (!gs::ExcludeGame(cfg, "Nandu.exe", &err)) return "first refused";
        if (gs::ExcludeGame(cfg, "nANDU.EXE", &err)) return "ASCII case duplicate accepted";
        if (cfg.process_policy.excluded_game_exes.size() != 1) return "list size " + std::to_string(cfg.process_policy.excluded_game_exes.size());
        if (!gs::IncludeGame(cfg, "C:\\x\\NANDU.exe")) return "Include did not find it by file name";
        if (!cfg.process_policy.excluded_game_exes.empty()) return "still listed";
        if (gs::IncludeGame(cfg, "Nandu.exe")) return "Include reported a change twice";
        return "";
    });

    Attack("Test 76.12: a name that is not valid UTF-8 is refused instead of being stored with replacement characters", [&]() -> std::string {
        AppConfig cfg;
        std::string err;
        if (gs::ExcludeGame(cfg, "\xff\xfe.exe", &err)) {
            const auto& n = cfg.process_policy.excluded_game_exes.at(0);
            return "accepted bytes FF FE 2E 65 78 65 and stored " + std::to_string(n.size()) + " wide chars, first U+" + std::to_string(static_cast<unsigned>(n[0]));
        }
        return "";
    });

    Attack("Test 76.13: letter case is ignored for non-ASCII letters too (Windows file names are case-insensitive): the model matches, ExcludeGame sees duplicates", [&]() -> std::string {
        SteamGameEntry g = Game(1, "X", GameEngineType::Unity, L"Ünï.EXE");
        const bool model_matches = gs::BuildGamesScreen({g}, Policy(), 4096, {L"ünï.exe"}).rows.at(0).excluded;
        AppConfig cfg;
        std::string err;
        if (!gs::ExcludeGame(cfg, "Ñandú.exe", &err)) return "first refused";
        const bool dup_stored = gs::ExcludeGame(cfg, "ñANDÚ.EXE", &err);
        if (!model_matches && dup_stored) return "model: Ünï.EXE is not matched by the entry ünï.exe; ExcludeGame: Ñandú.exe and ñANDÚ.EXE both stored";
        if (!model_matches) return "model: Ünï.EXE is not matched by the entry ünï.exe";
        if (dup_stored) return "ExcludeGame: Ñandú.exe and ñANDÚ.EXE both stored (" + std::to_string(cfg.process_policy.excluded_game_exes.size()) + " entries)";
        return "";
    });

    Attack("Test 76.10: Include with empty, folder-only, unknown and null-bearing names changes nothing and does not throw", [&]() -> std::string {
        AppConfig cfg;
        std::string err;
        gs::ExcludeGame(cfg, "keep.exe", &err);
        for (const std::string& name : {std::string(""), std::string("\\"), std::string("C:\\dir\\"), std::string("nope.exe"), std::string("keep.exe\0x", 10), std::string("..")}) {
            if (gs::IncludeGame(cfg, name)) return "Include changed the list for '" + name + "'";
        }
        if (cfg.process_policy.excluded_game_exes.size() != 1) return "list changed";
        return "";
    });

    Attack("Test 76.11: Exclude then Include 1000 times leaves the list as it started", [&]() -> std::string {
        AppConfig cfg;
        std::string err;
        for (int i = 0; i < 1000; ++i) {
            const std::string name = "g" + std::to_string(i % 37) + ".exe";
            if (i % 2 == 0) {
                gs::ExcludeGame(cfg, name, &err);
            } else {
                gs::IncludeGame(cfg, "g" + std::to_string((i - 1) % 37) + ".EXE");
            }
        }
        if (!cfg.process_policy.excluded_game_exes.empty()) return "left " + std::to_string(cfg.process_policy.excluded_game_exes.size()) + " names";
        return "";
    });
}

} // namespace vftest
