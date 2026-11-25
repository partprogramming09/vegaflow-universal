// Unit tests: what the buttons of the "Juegos" screen do (phase G), driven with a fake file system and registry:
// apply, restore, "Me fue mal" (restore + exclude + save), include again, DXVK refusal on anti-cheat, rescan, refresh.

#include "optimize/game_files_adapter.hpp"
#include "session/game_actions.hpp"
#include "fake_file_system.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

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

SteamGameEntry Unity(uint32_t id, const wchar_t* exe, bool anticheat = false) {
    SteamGameEntry g;
    g.app_id = id;
    g.name = "Game" + std::to_string(id);
    g.engine = GameEngineType::Unity;
    g.install_dir = L"C:\\Steam\\steamapps\\common\\Game" + std::to_wstring(id);
    g.primary_exe_path = g.install_dir + L"\\" + exe;
    g.has_anticheat = anticheat;
    return g;
}

} // namespace

void Test72() {
    // Test 72: the Juegos buttons act on the right game, change files only through the ports, and save only what is valid
    FakeFileSystem fs;
    FakeRegistryPort reg;
    SteamEngineOptimizer steam(fs, &reg);
    DxvkManager dxvk(fs);
    vegaflow::OptimizeGameFiles files(steam, dxvk);
    AppConfig config;
    ConfigStore store(fs, L"C:\\VegaFlow\\config.json", true);
    config = store.Load();
    SteamGameEntry plain = Unity(4, L"Plain.exe");
    plain.engine = GameEngineType::Unknown;
    std::vector<SteamGameEntry> games = {Unity(1, L"Octopath.exe"), Unity(2, L"Guarded.exe", true), Unity(3, L"Other.exe"), plain};
    std::vector<std::string> audit;
    bool inspect_applied = false;
    int scans = 0;
    gs::GameActionsDeps deps{files, config, store, games, DetectedPolicy(),
                             [&]() { ++scans; return std::vector<SteamGameEntry>{Unity(7, L"A.exe"), Unity(8, L"B.exe")}; },
                             [&](const SteamGameEntry& g) { SteamGameEntry n = g; n.engine_preset_applied = inspect_applied; return n; },
                             [&](const std::string& s) { audit.push_back(s); }};
    gs::GameActions act(deps);

    // 1. Apply goes through the registry port (a game with no engine profile has no file to change), sets the cached flags and
    // logs one audit line; Restore clears the profile flag. (Engine files are covered by the Ini and Steam tests.)
    const std::string applied_msg = act.Apply(4);
    const bool apply_ok = games[3].engine_preset_applied && games[3].flip_model_applied && !reg.writes.empty() && fs.Changes() == 0 && !applied_msg.empty() && audit.size() == 1;
    const std::string restored_msg = act.Restore(4);
    const bool restore_ok = !games[3].engine_preset_applied && audit.size() == 2 && !restored_msg.empty();
    RecordResult(apply_ok && restore_ok, "Test 72.1: Apply writes the flip-model value through the registry port and sets the flags; Restore clears the profile flag");

    // 2. An unknown AppID changes nothing and says so.
    const size_t before = fs.Changes();
    const std::string nf = act.Apply(99) + act.Restore(99) + act.ToggleDxvk(99);
    RecordResult(fs.Changes() == before && nf.find("no encontrado") != std::string::npos, "Test 72.2: an unknown AppID changes nothing and is reported");

    // 3. DXVK on an anti-cheat game is refused: no file touched, flag unchanged.
    const size_t before_dx = fs.Changes();
    act.ToggleDxvk(2);
    RecordResult(fs.Changes() == before_dx && !games[1].dxvk_installed, "Test 72.3: DXVK is not deployed to an anti-cheat game");

    // 4. "Me fue mal": an invalid name stops before any file or save; a valid one restores the file, saves and excludes.
    games[2].primary_exe_path = L"C:\\Windows\\explorer.exe";
    const size_t before_bad = fs.Changes();
    act.Exclude(3);
    const bool bad_ok = fs.Changes() == before_bad && config.process_policy.excluded_game_exes.empty();
    inspect_applied = true; // the daemon applied the profile after the scan: the flags are read again first
    games[0].engine_preset_applied = false;
    fs.files.erase(L"C:\\VegaFlow\\config.json");
    const std::string ex_msg = act.Exclude(1);
    const bool excl_ok = config.process_policy.excluded_game_exes.size() == 1 && ex_msg.find("excluido") != std::string::npos &&
                         fs.files.count(L"C:\\VegaFlow\\config.json") == 1 && !games[0].engine_preset_applied;
    RecordResult(bad_ok && excl_ok, "Test 72.4: Me fue mal refuses an invalid name, and for a valid one restores the file, saves and excludes");

    // 5. Poll shows it excluded; a failed save leaves the config as it was and says it was not saved.
    const gs::GamesScreenModel m = act.Poll();
    bool shown = false;
    for (const auto& r : m.rows) shown = shown || (r.app_id == 1 && r.excluded);
    games[1].primary_exe_path = L"C:\\Steam\\steamapps\\common\\Game2\\Guarded.exe";
    fs.fail_write = true;
    fs.fail_replace = true;
    const std::string fail_msg = act.Exclude(2);
    fs.fail_write = false;
    fs.fail_replace = false;
    RecordResult(shown && config.process_policy.excluded_game_exes.size() == 1 && fail_msg.find("No se guardó") != std::string::npos,
                 "Test 72.5: Poll marks the excluded game; a failed save keeps the config unchanged and says so");

    // 6. Include removes it and saves; including a game that is not excluded says so.
    const std::string in_msg = act.Include(1);
    const bool in_ok = config.process_policy.excluded_game_exes.empty() && in_msg.find("incluido") != std::string::npos;
    const bool not_excluded = act.Include(1).find("no estaba excluido") != std::string::npos;
    RecordResult(in_ok && not_excluded, "Test 72.6: Include puts the game back and saves; a game that was not excluded is reported");

    // 7. Rescan replaces the library; RefreshFlags reads one game (or all) again.
    const std::string rs = act.Rescan();
    const bool rescan_ok = scans == 1 && games.size() == 2 && rs.find("2 juegos") != std::string::npos;
    games[0].engine_preset_applied = false;
    games[1].engine_preset_applied = false;
    inspect_applied = true;
    act.RefreshFlags(8);
    const bool one = !games[0].engine_preset_applied && games[1].engine_preset_applied;
    act.RefreshFlags(0);
    RecordResult(rescan_ok && one && games[0].engine_preset_applied, "Test 72.7: Rescan replaces the library; RefreshFlags re-reads one game or all");
}

} // namespace vftest
