// Unit tests: the model of the "Configuración" screen (phase G): rows read from AppConfig, numbers parsed from what
// the person types, and an edit that survives a save and a reload of config.json.

#include "session/config_screen.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

namespace {

using vegaflow::app::AppConfig;
using vegaflow::app::ConfigStore;
namespace cs = vegaflow::session;

const cs::ConfigRow* Find(const cs::ConfigScreenModel& m, const std::string& id) {
    for (const auto& g : m.groups) {
        for (const auto& r : g.rows) {
            if (r.id == id) return &r;
        }
    }
    return nullptr;
}

} // namespace

void Test65() {
    const std::wstring path = L"C:\\VegaFlow\\config.json";
    const AppConfig defaults;

    // 1. The factory config shows every setting with its value in the unit the person reads.
    const auto m = cs::BuildConfigScreen(defaults);
    const auto* poll = Find(m, "poll_interval");
    const auto* free_ram = Find(m, "min_free_ram");
    const auto* standby = Find(m, "min_standby");
    const auto* cooldown = Find(m, "cooldown");
    const auto* disk = Find(m, "max_disk_read");
    const auto* restrict_sets = Find(m, "restrict_cpu_sets");
    const auto* heavy = Find(m, "heavy_work_as_game");
    RecordResult(poll && free_ram && standby && cooldown && disk && restrict_sets && heavy && poll->value == "1,5" &&
                     free_ram->value == "1200" && standby->value == "1500" && cooldown->value == "90" && disk->value == "15" &&
                     !restrict_sets->on && !heavy->on,
                 "Test 65.1: the factory config shows 1,5 s, 1200 MB, 1500 MB, 90 s, 15 MB/s and both switches off");

    // 2. Typed numbers: seconds accept a comma or a dot; plain integers elsewhere; junk is refused with a sentence.
    AppConfig c = defaults;
    std::string err;
    const bool a = cs::ApplyConfigNumber(c, "poll_interval", "2", &err) && c.poll_interval_ms == 2000;
    const bool b = cs::ApplyConfigNumber(c, "poll_interval", "0,8", &err) && c.poll_interval_ms == 800;
    const bool d = cs::ApplyConfigNumber(c, "poll_interval", "1.25", &err) && c.poll_interval_ms == 1250;
    const bool e = cs::ApplyConfigNumber(c, "min_free_ram", " 2048 ", &err) && c.memory.min_free_ram_mb == 2048;
    const bool f = cs::ApplyConfigNumber(c, "max_disk_read", "30", &err) && c.memory.max_game_disk_read_bytes_per_sec == 30ULL * 1024 * 1024;
    RecordResult(a && b && d && e && f, "Test 65.2: seconds accept comma or dot, integers are trimmed, MB/s becomes bytes per second");

    AppConfig before = c;
    bool junk_refused = true;
    for (const char* bad : {"", "abc", "-3", "1,5,2", "12x", "99999999999999999999999", "1e3"}) {
        std::string why;
        junk_refused = junk_refused && !cs::ApplyConfigNumber(c, "min_standby", bad, &why) && !why.empty();
    }
    RecordResult(junk_refused && c.memory.min_standby_mb == before.memory.min_standby_mb,
                 "Test 65.3: empty, text, negative, repeated separators and overflow are refused with a reason and change nothing");

    // 3. Unknown ids and toggles.
    std::string why;
    AppConfig t = defaults;
    const bool unknown_refused = !cs::ApplyConfigNumber(t, "nope", "1", &why) && !cs::ApplyConfigToggle(t, "nope", true);
    const bool toggles = cs::ApplyConfigToggle(t, "restrict_cpu_sets", true) && t.restrict_game_cpu_sets &&
                         cs::ApplyConfigToggle(t, "heavy_work_as_game", true) && t.process_policy.heavy_work_as_game;
    RecordResult(unknown_refused && toggles, "Test 65.4: unknown ids are refused and the two switches write their own field");

    // 4. Round trip: edit everything, save through ConfigStore, reload from the same file: same values.
    FakeFileSystem fs;
    ConfigStore store(fs, path, true);
    AppConfig edited = store.Load();
    cs::ApplyConfigNumber(edited, "poll_interval", "2,5", &why);
    cs::ApplyConfigNumber(edited, "min_free_ram", "1800", &why);
    cs::ApplyConfigNumber(edited, "min_standby", "2200", &why);
    cs::ApplyConfigNumber(edited, "cooldown", "120", &why);
    cs::ApplyConfigNumber(edited, "max_disk_read", "25", &why);
    cs::ApplyConfigToggle(edited, "restrict_cpu_sets", true);
    cs::ApplyConfigToggle(edited, "heavy_work_as_game", true);
    const bool saved = store.Save(edited, false, &why);
    const AppConfig reloaded = ConfigStore(fs, path, true).Load();
    RecordResult(saved && reloaded.poll_interval_ms == 2500 && reloaded.memory.min_free_ram_mb == 1800 &&
                     reloaded.memory.min_standby_mb == 2200 && reloaded.memory.cooldown_seconds == 120 &&
                     reloaded.memory.max_game_disk_read_bytes_per_sec == 25ULL * 1024 * 1024 && reloaded.restrict_game_cpu_sets &&
                     reloaded.process_policy.heavy_work_as_game,
                 "Test 65.5: every Configuración value survives a save and a reload of config.json", why);

    // 5. A value the daemon's rules refuse parses fine but the save check says no, with the file untouched.
    AppConfig extreme = reloaded;
    cs::ApplyConfigNumber(extreme, "poll_interval", "0,001", &why);
    std::string refused_why;
    const std::string file_before = fs.files[path];
    const bool refused = !store.Save(extreme, true, &refused_why) && !refused_why.empty() && fs.files[path] == file_before;
    RecordResult(refused, "Test 65.6: a value outside the daemon's rules is refused by the save check with a reason, file untouched");

    // 6. Edges of the seconds field: junk is refused without touching the value, the 250 ms floor is the last accepted
    //    and 249 ms the first refused by the real save, and what the screen shows parses back to the same value.
    bool junk_ok = true;
    for (const char* junk : {".", ",", "1,,5", "1,2345", "5000000", "abc", ""}) {
        AppConfig c;
        const auto before = c.poll_interval_ms;
        std::string e;
        junk_ok = junk_ok && !cs::ApplyConfigNumber(c, "poll_interval", junk, &e) && c.poll_interval_ms == before;
    }
    AppConfig floor_ok;
    AppConfig floor_bad;
    std::string e1, e2, w1, w2;
    const bool floor_accepted = cs::ApplyConfigNumber(floor_ok, "poll_interval", "0,25", &e1) && store.Save(floor_ok, true, &w1);
    const bool floor_refused = cs::ApplyConfigNumber(floor_bad, "poll_interval", "0,249", &e2) && !store.Save(floor_bad, true, &w2);
    bool round_trip = true;
    for (unsigned ms : {250u, 1001u, 1050u, 1500u, 60000u}) {
        AppConfig c;
        c.poll_interval_ms = ms;
        const auto* row = Find(cs::BuildConfigScreen(c), "poll_interval");
        AppConfig d;
        std::string e;
        round_trip = round_trip && row && cs::ApplyConfigNumber(d, "poll_interval", row->value, &e) && d.poll_interval_ms == ms;
    }
    RecordResult(junk_ok, "Test 65.7a: seconds field refuses junk and leaves the value alone");
    RecordResult(floor_accepted && floor_refused, "Test 65.7b: the save accepts 250 ms and refuses 249 ms");
    RecordResult(round_trip, "Test 65.7c: what the screen shows for the seconds field parses back to the same value");
}

} // namespace vftest
