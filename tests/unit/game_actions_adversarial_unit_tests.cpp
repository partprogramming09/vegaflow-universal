// Adversarial unit tests: the Juegos buttons (GameActions) with unknown AppIDs, anti-cheat and excluded games, repeated
// clicks, writes that fail halfway, a refused save after files were already changed, and a hostile library.

#include "optimize/game_files_adapter.hpp"
#include "session/game_actions.hpp"
#include "fake_file_system.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::DxvkManager;
using vegaflow::GameEngineType;
using vegaflow::SteamEngineOptimizer;
using vegaflow::SteamGameEntry;
using vegaflow::app::AppConfig;
using vegaflow::app::ConfigStore;

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

SteamGameEntry Game(uint32_t id, GameEngineType engine, const std::wstring& exe, bool anticheat = false) {
    SteamGameEntry g;
    g.app_id = id;
    g.name = "VfAdvZzq" + std::to_string(id);
    g.engine = engine;
    g.install_dir = L"C:\\Steam\\steamapps\\common\\VfAdvZzq" + std::to_wstring(id) + L"Game";
    g.primary_exe_path = exe.empty() ? L"" : g.install_dir + L"\\" + exe;
    g.has_anticheat = anticheat;
    return g;
}

// A name no real folder under %LOCALAPPDATA% matches, so the Engine.ini path is only the default one.
SteamGameEntry Unreal(uint32_t id, bool anticheat = false) {
    return Game(id, GameEngineType::UnrealEngine4_5, L"VfAdvZzq" + std::to_wstring(id) + L"Game-Win64-Shipping.exe", anticheat);
}

struct Rig {
    FakeFileSystem fs;
    FakeRegistryPort reg;
    SteamEngineOptimizer steam{fs, &reg};
    DxvkManager dxvk{fs};
    vegaflow::OptimizeGameFiles files{steam, dxvk};
    ConfigStore store{fs, L"C:\\VegaFlow\\config.json", true};
    AppConfig config;
    std::vector<SteamGameEntry> games;
    std::vector<std::string> audit;
    int inspects = 0;
    bool inspect_applied = false;
    int scans = 0;
    std::vector<SteamGameEntry> scan_result;
    gs::GameActions act;

    explicit Rig(std::vector<SteamGameEntry> g)
        : games(std::move(g)),
          act(gs::GameActionsDeps{files, config, store, games, DetectedPolicy(),
                                  [this]() { ++scans; return scan_result; },
                                  [this](const SteamGameEntry& x) { ++inspects; SteamGameEntry n = x; n.engine_preset_applied = inspect_applied; return n; },
                                  [this](const std::string& s) { audit.push_back(s); }}) {
        config = store.Load();
    }
};

bool EndsWith(const std::wstring& s, const std::wstring& suffix) { return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0; }

std::wstring FindIni(const FakeFileSystem& fs) {
    for (const auto& kv : fs.files) {
        if (EndsWith(kv.first, L"Engine.ini")) return kv.first;
    }
    return L"";
}

const std::string kOriginalIni = "[Original]\r\nr.Example=1\r\n";

// Where the Engine.ini of Unreal(1) lives (found by letting a first Apply write it on a clean fake).
std::wstring ProbeIniPath() {
    Rig p({Unreal(1)});
    p.act.Apply(1);
    return FindIni(p.fs);
}

std::string FilesDiff(const FakeFileSystem& a, const std::map<std::wstring, std::string>& before) {
    std::string out;
    for (const auto& kv : a.files) {
        const auto it = before.find(kv.first);
        if (it == before.end()) out += "new:" + std::string(kv.first.begin(), kv.first.end()) + " ";
        else if (it->second != kv.second) out += "changed:" + std::string(kv.first.begin(), kv.first.end()) + " ";
    }
    for (const auto& kv : before) {
        if (!a.files.count(kv.first)) out += "removed:" + std::string(kv.first.begin(), kv.first.end()) + " ";
    }
    return out;
}

} // namespace

