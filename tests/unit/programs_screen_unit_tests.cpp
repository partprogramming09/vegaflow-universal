// Unit tests: the model of the "Programas" screen (phase G): what VegaFlow does with each kind of program, the
// exceptions for single programs and the games that are not detected on their own. Edits go through AppConfig and
// must survive a save and a reload of config.json.

#include "session/programs_screen.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

namespace {

using vegaflow::ProcessAction;
using vegaflow::app::AppConfig;
using vegaflow::app::ConfigStore;
namespace ps = vegaflow::session;

} // namespace

void Test66() {
    const std::wstring path = L"C:\\VegaFlow\\config.json";
    const AppConfig defaults;

    // 1. The factory config lists the seven kinds of program with the daemon's own defaults, and nothing else.
    const auto m = ps::BuildProgramsScreen(defaults);
    bool order_ok = m.categories.size() == 7;
    const char* ids[] = {"recording_streaming", "calls_voice", "messaging", "browsers", "stores_launchers", "music", "cloud_sync"};
    for (size_t i = 0; order_ok && i < 7; ++i) order_ok = m.categories[i].id == ids[i] && !m.categories[i].title.empty() && !m.categories[i].apps.empty();
    RecordResult(order_ok && m.categories[0].action == ProcessAction::DoNotTouch && m.categories[2].action == ProcessAction::Throttle &&
                     m.categories[5].action == ProcessAction::EcoOnly && m.exceptions.empty() && m.games.empty() &&
                     !m.no_exceptions_text.empty() && !m.no_games_text.empty(),
                 "Test 66.1: the factory config shows seven kinds of program with the daemon's defaults and two empty lists with their text");

    // 2. A kind of program takes one of the three choices; heavy work as a game is not a choice here, and unknown ids are refused.
    AppConfig c = defaults;
    const bool set_ok = ps::SetCategoryAction(c, "music", ProcessAction::Throttle) && c.process_policy.category_actions.music == ProcessAction::Throttle &&
                        ps::SetCategoryAction(c, "browsers", ProcessAction::DoNotTouch) && c.process_policy.category_actions.browsers == ProcessAction::DoNotTouch;
    const AppConfig before = c;
    const bool refused = !ps::SetCategoryAction(c, "no_such_kind", ProcessAction::Throttle) && !ps::SetCategoryAction(c, "music", ProcessAction::BoostAsGame) &&
                         c.process_policy.category_actions.music == before.process_policy.category_actions.music;
    RecordResult(set_ok && refused, "Test 66.2: each kind takes Frenar, Solo ahorro or No tocar; unknown kinds and BoostAsGame are refused");

    // 3. Exceptions: bare .exe names, no duplicates (any letter case), a reason in Spanish when refused, new ones start as No tocar.
    AppConfig e = defaults;
    std::string err;
    const bool added = ps::AddException(e, "  Telegram.exe ", &err) && e.process_policy.exceptions.size() == 1 &&
                       e.process_policy.exceptions[0].action == ProcessAction::DoNotTouch;
    const bool dup = !ps::AddException(e, "TELEGRAM.EXE", &err) && !err.empty() && e.process_policy.exceptions.size() == 1;
    bool bad_names = true;
    for (const std::string& bad : std::vector<std::string>{"", "   ", "telegram", "a\\b.exe", "a/b.exe", "c:b.exe", ".exe", "tele*gram.exe", "x.txt", "explorer.exe", "NUL.exe", "a\tb.exe", std::string(252, 'a') + ".exe"}) {
        std::string why;
        bad_names = bad_names && !ps::AddException(e, bad, &why) && !why.empty() && e.process_policy.exceptions.size() == 1;
    }
    RecordResult(added && dup && bad_names, "Test 66.3: an exception is a bare .exe name, trimmed, without duplicates, refused with a reason otherwise");

    // 4. Change the action of an exception, find it in any letter case, and remove it.
    const bool changed = ps::SetExceptionAction(e, "telegram.exe", ProcessAction::Throttle) && e.process_policy.exceptions[0].action == ProcessAction::Throttle;
    const bool unknown_exc = !ps::SetExceptionAction(e, "other.exe", ProcessAction::Throttle) && !ps::RemoveException(e, "other.exe");
    const bool removed = ps::RemoveException(e, "TELEGRAM.exe") && e.process_policy.exceptions.empty();
    RecordResult(changed && unknown_exc && removed, "Test 66.4: an exception changes its action, is found in any letter case and can be removed");

    // 5. Manual games change only the user's list; the lists the daemon reads are rebuilt from it plus Steam, so a Steam
    //    game with the same name stays detected after the user removes theirs.
    AppConfig g = defaults;
    const bool game_added = ps::AddManualGame(g, "GenshinImpact.exe", &err) && g.user_custom_game_exes.size() == 1;
    const auto gm = ps::BuildProgramsScreen(g);
    const bool shown = gm.games.size() == 1 && gm.games[0] == "GenshinImpact.exe";
    const bool game_dup = !ps::AddManualGame(g, "genshinimpact.exe", &err) && g.user_custom_game_exes.size() == 1;
    vegaflow::SteamGameEntry steam;
    steam.primary_exe_path = L"C:\\Steam\\steamapps\\common\\GenshinImpact\\GenshinImpact.exe";
    vegaflow::app::SyncSteamGamesToCustomExes({steam}, g);
    const bool synced = g.custom_game_exes.size() == 1 && g.process_policy.custom_game_exes.size() == 1;
    const bool game_removed = ps::RemoveManualGame(g, "GENSHINIMPACT.EXE") && g.user_custom_game_exes.empty() && !ps::RemoveManualGame(g, "GenshinImpact.exe");
    vegaflow::app::SyncSteamGamesToCustomExes({steam}, g);
    const bool steam_kept = g.custom_game_exes.size() == 1 && g.process_policy.custom_game_exes.size() == 1;
    AppConfig ex = defaults;
    ex.process_policy.excluded_game_exes = {L"Bad.exe"};
    const bool excluded_refused = !ps::AddManualGame(ex, "bad.exe", &err) && !err.empty() && ex.user_custom_game_exes.empty();
    RecordResult(game_added && shown && game_dup && synced && game_removed && steam_kept && excluded_refused,
                 "Test 66.5: a manual game is shown, not duplicated, refused when excluded, and the Steam copy of the same name stays detected after removing it");

    // 6. Everything round-trips through config.json: categories, exceptions and manual games.
    FakeFileSystem fs;
    ConfigStore store(fs, path, true);
    AppConfig edited = store.Load();
    ps::SetCategoryAction(edited, "cloud_sync", ProcessAction::EcoOnly);
    ps::SetCategoryAction(edited, "calls_voice", ProcessAction::Throttle);
    ps::AddException(edited, "Slack.exe", &err);
    ps::SetExceptionAction(edited, "slack.exe", ProcessAction::EcoOnly);
    ps::AddManualGame(edited, "Game One.exe", &err);
    ps::AddManualGame(edited, "game2.exe", &err);
    std::string why;
    const bool saved = store.Save(edited, false, &why);
    ConfigStore reload_store(fs, path, true);
    const AppConfig back = reload_store.Load();
    const auto bm = ps::BuildProgramsScreen(back);
    RecordResult(saved && back.process_policy.category_actions.cloud_sync == ProcessAction::EcoOnly &&
                     back.process_policy.category_actions.calls_voice == ProcessAction::Throttle && bm.exceptions.size() == 1 &&
                     bm.exceptions[0].exe == "Slack.exe" && bm.exceptions[0].action == ProcessAction::EcoOnly && bm.games.size() == 2 &&
                     bm.games[0] == "Game One.exe" && bm.games[1] == "game2.exe",
                 "Test 66.6: categories, exceptions and manual games survive a save and a reload of config.json", why);

    // 7. A hand-edited Windows process entry is still refused by the save check (the daemon never touches them), file untouched.
    AppConfig win = back;
    win.process_policy.exceptions.push_back({L"explorer.exe", ProcessAction::Throttle});
    const std::string file_before = fs.files[path];
    std::string win_why;
    RecordResult(!reload_store.Save(win, true, &win_why) && !win_why.empty() && fs.files[path] == file_before,
                 "Test 66.7: an exception for a Windows process is refused by the save check with a reason, file untouched");
}

} // namespace vftest
