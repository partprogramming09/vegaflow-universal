// Unit tests: the edits of Configuración and Programas (phase G): a copy is changed, checked and saved, and `config`
// follows only when config.json was written. Driven with a fake file system.

#include "session/config_actions.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

void Test74() {
    // Test 74: a Configuración or Programas edit is saved before the screen follows, and a refused edit changes nothing
    namespace gs = vegaflow::session;
    using vegaflow::ProcessAction;
    using vegaflow::app::AppConfig;
    using vegaflow::app::ConfigStore;
    const std::wstring path = L"C:\\VegaFlow\\config.json";
    FakeFileSystem fs;
    ConfigStore store(fs, path, true);
    AppConfig config = store.Load();
    std::vector<vegaflow::SteamGameEntry> games;
    std::vector<std::string> audit;
    gs::ConfigActions act(config, store, games, [&](const std::string& s) { audit.push_back(s); });
    const uint32_t factory_interval = config.poll_interval_ms;

    // 1. A valid number is saved and shown; the file is what the next start reads.
    const std::string ok = act.SetNumber("poll_interval", "2");
    ConfigStore again(fs, path, true);
    const bool saved = config.poll_interval_ms == 2000 && again.Load().poll_interval_ms == 2000 && ok.find("Guardado") == 0 && audit.size() == 1;
    // 2. Junk is refused with a sentence: nothing written, config as before.
    const size_t writes = fs.writes.size() + fs.replaces.size();
    const std::string bad = act.SetNumber("poll_interval", "abc");
    const std::string unknown = act.SetToggle("no_such_switch", true);
    const bool refused = config.poll_interval_ms == 2000 && fs.writes.size() + fs.replaces.size() == writes && !bad.empty() && unknown == "Ese ajuste no existe.";
    RecordResult(saved && refused, "Test 74.1: a valid number is saved to config.json and shown; junk and an unknown switch change nothing");

    // 3. When the write fails, `config` keeps what the file really holds and the sentence says it was not saved.
    fs.fail_write = true;
    fs.fail_replace = true;
    const std::string failed = act.SetNumber("poll_interval", "3");
    fs.fail_write = false;
    fs.fail_replace = false;
    RecordResult(config.poll_interval_ms == 2000 && failed.find("No se guardó") != std::string::npos && audit.size() == 1,
                 "Test 74.2: a failed write leaves the config as saved and says it was not saved");

    // 4. Programas: category and exception edits go through the same save; an invalid name is refused unsaved.
    const std::string cat = act.SetCategoryAction("music", static_cast<int>(ProcessAction::Throttle));
    const std::string add = act.AddException(" Telegram.exe ");
    const size_t w_before = fs.writes.size() + fs.replaces.size();
    const std::string badname = act.AddException("..\\evil.exe");
    const bool programs = config.process_policy.category_actions.music == ProcessAction::Throttle && config.process_policy.exceptions.size() == 1 &&
                          badname.find("Guardado") != 0 && fs.writes.size() + fs.replaces.size() == w_before && cat.find("Guardado") == 0 && add.find("Guardado") == 0;
    const std::string rem = act.RemoveException("telegram.exe");
    RecordResult(programs && config.process_policy.exceptions.empty() && rem.find("Guardado") == 0,
                 "Test 74.3: Programas edits are saved; an invalid program name is refused with nothing written");

    // 5. Reset returns the Configuración fields to factory values and keeps the games, the exceptions and the UMA flag.
    config.memory.is_uma_architecture = true;
    act.AddException("Discord.exe");
    act.SetNumber("min_free_ram", "2048");
    const std::string reset = act.Reset();
    RecordResult(config.poll_interval_ms == factory_interval && config.memory.min_free_ram_mb != 2048 && config.memory.is_uma_architecture &&
                     config.process_policy.exceptions.size() == 1 && reset.find("restablecida") != std::string::npos,
                 "Test 74.4: Reset restores the Configuración fields and keeps the exceptions and the UMA flag");

    // 6. Manual games: added and removed through the same save, and the game list the daemon reads follows the Steam library.
    vegaflow::SteamGameEntry sg;
    sg.primary_exe_path = L"C:\\Steam\\steamapps\\common\\Octopath\\Octopath.exe";
    games.push_back(sg);
    const std::string game_add = act.AddManualGame("GenshinImpact.exe");
    auto has = [&](const wchar_t* n) {
        for (const auto& e : config.custom_game_exes) {
            if (e == n) return true;
        }
        return false;
    };
    const bool added = game_add.find("Guardado") == 0 && config.user_custom_game_exes.size() == 1 && has(L"GenshinImpact.exe") && has(L"Octopath.exe");
    const std::string game_rem = act.RemoveManualGame("genshinimpact.exe");
    const std::string game_none = act.RemoveManualGame("genshinimpact.exe");
    const std::string exc_add = act.AddException("Slack.exe");
    const std::string exc_set = act.SetExceptionAction("Slack.exe", static_cast<int>(ProcessAction::Throttle));
    const std::string missing = act.SetExceptionAction("Nobody.exe", static_cast<int>(ProcessAction::Throttle));
    RecordResult(added && game_rem.find("Guardado") == 0 && config.user_custom_game_exes.empty() && !has(L"GenshinImpact.exe") && has(L"Octopath.exe") &&
                     game_none == "Ese cambio no existe." && exc_add.find("Guardado") == 0 && exc_set.find("Guardado") == 0 && missing == "Ese cambio no existe.",
                 "Test 74.5: manual games and exception actions are saved, the game list follows the Steam library, and a missing entry is refused");
}

} // namespace vftest