void Test78() {
    // Test 78: attacks on the Juegos buttons
    Attack("Test 78.1: unknown AppIDs (0, huge) on every button change nothing, say so, and never throw; also on an empty library", [&]() -> std::string {
        for (const bool empty : {true, false}) {
            Rig r(empty ? std::vector<SteamGameEntry>{} : std::vector<SteamGameEntry>{Unreal(5)});
            for (const uint32_t id : {0u, 4u, UINT32_MAX}) {
                const std::string msgs[] = {r.act.Apply(id), r.act.Restore(id), r.act.Exclude(id), r.act.Include(id), r.act.ToggleDxvk(id)};
                for (const auto& m : msgs) {
                    if (m.empty()) return "empty message for id " + std::to_string(id);
                }
            }
            if (r.fs.Changes() != 0 || !r.reg.writes.empty()) return "files or registry written for an unknown id";
            if (!r.config.process_policy.excluded_game_exes.empty()) return "config changed for an unknown id";
            if (r.inspects != 0) return "game files were inspected for an unknown id";
        }
        return "";
    });

    Attack("Test 78.2: Apply on an anti-cheat Unreal game writes no engine file, no copy and no backup (registry value only)", [&]() -> std::string {
        Rig r({Unreal(1, true)});
        r.act.Apply(1);
        if (r.fs.Changes() != 0) return "fs changes: " + std::to_string(r.fs.Changes());
        if (!r.fs.files.empty()) return "files created: " + std::to_string(r.fs.files.size());
        r.act.Restore(1);
        if (r.fs.Changes() != 0) return "Restore touched files of an anti-cheat game";
        return "";
    });

    Attack("Test 78.3: ToggleDxvk on an anti-cheat game is refused twice in a row and leaves flag and files alone", [&]() -> std::string {
        Rig r({Game(1, GameEngineType::Unity, L"Game.exe", true)});
        const std::string a = r.act.ToggleDxvk(1);
        const std::string b = r.act.ToggleDxvk(1);
        if (r.games[0].dxvk_installed || r.fs.Changes() != 0) return "DXVK flag or files changed";
        if (a.empty() || b.empty()) return "empty message";
        return "";
    });

    Attack("Test 78.4: removing DXVK from an anti-cheat game (flag set) does not delete or copy files in its folder", [&]() -> std::string {
        // RemoveDxvk looks at the real folder, so this one exists on disk (empty); the file system calls are still the fake's.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "vf_adv_dxvk_anticheat";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{dir};
        SteamGameEntry g = Game(1, GameEngineType::Unity, L"Game.exe", true);
        g.primary_exe_path = (dir / L"Game.exe").wstring();
        g.dxvk_installed = true;
        Rig r({g});
        r.act.ToggleDxvk(1);
        if (!r.fs.removes.empty() || !r.fs.copies.empty() || r.fs.Changes() != 0) {
            return "anti-cheat game: " + std::to_string(r.fs.removes.size()) + " removes and " + std::to_string(r.fs.copies.size()) + " copies in its folder";
        }
        return "";
    });

    Attack("Test 78.5: Apply on a game that is in the excluded list writes nothing (the screen says VegaFlow writes nothing for it)", [&]() -> std::string {
        Rig r({Unreal(1)});
        r.config.process_policy.excluded_game_exes = {L"VfAdvZzq1Game-Win64-Shipping.exe"};
        const std::string msg = r.act.Apply(1);
        if (r.fs.Changes() != 0 || !r.reg.writes.empty()) {
            return "excluded game: " + std::to_string(r.fs.Changes()) + " file changes and " + std::to_string(r.reg.writes.size()) + " registry writes; message '" + msg + "'";
        }
        return "";
    });

    Attack("Test 78.6: DXVK is not deployed to a game that is in the excluded list", [&]() -> std::string {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "vf_adv_dxvk_excluded";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{dir};
        SteamGameEntry g = Game(1, GameEngineType::Unity, L"Excl.exe");
        g.primary_exe_path = (dir / L"Excl.exe").wstring();
        Rig r({g});
        r.config.process_policy.excluded_game_exes = {L"Excl.exe"};
        r.act.ToggleDxvk(1);
        if (r.games[0].dxvk_installed || r.fs.Changes() != 0) {
            return "excluded game: dxvk_installed=" + std::string(r.games[0].dxvk_installed ? "true" : "false") + ", " + std::to_string(r.fs.Changes()) + " file changes";
        }
        return "";
    });

    Attack("Test 78.7: Apply twice never overwrites the original backup with the modified file; Restore then returns the original", [&]() -> std::string {
        const std::wstring ini = ProbeIniPath();
        if (ini.empty()) return "blind: the first Apply wrote no Engine.ini";
        Rig r({Unreal(1)});
        r.fs.files[ini] = kOriginalIni;
        r.act.Apply(1);
        const std::string after_one = r.fs.files[ini];
        if (after_one == kOriginalIni) return "blind: Apply did not change Engine.ini";
        r.act.Apply(1);
        if (r.fs.files[ini] != after_one) return "second Apply changed Engine.ini again (not idempotent)";
        if (r.fs.files[ini + L".vegaflow.bak"] != kOriginalIni) return "backup after two Applies is no longer the original: '" + r.fs.files[ini + L".vegaflow.bak"] + "'";
        r.act.Restore(1);
        if (r.fs.files[ini] != kOriginalIni) return "Restore did not return the original; has '" + r.fs.files[ini] + "'";
        return "";
    });

    Attack("Test 78.8: Restore twice, and Restore on a game never applied, leave the original file exactly as it was", [&]() -> std::string {
        const std::wstring ini = ProbeIniPath();
        if (ini.empty()) return "blind: no Engine.ini path";
        Rig r({Unreal(1)});
        r.fs.files[ini] = kOriginalIni;
        r.act.Restore(1);
        if (r.fs.files[ini] != kOriginalIni) return "Restore on a never-applied game changed the file";
        r.act.Apply(1);
        r.act.Restore(1);
        r.act.Restore(1);
        if (r.fs.files[ini] != kOriginalIni) return "second Restore damaged the file: '" + r.fs.files[ini] + "'";
        r.act.Apply(1);
        r.act.Restore(1);
        if (r.fs.files[ini] != kOriginalIni) return "Apply-Restore cycle did not end at the original";
        return "";
    });

    Attack("Test 78.9: a write that fails halfway (write, copy, replace, remove) leaves Engine.ini and the flags telling the truth", [&]() -> std::string {
        const std::wstring ini = ProbeIniPath();
        if (ini.empty()) return "blind: no Engine.ini path";
        for (int mode = 0; mode < 4; ++mode) {
            Rig r({Unreal(1)});
            r.fs.files[ini] = kOriginalIni;
            r.fs.fail_write = mode == 0;
            r.fs.fail_copy = mode == 1;
            r.fs.fail_replace = mode == 2;
            r.fs.fail_remove = mode == 3;
            const std::string msg = r.act.Apply(1);
            const bool changed = r.fs.files[ini] != kOriginalIni;
            const char* what[] = {"fail_write", "fail_copy", "fail_replace", "fail_remove"};
            if (msg.empty()) return std::string(what[mode]) + ": empty message";
            if (r.games[0].engine_preset_applied && !changed) return std::string(what[mode]) + ": flag says applied but Engine.ini is the original";
            if (!r.games[0].engine_preset_applied && changed) return std::string(what[mode]) + ": flag says not applied but Engine.ini was changed";
        }
        return "";
    });

    Attack("Test 78.10: 'Me fue mal' refused by the save (list already at its 1024 limit) leaves the game's files as they were", [&]() -> std::string {
        const std::wstring ini = ProbeIniPath();
        if (ini.empty()) return "blind: no Engine.ini path";
        const auto scenario = [&](bool refuse_save, std::string* files_diff, std::string* msg) {
            Rig r({Unreal(1)});
            r.fs.files[ini] = kOriginalIni;
            r.act.Apply(1);
            r.inspect_applied = true; // the daemon applied it: the flag read from disk says so
            if (refuse_save) {
                for (int i = 0; i < 1024; ++i) r.config.process_policy.excluded_game_exes.push_back(L"filler" + std::to_wstring(i) + L".exe");
            }
            const auto before = r.fs.files;
            *msg = r.act.Exclude(1);
            *files_diff = FilesDiff(r.fs, before);
            return r.config.process_policy.excluded_game_exes.size();
        };
        std::string diff_ok, msg_ok, diff_refused, msg_refused;
        const size_t listed_ok = scenario(false, &diff_ok, &msg_ok);
        const size_t listed_refused = scenario(true, &diff_refused, &msg_refused);
        if (listed_ok != 1) return "control failed: the normal Exclude did not exclude (" + msg_ok + ")";
        if (diff_ok.empty()) return "blind: the normal Exclude restored nothing";
        if (listed_refused != 1024) return "the refused save changed the list: " + std::to_string(listed_refused);
        if (!diff_refused.empty()) return "save refused ('" + msg_refused + "') but files changed: " + diff_refused;
        return "";
    });

    Attack("Test 78.11: 'Me fue mal' with a save that fails to write leaves the game's files as they were", [&]() -> std::string {
        const std::wstring ini = ProbeIniPath();
        if (ini.empty()) return "blind: no Engine.ini path";
        Rig r({Unreal(1)});
        r.fs.files[ini] = kOriginalIni;
        r.act.Apply(1);
        r.inspect_applied = true;
        const auto before = r.fs.files;
        const auto replaces_before = r.fs.replaces.size();
        r.fs.fail_replace = true; // config.json is written with Replace
        const std::string msg = r.act.Exclude(1);
        r.fs.fail_replace = false;
        if (!r.config.process_policy.excluded_game_exes.empty()) return "config changed although the write failed";
        if (msg.find("No se guardó") == std::string::npos) return "message does not say it was not saved: " + msg;
        (void)replaces_before;
        const std::string diff = FilesDiff(r.fs, before);
        if (!diff.empty()) return "write failed ('" + msg + "') but files changed: " + diff;
        return "";
    });

    Attack("Test 78.12: Exclude twice: the second says already excluded, writes nothing and keeps one entry; Include twice likewise", [&]() -> std::string {
        Rig r({Game(1, GameEngineType::NativeDirectX_Vulkan, L"Game.exe")});
        r.act.Exclude(1);
        const size_t w = r.fs.writes.size() + r.fs.replaces.size();
        const std::string second = r.act.Exclude(1);
        if (r.fs.writes.size() + r.fs.replaces.size() != w) return "second Exclude wrote config.json again";
        if (r.config.process_policy.excluded_game_exes.size() != 1) return "list size " + std::to_string(r.config.process_policy.excluded_game_exes.size());
        if (second.empty() || second.find("excluido") == std::string::npos) return "second message: " + second;
        r.act.Include(1);
        const size_t w2 = r.fs.writes.size() + r.fs.replaces.size();
        const std::string again = r.act.Include(1);
        if (r.fs.writes.size() + r.fs.replaces.size() != w2) return "second Include wrote config.json again";
        if (!r.config.process_policy.excluded_game_exes.empty()) return "still excluded";
        if (again.find("no estaba excluido") == std::string::npos) return "second Include message: " + again;
        return "";
    });

    Attack("Test 78.13: a game whose program is a Windows process or a folder is refused before anything is inspected or saved", [&]() -> std::string {
        for (const std::wstring& exe : {std::wstring(L"..\\..\\Windows\\explorer.exe"), std::wstring(L"svchost.exe"), std::wstring(L"CON.exe"), std::wstring(L"dir\\")}) {
            Rig r({Game(1, GameEngineType::Unity, L"x.exe")});
            r.games[0].primary_exe_path = L"C:\\Games\\" + exe;
            const std::string msg = r.act.Exclude(1);
            if (!r.config.process_policy.excluded_game_exes.empty()) return "excluded '" + std::string(exe.begin(), exe.end()) + "'";
            if (r.inspects != 0 || r.fs.Changes() != 0) return "inspected or wrote before refusing";
            if (msg.empty()) return "empty message";
        }
        Rig none({Game(1, GameEngineType::Unity, L"")});
        if (none.act.Exclude(1).empty() || !none.config.process_policy.excluded_game_exes.empty() || none.fs.Changes() != 0) return "game with no program";
        return "";
    });

    Attack("Test 78.14: a failed save in Include keeps the game excluded and says it was not saved", [&]() -> std::string {
        Rig r({Game(1, GameEngineType::NativeDirectX_Vulkan, L"Game.exe")});
        r.act.Exclude(1);
        r.fs.fail_replace = true;
        const std::string msg = r.act.Include(1);
        r.fs.fail_replace = false;
        if (r.config.process_policy.excluded_game_exes.size() != 1) return "config changed";
        if (msg.find("No se guardó") == std::string::npos) return msg;
        if (!r.act.Include(1).empty() && r.config.process_policy.excluded_game_exes.size() != 0) return "a later Include with a healthy disk did not work";
        return "";
    });

    Attack("Test 78.15: duplicate AppIDs: the buttons act on the first game with that id and leave the other alone", [&]() -> std::string {
        Rig r({Game(7, GameEngineType::NativeDirectX_Vulkan, L"A.exe"), Game(7, GameEngineType::NativeDirectX_Vulkan, L"B.exe")});
        r.act.Apply(7);
        if (!r.games[0].engine_preset_applied || r.games[1].engine_preset_applied) return "Apply flags: first=" + std::to_string(r.games[0].engine_preset_applied) + " second=" + std::to_string(r.games[1].engine_preset_applied);
        r.act.Exclude(7);
        const auto& ex = r.config.process_policy.excluded_game_exes;
        if (ex.size() != 1 || ex[0] != L"A.exe") return "excluded the wrong exe";
        return "";
    });

    Attack("Test 78.16: every button answers with a non-empty sentence for every kind of game, and audits exactly what it says", [&]() -> std::string {
        const std::vector<SteamGameEntry> kinds = {Unreal(1), Unreal(2, true), Game(3, GameEngineType::Unity, L"u.exe"), Game(4, GameEngineType::Unity, L""),
                                                   Game(5, GameEngineType::Unknown, L"k.exe"), Game(6, GameEngineType::Source1_2, L"s.exe", true)};
        for (const auto& g : kinds) {
            Rig r({g});
            const uint32_t id = g.app_id;
            const std::vector<std::pair<std::string, std::string>> out = {{"Apply", r.act.Apply(id)},   {"Restore", r.act.Restore(id)}, {"ToggleDxvk", r.act.ToggleDxvk(id)},
                                                                         {"Exclude", r.act.Exclude(id)}, {"Include", r.act.Include(id)},   {"Exclude2", r.act.Exclude(id)}};
            for (const auto& [what, msg] : out) {
                if (msg.empty()) return what + " on app " + std::to_string(id) + " gave an empty message";
            }
            if (r.audit.size() < out.size()) return "app " + std::to_string(id) + ": " + std::to_string(r.audit.size()) + " audit lines for " + std::to_string(out.size()) + " clicks";
        }
        return "";
    });

    Attack("Test 78.17: Rescan with 3000 games, planted Windows names and duplicates: custom list holds only valid, unique names; nothing is written", [&]() -> std::string {
        Rig r({});
        for (uint32_t i = 0; i < 3000; ++i) {
            r.scan_result.push_back(Game(i, GameEngineType::Unity, L"game" + std::to_wstring(i % 2000) + (i % 2 ? L".EXE" : L".exe")));
        }
        r.scan_result.push_back(Game(9000, GameEngineType::Unity, L"explorer.exe"));
        r.scan_result.push_back(Game(9001, GameEngineType::Unity, L"CON.exe"));
        r.scan_result.push_back(Game(9002, GameEngineType::Unity, L""));
        const std::string msg = r.act.Rescan();
        if (r.games.size() != 3003) return "games " + std::to_string(r.games.size());
        if (msg.find("3003 juegos") == std::string::npos) return msg;
        std::vector<std::wstring> lower;
        for (auto n : r.config.custom_game_exes) {
            for (auto& c : n) c = static_cast<wchar_t>(std::towlower(c));
            lower.push_back(n);
        }
        std::sort(lower.begin(), lower.end());
        if (std::adjacent_find(lower.begin(), lower.end()) != lower.end()) return "duplicate names in custom_game_exes";
        if (std::find(lower.begin(), lower.end(), L"explorer.exe") != lower.end() || std::find(lower.begin(), lower.end(), L"con.exe") != lower.end()) return "a Windows name became a game";
        if (r.fs.Changes() != 0) return "Rescan wrote files";
        const std::string again = r.act.Rescan();
        if (again != msg || r.scans != 2) return "second Rescan differs: " + again;
        return "";
    });

    Attack("Test 78.18: Rescan to an empty library gives an empty list, '0 juegos', and drops the Steam names from the daemon's list", [&]() -> std::string {
        Rig r({Game(1, GameEngineType::Unity, L"a.exe")});
        r.act.Rescan();
        if (!r.games.empty()) return "games not emptied";
        if (!r.config.custom_game_exes.empty()) return "Steam names stayed in custom_game_exes";
        return r.act.Poll().rows.empty() ? "" : "Poll shows rows";
    });

    Attack("Test 78.20: no sentence for the status bar promises FPS, 'mejora' or 'más rápido' (Apply, Restore, Exclude, Include, DXVK)", [&]() -> std::string {
        const auto promises = [](std::string t) {
            for (char& c : t) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            return t.find("fps") != std::string::npos || t.find("mejora") != std::string::npos || t.find("más rápido") != std::string::npos || t.find("rápid") != std::string::npos;
        };
        Rig r({Unreal(1), Game(2, GameEngineType::Unity, L"u.exe"), Game(3, GameEngineType::NativeDirectX_Vulkan, L"n.exe")});
        for (const uint32_t id : {1u, 2u, 3u}) {
            const std::pair<const char*, std::string> out[] = {{"Apply", r.act.Apply(id)}, {"Restore", r.act.Restore(id)}, {"ToggleDxvk", r.act.ToggleDxvk(id)},
                                                              {"Exclude", r.act.Exclude(id)}, {"Include", r.act.Include(id)}};
            for (const auto& [what, msg] : out) {
                if (promises(msg)) return std::string(what) + " on app " + std::to_string(id) + " answers: '" + msg + "'";
            }
        }
        return "";
    });

    Attack("Test 78.19: RefreshFlags with a missing inspect callback, folder-less games and an unknown id changes nothing and does not throw", [&]() -> std::string {
        Rig r({Game(1, GameEngineType::Unity, L"a.exe"), Game(2, GameEngineType::Unity, L"b.exe")});
        r.games[1].install_dir.clear();
        r.games[0].engine_preset_applied = true;
        r.inspect_applied = false;
        r.act.RefreshFlags(0);
        if (r.inspects != 1) return "inspected " + std::to_string(r.inspects) + " games, expected 1 (the one with a folder)";
        if (r.games[0].engine_preset_applied) return "flag not refreshed";
        r.act.RefreshFlags(999);
        if (r.inspects != 1) return "unknown id inspected something";
        return "";
    });
}

} // namespace vftest
