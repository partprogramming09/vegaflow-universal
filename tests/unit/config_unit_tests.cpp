// Unit tests: reading and saving config.json (phase 1b) through a fake file system, and the shared value vectors.
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

#include "bridge/rust_bridge.h"
#include "session/config.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

const std::wstring kPath = L"C:\\VegaFlow\\config\\config.json";

std::string Pass(bool b) { return b ? "PASS" : "FAIL"; }

} // namespace

void Test44() {
    // -------------------------------------------------------------------------
    // Test 44: config.json keeps what it does not know and refuses values that break the daemon
    // -------------------------------------------------------------------------
    using vegaflow::app::AppConfig;
    using vegaflow::app::ConfigStore;

    // Saving keeps a key the person wrote by hand, replaces the file whole and moves it to version 1.
    FakeFileSystem fs;
    fs.files[kPath] = "{\n  \"my_note\": \"keep me\",\n  \"poll_interval_ms\": 2000\n}\n";
    ConfigStore store(fs, kPath, true);
    AppConfig cfg = store.Load();
    cfg.timer_forced_manual = true;
    const bool saved = store.Save(cfg);
    const std::string& text = fs.files[kPath];
    const bool unknown_kept = saved && fs.replaces.size() == 1 && fs.writes.empty() &&
                              text.find("\"my_note\": \"keep me\"") != std::string::npos &&
                              text.find("\"version\": 1") != std::string::npos &&
                              text.find("\"timer_forced_manual\": true") != std::string::npos &&
                              ConfigStore(fs, kPath, true).Load().poll_interval_ms == 2000;

    // A value that is not a number does not borrow the digits of the next key; 0 is refused for the poll and the pause.
    FakeFileSystem bad;
    bad.files[kPath] = "{\"version\": 1, \"poll_interval_ms\": \"fast\", \"memory\": {\"cooldown_seconds\": 0, "
                       "\"min_free_ram_mb\": 900}}";
    ConfigStore bad_store(bad, kPath, true);
    const AppConfig bad_cfg = bad_store.Load();
    const bool bad_values_refused = bad_cfg.poll_interval_ms == 1500 && bad_cfg.memory.cooldown_seconds == 90 &&
                                    bad_cfg.memory.min_free_ram_mb == 900 && !bad_store.Report().CanSave() &&
                                    !bad_store.Report().Warning().empty();
    const bool bad_not_overwritten = !bad_store.Save(bad_cfg) && bad.Changes() == 0;

    // A Windows process cannot be added as a game; the other names stay.
    FakeFileSystem games;
    games.files[kPath] = "{\"version\": 1, \"custom_game_exes\": [\"explorer.exe\", \"MyGame.exe\"]}";
    const AppConfig games_cfg = ConfigStore(games, kPath, true).Load();
    const bool windows_process_refused =
        games_cfg.user_custom_game_exes == std::vector<std::wstring>{L"MyGame.exe"} &&
        games_cfg.custom_game_exes == games_cfg.user_custom_game_exes;

    // A corrupt file: factory values, a warning, and it is never overwritten.
    FakeFileSystem corrupt;
    corrupt.files[kPath] = "{\"poll_interval_ms\": 2000,";
    ConfigStore corrupt_store(corrupt, kPath, true);
    const AppConfig corrupt_cfg = corrupt_store.Load();
    const bool corrupt_kept = corrupt_cfg.poll_interval_ms == 1500 && !corrupt_store.Report().Warning().empty() &&
                              !corrupt_store.Save(corrupt_cfg) && corrupt.Changes() == 0;

    // No file or an empty one: factory values, no warning, and saving creates it.
    FakeFileSystem missing;
    ConfigStore missing_store(missing, kPath, true);
    const AppConfig missing_cfg = missing_store.Load();
    FakeFileSystem empty;
    empty.files[kPath] = "";
    ConfigStore empty_store(empty, kPath, true);
    const bool empty_ok = missing_store.Report().Warning().empty() && missing_store.Save(missing_cfg) &&
                          missing.files.count(kPath) == 1 && empty_store.Load().poll_interval_ms == 1500 &&
                          empty_store.Report().Warning().empty() && empty_store.Save(missing_cfg);

    // A file that exists but cannot be read is never overwritten.
    FakeFileSystem unreadable;
    unreadable.unreadable.insert(kPath);
    ConfigStore unreadable_store(unreadable, kPath, true);
    const AppConfig unreadable_cfg = unreadable_store.Load();
    const bool unreadable_kept = !unreadable_store.Report().Warning().empty() &&
                                 !unreadable_store.Save(unreadable_cfg) && unreadable.Changes() == 0;

    // A folder that accounts without administrator rights can write: factory values, a warning, nothing saved.
    FakeFileSystem untrusted;
    untrusted.files[kPath] = "{\"version\": 1, \"poll_interval_ms\": 2000}";
    ConfigStore untrusted_store(untrusted, kPath, false);
    const AppConfig untrusted_cfg = untrusted_store.Load();
    const bool untrusted_ignored = untrusted_cfg.poll_interval_ms == 1500 &&
                                   !untrusted_store.Report().Warning().empty() &&
                                   !untrusted_store.Save(untrusted_cfg) && untrusted.Changes() == 0;

    // A file from a newer version is read but never overwritten.
    FakeFileSystem newer;
    newer.files[kPath] = "{\"version\": 2, \"poll_interval_ms\": 2000}";
    ConfigStore newer_store(newer, kPath, true);
    const AppConfig newer_cfg = newer_store.Load();
    const bool newer_kept = newer_cfg.poll_interval_ms == 2000 && !newer_store.Report().Warning().empty() &&
                            !newer_store.Save(newer_cfg) && newer.Changes() == 0;

    // A dry run computes the new file but writes nothing.
    FakeFileSystem dry;
    dry.files[kPath] = "{\"version\": 1}";
    ConfigStore dry_store(dry, kPath, true);
    const bool dry_ok = dry_store.Save(dry_store.Load(), true) && dry.Changes() == 0;

    // A file broken by hand after Load is not overwritten, and the reason is given.
    FakeFileSystem later;
    later.files[kPath] = "{\"version\": 1}";
    ConfigStore later_store(later, kPath, true);
    const AppConfig later_cfg = later_store.Load();
    later.files[kPath] = "{broken";
    std::string why;
    const bool later_kept = !later_store.Save(later_cfg, false, &why) && !why.empty() && later.Changes() == 0;

    // A known key changed by hand after Load is not overwritten with the value VegaFlow has in memory.
    FakeFileSystem edited;
    edited.files[kPath] = "{\"version\": 1, \"poll_interval_ms\": 2000}";
    ConfigStore edited_store(edited, kPath, true);
    AppConfig edited_cfg = edited_store.Load();
    edited.files[kPath] = "{\"version\": 1, \"poll_interval_ms\": 3000}";
    std::string edited_why;
    edited_cfg.timer_forced_manual = true;
    const bool hand_edit_kept = !edited_store.Save(edited_cfg, false, &edited_why) && !edited_why.empty() &&
                                edited.Changes() == 0;
    // After VegaFlow's own save, the next save is allowed (its own write is not a change by hand).
    FakeFileSystem twice;
    ConfigStore twice_store(twice, kPath, true);
    AppConfig twice_cfg = twice_store.Load();
    const bool first = twice_store.Save(twice_cfg);
    twice_cfg.timer_forced_manual = true;
    const bool saves_twice = first && twice_store.Save(twice_cfg) && twice.replaces.size() == 2;

    // A failed write gives a reason.
    FakeFileSystem failing;
    failing.fail_replace = true;
    ConfigStore failing_store(failing, kPath, true);
    std::string failing_why;
    const bool failed_write_explained = !failing_store.Save(failing_store.Load(), false, &failing_why) &&
                                        !failing_why.empty();

    // A program name from the Steam library goes through the same rule as the names in config.json.
    AppConfig steam_cfg;
    vegaflow::SteamGameEntry real_game, planted;
    real_game.primary_exe_path = L"D:\\SteamLibrary\\steamapps\\common\\Game\\Game.exe";
    planted.primary_exe_path = L"D:\\SteamLibrary\\steamapps\\common\\Other\\lsass.exe";
    vegaflow::app::SyncSteamGamesToCustomExes({real_game, planted}, steam_cfg);
    const bool steam_filtered = steam_cfg.custom_game_exes == std::vector<std::wstring>{L"Game.exe"};

    RecordResult(hand_edit_kept && saves_twice && failed_write_explained && later_kept && steam_filtered && unknown_kept && bad_values_refused && bad_not_overwritten && windows_process_refused &&
                     corrupt_kept && empty_ok && unreadable_kept && untrusted_ignored && newer_kept && dry_ok,
                 "Test 44: config.json keeps what it does not know and refuses values that break the daemon",
                 "unknown key kept, replaced whole, version 1: " + Pass(unknown_kept) +
                     ", bad values refused per key with a warning: " + Pass(bad_values_refused) +
                     ", file with bad values not overwritten: " + Pass(bad_not_overwritten) +
                     ", Windows process refused as a game: " + Pass(windows_process_refused) +
                     ", corrupt file kept: " + Pass(corrupt_kept) + ", missing or empty file: " + Pass(empty_ok) +
                     ", unreadable file kept: " + Pass(unreadable_kept) +
                     ", untrusted folder ignored: " + Pass(untrusted_ignored) +
                     ", newer version kept: " + Pass(newer_kept) + ", dry run writes nothing: " + Pass(dry_ok) +
                     ", file broken after load kept, with a reason: " + Pass(later_kept) +
                     ", Windows process from Steam refused: " + Pass(steam_filtered) +
                     ", known key edited by hand after load kept: " + Pass(hand_edit_kept) +
                     ", second save after its own: " + Pass(saves_twice) +
                     ", failed write explained: " + Pass(failed_write_explained));
}

void Test45() {
    // -------------------------------------------------------------------------
    // Test 45: config.json values match the shared vector file (same vectors as the Rust core)
    // -------------------------------------------------------------------------
    int checked = 0;
    std::string failures;
    ForEachTsvRow("tests/vectors/config_rules.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
        if (col.size() != 4) {
            failures += " [malformed: " + line + "]";
            return;
        }
        const std::string& key = col[1];
        std::string file;
        if (key == "game_exe") {
            file = "{\"version\": 1, \"custom_game_exes\": [" + col[2] + "]}";
        } else if (key == "min_free_ram_mb" || key == "min_standby_mb" || key == "cooldown_seconds" ||
                   key == "max_game_disk_read_mb_s") {
            file = "{\"version\": 1, \"memory\": {\"" + key + "\": " + col[2] + "}}";
        } else {
            file = "{\"version\": 1, \"" + key + "\": " + col[2] + "}";
        }
        VfConfigLoad out{};
        const bool ok = vf_rust_config_load(file.data(), file.size(), &out, nullptr, nullptr);
        const bool accepted = ok && (out.status & VF_CONFIG_CORRUPT) == 0 && out.rejected_keys == 0;
        if (accepted != (col[3] == "accepted")) failures += " [" + col[0] + "]";
        ++checked;
    });
    RecordResult(checked >= 69 && failures.empty(),
                 "Test 45: config.json values match the shared vector file (same vectors as the Rust core)",
                 std::to_string(checked) + " vectors" + (failures.empty() ? ", 0 differences" : "; differences:" + failures));
}

} // namespace vftest
