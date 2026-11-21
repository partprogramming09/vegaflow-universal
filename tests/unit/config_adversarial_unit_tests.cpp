// Adversarial unit tests: the Configuración model (number parsing, rows) and the edits of Configuración and Programas
// (ConfigActions): signs, zeros, overflows, commas and dots, values the Rust rules refuse, writes that fail, a config.json
// edited by hand in between, invalid enum values, hostile program names and lists at their limits.

#include "session/config_actions.hpp"
#include "session/config_screen.hpp"
#include "session/programs_screen.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::ProcessAction;
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

const std::wstring kPath = L"C:\\VegaFlow\\config.json";

struct Rig {
    FakeFileSystem fs;
    ConfigStore store{fs, kPath, true};
    AppConfig config;
    std::vector<vegaflow::SteamGameEntry> games;
    std::vector<std::string> audit;
    gs::ConfigActions act;
    Rig() : act(config, store, games, [this](const std::string& s) { audit.push_back(s); }) { config = store.Load(); }
    size_t Writes() const { return fs.writes.size() + fs.replaces.size(); }
    AppConfig Reload() {
        ConfigStore again(fs, kPath, true);
        return again.Load();
    }
};

// The fields Configuración and Programas can change, as one string to compare before and after.
std::string Fingerprint(const AppConfig& c) {
    std::string s = std::to_string(c.poll_interval_ms) + "|" + std::to_string(c.restrict_game_cpu_sets) + "|" + std::to_string(c.memory.min_free_ram_mb) + "|" +
                    std::to_string(c.memory.min_standby_mb) + "|" + std::to_string(c.memory.cooldown_seconds) + "|" +
                    std::to_string(c.memory.max_game_disk_read_bytes_per_sec) + "|" + std::to_string(c.memory.is_uma_architecture) + "|" +
                    std::to_string(c.process_policy.heavy_work_as_game);
    const auto& a = c.process_policy.category_actions;
    for (const auto v : {a.recording_streaming, a.calls_voice, a.messaging, a.browsers, a.stores_launchers, a.music, a.cloud_sync}) s += "|" + std::to_string(static_cast<int>(v));
    for (const auto& e : c.process_policy.exceptions) s += "|ex:" + std::string(e.exe_name.begin(), e.exe_name.end()) + "=" + std::to_string(static_cast<int>(e.action));
    for (const auto& g : c.user_custom_game_exes) s += "|g:" + std::string(g.begin(), g.end());
    for (const auto& g : c.process_policy.excluded_game_exes) s += "|x:" + std::string(g.begin(), g.end());
    return s;
}

} // namespace

void Test80() {
    // Test 80: attacks on the Configuración model and the edits of Configuración and Programas
    Attack("Test 80.1: poll interval text: accepted forms give the exact milliseconds", [&]() -> std::string {
        const std::vector<std::pair<std::string, uint32_t>> ok = {{"1", 1000},        {"1,5", 1500},      {"1.5", 1500},      {"0,25", 250},        {"0.250", 250},
                                                                  {"60", 60000},      {" 2 ", 2000},      {"\t2\t", 2000},    {"000,5", 500},       {"10,05", 10050},
                                                                  {"0", 0},           {"4294967,295", UINT32_MAX}, {"1,500", 1500}, {"2,0", 2000}};
        for (const auto& [text, ms] : ok) {
            AppConfig c;
            std::string err;
            if (!gs::ApplyConfigNumber(c, "poll_interval", text, &err)) return "refused '" + text + "': " + err;
            if (c.poll_interval_ms != ms) return "'" + text + "' gave " + std::to_string(c.poll_interval_ms) + " ms, expected " + std::to_string(ms);
        }
        return "";
    });

    Attack("Test 80.2: poll interval text: junk, signs, exponents, overflow and non-ASCII digits are refused with a sentence and cfg untouched", [&]() -> std::string {
        const std::vector<std::string> bad = {"",        " ",       ",",        ".",       "1,2,3",       "1.2.3",   "1,5000",     "-1",     "+1",         "1e3",       "0x10",
                                              "１",      "٣",       "1\xC2\xA0", "4294967,296", "99999999999", "1 000", "1,5 s",   "1,-5",   "NaN",        "inf",       "1\n",
                                              "\n1",     "1\r",     "--1",      "1,,5",    "1.,5",        ",,",      "..5",        "abc",    "9999999999999999999999", std::string("1\0" "2", 3),
                                              "4294968", "1000000000", "-0",    "0,-1"};
        for (const auto& text : bad) {
            AppConfig c;
            c.poll_interval_ms = 1234;
            std::string err;
            if (gs::ApplyConfigNumber(c, "poll_interval", text, &err)) return "accepted '" + text + "' as " + std::to_string(c.poll_interval_ms) + " ms";
            if (c.poll_interval_ms != 1234) return "cfg changed by a refused '" + text + "'";
            if (err.empty()) return "refused '" + text + "' with no sentence";
        }
        return "";
    });

    Attack("Test 80.3: whole-number fields: exact values, zero, leading zeros, trimmed blanks, and the 1 000 000 000 ceiling", [&]() -> std::string {
        struct Field {
            const char* id;
            std::function<uint64_t(const AppConfig&)> get;
            uint64_t factor;
        };
        const std::vector<Field> fields = {{"min_free_ram", [](const AppConfig& c) { return c.memory.min_free_ram_mb; }, 1},
                                           {"min_standby", [](const AppConfig& c) { return c.memory.min_standby_mb; }, 1},
                                           {"cooldown", [](const AppConfig& c) { return static_cast<uint64_t>(c.memory.cooldown_seconds); }, 1},
                                           {"max_disk_read", [](const AppConfig& c) { return c.memory.max_game_disk_read_bytes_per_sec; }, 1024ULL * 1024ULL}};
        const std::vector<std::pair<std::string, uint64_t>> ok = {{"0", 0}, {"7", 7}, {"007", 7}, {" 5 ", 5}, {"\t5", 5}, {"0000000000", 0}, {"1000000000", 1000000000ULL}, {"128", 128}};
        const std::vector<std::string> bad = {"1000000001", "99999999999", "-5", "+5", "5.0", "5,0", "1e3", "", " ", "0x1F", "1 2", "٣", "５", "5\n", "5\r\n", "4294967296", "18446744073709551616", "-0"};
        for (const auto& f : fields) {
            for (const auto& [text, v] : ok) {
                AppConfig c;
                std::string err;
                if (!gs::ApplyConfigNumber(c, f.id, text, &err)) return std::string(f.id) + " refused '" + text + "': " + err;
                if (f.get(c) != v * f.factor) return std::string(f.id) + " '" + text + "' gave " + std::to_string(f.get(c));
            }
            for (const auto& text : bad) {
                AppConfig c;
                const std::string before = Fingerprint(c);
                std::string err;
                if (gs::ApplyConfigNumber(c, f.id, text, &err)) return std::string(f.id) + " accepted '" + text + "' as " + std::to_string(f.get(c));
                if (Fingerprint(c) != before) return std::string(f.id) + ": cfg changed by a refused '" + text + "'";
                if (err.empty()) return std::string(f.id) + ": no sentence for '" + text + "'";
            }
        }
        return "";
    });

    Attack("Test 80.4: unknown ids (empty, other case, with spaces, the JSON key names) change nothing and say the setting does not exist", [&]() -> std::string {
        for (const std::string& id : {std::string(""), std::string("POLL_INTERVAL"), std::string("poll_interval "), std::string("poll_interval_ms"), std::string("min_free_ram_mb"),
                                      std::string("cooldown_seconds"), std::string("heavy_work_as_game"), std::string("is_uma")}) {
            AppConfig c;
            const std::string before = Fingerprint(c);
            std::string err;
            if (gs::ApplyConfigNumber(c, id, "5", &err) || err != "Ese ajuste no existe.") return "number id '" + id + "': " + err;
            if (gs::ApplyConfigToggle(c, id == "heavy_work_as_game" ? std::string("Heavy_Work_As_Game") : id, true)) return "toggle id '" + id + "' accepted";
            if (Fingerprint(c) != before) return "cfg changed for id '" + id + "'";
        }
        return "";
    });

    Attack("Test 80.5: property: what the screen shows for any poll interval is parsed back to the same milliseconds", [&]() -> std::string {
        uint64_t x = 0x2545F4914F6CDD1DULL;
        const auto next = [&]() {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        };
        std::vector<uint32_t> values = {0, 1, 9, 10, 99, 100, 999, 1000, 1001, 1010, 1500, 59999, 60000, 60001, 999999999, UINT32_MAX - 1, UINT32_MAX};
        for (int i = 0; i < 20000; ++i) values.push_back(static_cast<uint32_t>(next()));
        for (int i = 0; i < 5000; ++i) values.push_back(static_cast<uint32_t>(next() % 100000));
        for (const uint32_t ms : values) {
            AppConfig c;
            c.poll_interval_ms = ms;
            std::string shown;
            for (const auto& g : gs::BuildConfigScreen(c).groups) {
                for (const auto& r : g.rows) {
                    if (r.id == "poll_interval") shown = r.value;
                }
            }
            AppConfig back;
            std::string err;
            if (shown.empty()) return "no text for " + std::to_string(ms);
            if (!gs::ApplyConfigNumber(back, "poll_interval", shown, &err)) return std::to_string(ms) + " ms shown as '" + shown + "' is refused: " + err;
            if (back.poll_interval_ms != ms) return std::to_string(ms) + " ms shown as '" + shown + "' parses back as " + std::to_string(back.poll_interval_ms);
            if (shown.back() == '0' && shown.find(',') != std::string::npos) return "trailing zero in '" + shown + "'";
        }
        return "";
    });

    Attack("Test 80.6: every row of the screen is well formed: unique ids the editors know, non-empty texts, values that parse back unchanged, no promise of gain", [&]() -> std::string {
        AppConfig c;
        c.memory.min_free_ram_mb = 4096;
        c.memory.cooldown_seconds = 300;
        c.memory.max_game_disk_read_bytes_per_sec = 50ULL * 1024 * 1024;
        c.poll_interval_ms = 2750;
        const auto m = gs::BuildConfigScreen(c);
        std::set<std::string> ids;
        for (const auto& g : m.groups) {
            if (g.title.empty()) return "group with no title";
            if (g.rows.empty()) return "group '" + g.title + "' with no rows";
            if (Promises(g.title + g.note)) return "promise in group " + g.title;
            for (const auto& r : g.rows) {
                if (!ids.insert(r.id).second) return "duplicate id " + r.id;
                if (r.name.empty() || r.help.empty()) return "row " + r.id + " has an empty name or help";
                if (Promises(r.name + " " + r.help + " " + r.chip)) return "promise in row " + r.id + ": " + r.help;
                const AppConfig before = c;
                AppConfig copy = c;
                std::string err;
                if (r.kind == gs::ConfigRowKind::Number) {
                    if (r.value.empty() || r.unit.empty()) return "number row " + r.id + " without value or unit";
                    if (!gs::ApplyConfigNumber(copy, r.id, r.value, &err)) return "number row " + r.id + " value '" + r.value + "' refused: " + err;
                } else if (!gs::ApplyConfigToggle(copy, r.id, r.on)) {
                    return "toggle row " + r.id + " unknown to ApplyConfigToggle";
                }
                if (Fingerprint(copy) != Fingerprint(before)) return "row " + r.id + " does not round trip: " + Fingerprint(before) + " vs " + Fingerprint(copy);
            }
        }
        return ids.size() >= 6 ? "" : "only " + std::to_string(ids.size()) + " rows";
    });

    Attack("Test 80.7: extreme values in the config (UINT32_MAX, UINT64_MAX, 0) still build a screen with every text filled in", [&]() -> std::string {
        AppConfig c;
        c.poll_interval_ms = UINT32_MAX;
        c.memory.min_free_ram_mb = UINT64_MAX;
        c.memory.min_standby_mb = UINT64_MAX;
        c.memory.cooldown_seconds = UINT32_MAX;
        c.memory.max_game_disk_read_bytes_per_sec = UINT64_MAX;
        AppConfig z;
        z.poll_interval_ms = 0;
        z.memory = {};
        z.memory.min_free_ram_mb = 0;
        z.memory.min_standby_mb = 0;
        z.memory.cooldown_seconds = 0;
        z.memory.max_game_disk_read_bytes_per_sec = 0;
        for (const AppConfig* cfg : {&c, &z}) {
            for (const auto& g : gs::BuildConfigScreen(*cfg).groups) {
                for (const auto& r : g.rows) {
                    if (r.kind == gs::ConfigRowKind::Number && r.value.empty()) return "empty value for " + r.id;
                }
            }
        }
        return "";
    });

    // ---- the edits, saved through the fake file system and the Rust rules ----

    Attack("Test 80.8: numbers the Rust rules refuse (and the exact borders they accept) are refused or saved; refused ones change and write nothing", [&]() -> std::string {
        struct Case {
            const char* id;
            const char* text;
            bool saved;
        };
        const std::vector<Case> cases = {
            {"poll_interval", "0", false},     {"poll_interval", "0,249", false},   {"poll_interval", "0,25", true},  {"poll_interval", "60", true},      {"poll_interval", "60,001", false},
            {"min_free_ram", "0", false},      {"min_free_ram", "127", false},      {"min_free_ram", "128", true},    {"min_free_ram", "1048576", true},  {"min_free_ram", "1048577", false},
            {"min_standby", "127", false},     {"min_standby", "128", true},        {"min_standby", "1048576", true}, {"min_standby", "1048577", false},
            {"cooldown", "9", false},          {"cooldown", "10", true},            {"cooldown", "3600", true},       {"cooldown", "3601", false},       {"cooldown", "0", false},
            {"max_disk_read", "0", false},     {"max_disk_read", "1", true},        {"max_disk_read", "100000", true}, {"max_disk_read", "100001", false}, {"max_disk_read", "1000000000", false}};
        for (const auto& c : cases) {
            Rig r;
            const std::string before = Fingerprint(r.config);
            const size_t w = r.Writes();
            const size_t lines = r.audit.size();
            const std::string msg = r.act.SetNumber(c.id, c.text);
            const std::string tag = std::string(c.id) + "='" + c.text + "'";
            if (msg.empty()) return tag + ": empty message";
            if (c.saved) {
                if (msg.rfind("Guardado", 0) != 0) return tag + " should be saved, got '" + msg + "'";
                if (Fingerprint(r.Reload()) != Fingerprint(r.config)) return tag + ": file and screen disagree after saving";
                if (r.audit.size() != lines + 1) return tag + ": audit lines " + std::to_string(r.audit.size() - lines);
            } else {
                if (msg.rfind("Guardado", 0) == 0) return tag + " should be refused, got '" + msg + "'";
                if (Fingerprint(r.config) != before) return tag + ": config changed by a refused edit";
                if (r.Writes() != w) return tag + ": a refused edit wrote config.json";
                if (r.audit.size() != lines) return tag + ": a refused edit was audited as done";
            }
        }
        return "";
    });

    Attack("Test 80.9: a failed write keeps the screen as saved, and the next healthy edit still works", [&]() -> std::string {
        Rig r;
        r.act.SetNumber("cooldown", "100");
        r.fs.fail_replace = true;
        r.fs.fail_write = true;
        const std::string failed = r.act.SetNumber("cooldown", "200");
        const std::string failed_toggle = r.act.SetToggle("restrict_cpu_sets", true);
        const std::string failed_reset = r.act.Reset();
        const std::string failed_prog = r.act.AddException("Foo.exe");
        r.fs.fail_replace = false;
        r.fs.fail_write = false;
        if (r.config.memory.cooldown_seconds != 100 || r.config.restrict_game_cpu_sets || !r.config.process_policy.exceptions.empty()) return "config followed a write that failed";
        for (const auto& m : {failed, failed_toggle, failed_reset, failed_prog}) {
            if (m.find("No se guardó") == std::string::npos) return "message does not say it was not saved: '" + m + "'";
        }
        if (r.act.SetNumber("cooldown", "300").rfind("Guardado", 0) != 0) return "a healthy edit after the failure was refused";
        if (r.Reload().memory.cooldown_seconds != 300) return "file does not hold the healthy edit";
        return "";
    });

    Attack("Test 80.10: config.json edited by hand between two edits: the second edit is refused, config and file are left as they are", [&]() -> std::string {
        Rig r;
        r.act.SetNumber("poll_interval", "2");
        std::string text = r.fs.files[kPath];
        const size_t at = text.find("2000");
        if (at == std::string::npos) return "blind: saved file has no 2000";
        text.replace(at, 4, "5000");
        r.fs.files[kPath] = text;
        const size_t w = r.Writes();
        const std::string msg = r.act.SetNumber("cooldown", "100");
        if (msg.rfind("Guardado", 0) == 0) return "overwrote a config.json edited by hand: " + msg;
        if (r.fs.files[kPath] != text || r.Writes() != w) return "file was written over the hand edit";
        if (r.config.memory.cooldown_seconds == 100) return "config followed a refused edit";
        return "";
    });

    Attack("Test 80.11: category actions: only 0..2 are accepted; -1, 3 (BoostAsGame), 4, 255, 256, INT_MAX, INT_MIN and unknown ids change nothing and write nothing", [&]() -> std::string {
        Rig r;
        const std::string before = Fingerprint(r.config);
        const size_t w = r.Writes();
        for (const int bad : {-1, 3, 4, 255, 256, 257, INT_MAX, INT_MIN}) {
            const std::string msg = r.act.SetCategoryAction("music", bad);
            if (msg.rfind("Guardado", 0) == 0) return "accepted action " + std::to_string(bad) + " (stored " + std::to_string(static_cast<int>(r.config.process_policy.category_actions.music)) + ")";
        }
        for (const std::string& id : {std::string(""), std::string("Music"), std::string("music "), std::string("browsers\0x", 10), std::string("cloud-sync")}) {
            if (r.act.SetCategoryAction(id, 2).rfind("Guardado", 0) == 0) return "accepted category id '" + id + "'";
        }
        if (Fingerprint(r.config) != before || r.Writes() != w) return "a refused category edit changed the config or wrote the file";
        for (const int good : {0, 1, 2}) {
            if (r.act.SetCategoryAction("music", good).rfind("Guardado", 0) != 0) return "refused action " + std::to_string(good);
            if (static_cast<int>(r.Reload().process_policy.category_actions.music) != good) return "file does not hold action " + std::to_string(good);
        }
        return "";
    });

    Attack("Test 80.12: exception actions: invalid values and unknown programs are refused, valid ones are kept per program", [&]() -> std::string {
        Rig r;
        r.act.AddException("Foo.exe");
        r.act.AddException("Bar.exe");
        const std::string before = Fingerprint(r.config);
        const size_t w = r.Writes();
        for (const int bad : {-1, 3, 4, 255, 256, INT_MAX, INT_MIN}) {
            if (r.act.SetExceptionAction("foo.exe", bad).rfind("Guardado", 0) == 0) return "accepted exception action " + std::to_string(bad);
        }
        if (r.act.SetExceptionAction("nope.exe", 2).rfind("Guardado", 0) == 0) return "edited an exception that does not exist";
        if (r.act.SetExceptionAction("", 2).rfind("Guardado", 0) == 0) return "edited an empty exception";
        if (Fingerprint(r.config) != before || r.Writes() != w) return "a refused edit changed the config or wrote the file";
        if (r.act.SetExceptionAction("FOO.EXE", 2).rfind("Guardado", 0) != 0) return "valid edit by another letter case refused";
        const AppConfig back = r.Reload();
        if (back.process_policy.exceptions.size() != 2) return "file holds " + std::to_string(back.process_policy.exceptions.size()) + " exceptions";
        for (const auto& e : back.process_policy.exceptions) {
            const ProcessAction want = e.exe_name == L"Foo.exe" ? ProcessAction::Throttle : ProcessAction::DoNotTouch;
            if (e.action != want) return "action of a program not kept";
        }
        return "";
    });

    Attack("Test 80.13: program names the rules refuse never reach the list, with a sentence and no write (exceptions and manual games)", [&]() -> std::string {
        const std::vector<std::string> bad = {"",
                                              "   ",
                                              "\t",
                                              "C:\\x.exe",
                                              "..\\x.exe",
                                              "a/b.exe",
                                              "explorer.exe",
                                              " EXPLORER.EXE ",
                                              "dwm.exe",
                                              "vegaflow_daemon.exe",
                                              "con.exe",
                                              "aux.EXE",
                                              "com9.exe",
                                              "x.exe.",
                                              "x.txt",
                                              "x",
                                              ".exe",
                                              "a.exe\n",
                                              "\na.exe",
                                              "a\x01.exe",
                                              "a:b.exe",
                                              "a*.exe",
                                              "a?.exe",
                                              "a|b.exe",
                                              "a<b.exe",
                                              "\xff\xfe.exe",
                                              std::string("a\0b.exe", 7),
                                              std::string(252, 'a') + ".exe",
                                              "a.exe:evil"};
        for (const auto& name : bad) {
            Rig r;
            const std::string before = Fingerprint(r.config);
            const size_t w = r.Writes();
            const std::string a = r.act.AddException(name);
            const std::string b = r.act.AddManualGame(name);
            if (a.rfind("Guardado", 0) == 0 || b.rfind("Guardado", 0) == 0) return "accepted '" + name.substr(0, 30) + "'";
            if (a.empty() || b.empty()) return "refused with no sentence: '" + name.substr(0, 30) + "'";
            if (Fingerprint(r.config) != before || r.Writes() != w) return "state changed by '" + name.substr(0, 30) + "'";
        }
        return "";
    });

    Attack("Test 80.14: valid names (blanks around, Unicode, 251 letters, 125 emoji = 254 UTF-16 units) are saved once; duplicates in another case are refused", [&]() -> std::string {
        std::string emoji;
        for (int i = 0; i < 125; ++i) emoji += "\xF0\x9F\x98\x80";
        std::string emoji_too_long;
        for (int i = 0; i < 126; ++i) emoji_too_long += "\xF0\x9F\x98\x80";
        const std::vector<std::string> good = {" Telegram.exe\t", "Ünï código.exe", "日本語.EXE", std::string(251, 'a') + ".exe", emoji + ".exe", "a b.exe"};
        for (const auto& name : good) {
            Rig r;
            if (r.act.AddException(name).rfind("Guardado", 0) != 0) return "AddException refused '" + name.substr(0, 20) + "'";
            if (r.act.AddManualGame(name).rfind("Guardado", 0) != 0) return "AddManualGame refused '" + name.substr(0, 20) + "'";
            if (r.act.AddException(name).rfind("Guardado", 0) == 0 || r.act.AddManualGame(name).rfind("Guardado", 0) == 0) return "duplicate accepted '" + name.substr(0, 20) + "'";
            const AppConfig back = r.Reload();
            if (back.process_policy.exceptions.size() != 1 || back.user_custom_game_exes.size() != 1) return "file holds " + std::to_string(back.process_policy.exceptions.size()) + " exceptions and " + std::to_string(back.user_custom_game_exes.size()) + " games";
        }
        Rig r;
        if (r.act.AddException(emoji_too_long + ".exe").rfind("Guardado", 0) == 0) return "accepted a 256-unit name";
        if (r.act.AddException("Pe.exe").rfind("Guardado", 0) != 0) return "Pe.exe refused";
        if (r.act.AddException("pE.EXE").rfind("Guardado", 0) == 0) return "pE.EXE duplicate of Pe.exe accepted";
        return "";
    });

    Attack("Test 80.21: a duplicate that differs only in the case of a non-ASCII letter is refused for exceptions and manual games", [&]() -> std::string {
        Rig r;
        if (r.act.AddException("Ñ.exe").rfind("Guardado", 0) != 0 || r.act.AddManualGame("Ñ.exe").rfind("Guardado", 0) != 0) return "first add refused";
        if (r.act.AddException("ñ.EXE").rfind("Guardado", 0) == 0) return "exception ñ.EXE accepted next to Ñ.exe";
        if (r.act.AddManualGame("ñ.EXE").rfind("Guardado", 0) == 0) return "manual game ñ.EXE accepted next to Ñ.exe";
        return "";
    });

    Attack("Test 80.15: 1100 exceptions added one by one: the file always agrees with the screen, whatever limit the rules apply", [&]() -> std::string {
        Rig r;
        size_t saved = 0;
        for (int i = 0; i < 1100; ++i) {
            if (r.act.AddException("p" + std::to_string(i) + ".exe").rfind("Guardado", 0) == 0) ++saved;
        }
        if (r.config.process_policy.exceptions.size() != saved) return "screen has " + std::to_string(r.config.process_policy.exceptions.size()) + ", saved " + std::to_string(saved);
        if (r.Reload().process_policy.exceptions.size() != saved) return "file has " + std::to_string(r.Reload().process_policy.exceptions.size()) + ", screen " + std::to_string(saved);
        return "";
    });

    Attack("Test 80.16: manual games stop at 1024: the 1025th is refused, nothing written, and the file keeps 1024", [&]() -> std::string {
        Rig r;
        for (int i = 0; i < 1024; ++i) {
            if (r.act.AddManualGame("m" + std::to_string(i) + ".exe").rfind("Guardado", 0) != 0) return "refused game " + std::to_string(i);
        }
        const size_t w = r.Writes();
        const std::string msg = r.act.AddManualGame("one-too-many.exe");
        if (msg.rfind("Guardado", 0) == 0) return "accepted the 1025th game";
        if (r.Writes() != w || r.config.user_custom_game_exes.size() != 1024) return "state changed by the refused 1025th";
        if (r.Reload().user_custom_game_exes.size() != 1024) return "file does not keep 1024";
        if (r.act.RemoveManualGame("m0.exe").rfind("Guardado", 0) != 0) return "could not remove one at the limit";
        if (r.act.AddManualGame("one-too-many.exe").rfind("Guardado", 0) != 0) return "could not add after removing one";
        return "";
    });

    Attack("Test 80.17: manual games and the Steam library: no duplicates in the daemon's list, planted Windows names never listed, removal keeps the Steam copy", [&]() -> std::string {
        Rig r;
        vegaflow::SteamGameEntry s1, s2, s3;
        s1.primary_exe_path = L"C:\\Steam\\steamapps\\common\\Octopath\\Octopath.exe";
        s2.primary_exe_path = L"C:\\Steam\\steamapps\\common\\Evil\\explorer.exe";
        s3.primary_exe_path = L"C:\\Steam\\steamapps\\common\\Evil\\CON.exe";
        r.games = {s1, s2, s3};
        if (r.act.AddManualGame("OCTOPATH.EXE").rfind("Guardado", 0) != 0) return "refused a manual game that is also in Steam";
        const auto count = [&](const std::wstring& n) {
            return std::count_if(r.config.custom_game_exes.begin(), r.config.custom_game_exes.end(), [&](const std::wstring& x) {
                std::wstring a = x, b = n;
                for (auto& c : a) c = static_cast<wchar_t>(std::towlower(c));
                for (auto& c : b) c = static_cast<wchar_t>(std::towlower(c));
                return a == b;
            });
        };
        if (count(L"octopath.exe") != 1) return "Octopath listed " + std::to_string(count(L"octopath.exe")) + " times";
        if (count(L"explorer.exe") != 0 || count(L"con.exe") != 0) return "a planted Windows name was listed as a game";
        r.act.RemoveManualGame("octopath.exe");
        if (count(L"octopath.exe") != 1) return "removing the manual copy dropped the Steam game";
        if (!r.config.user_custom_game_exes.empty()) return "manual list not empty";
        return "";
    });

    Attack("Test 80.18: a manual game that is in the excluded list is refused; removing one that is not there is refused without a write", [&]() -> std::string {
        Rig r;
        r.config.process_policy.excluded_game_exes = {L"Slow.exe"};
        if (r.act.AddManualGame("slow.exe").rfind("Guardado", 0) == 0) return "added an excluded game";
        const size_t w = r.Writes();
        if (r.act.RemoveManualGame("ghost.exe").rfind("Guardado", 0) == 0 || r.act.RemoveManualGame("").rfind("Guardado", 0) == 0) return "removed something that was not there";
        if (r.act.RemoveException("ghost.exe").rfind("Guardado", 0) == 0 || r.act.RemoveException("").rfind("Guardado", 0) == 0) return "removed an exception that was not there";
        return r.Writes() == w ? "" : "refused removals wrote the file";
    });

    Attack("Test 80.19: Reset restores only the Configuración fields and keeps categories, exceptions, games, the excluded list and the UMA flag; twice gives the same", [&]() -> std::string {
        Rig r;
        r.act.SetNumber("poll_interval", "9");
        r.act.SetNumber("min_free_ram", "4000");
        r.act.SetNumber("min_standby", "4000");
        r.act.SetNumber("cooldown", "500");
        r.act.SetNumber("max_disk_read", "77");
        r.act.SetToggle("restrict_cpu_sets", true);
        r.act.SetToggle("heavy_work_as_game", true);
        r.act.SetCategoryAction("music", 2);
        r.act.AddException("Keep.exe");
        r.act.AddManualGame("Mine.exe");
        r.config.process_policy.excluded_game_exes = {L"Slow.exe"};
        r.config.memory.is_uma_architecture = false;
        const AppConfig factory;
        const std::string kept_before = Fingerprint(r.config);
        const std::string msg = r.act.Reset();
        if (msg.rfind("Guardado", 0) == 0 || msg.find("restablecida") == std::string::npos) return "message '" + msg + "'";
        const AppConfig& c = r.config;
        if (c.poll_interval_ms != factory.poll_interval_ms || c.memory.min_free_ram_mb != factory.memory.min_free_ram_mb || c.memory.min_standby_mb != factory.memory.min_standby_mb ||
            c.memory.cooldown_seconds != factory.memory.cooldown_seconds || c.memory.max_game_disk_read_bytes_per_sec != factory.memory.max_game_disk_read_bytes_per_sec ||
            c.restrict_game_cpu_sets != factory.restrict_game_cpu_sets || c.process_policy.heavy_work_as_game != factory.process_policy.heavy_work_as_game) {
            return "a Configuración field was not reset: " + Fingerprint(c);
        }
        if (c.memory.is_uma_architecture) return "UMA flag was reset";
        if (c.process_policy.category_actions.music != ProcessAction::Throttle || c.process_policy.exceptions.size() != 1 || c.user_custom_game_exes.size() != 1 ||
            c.process_policy.excluded_game_exes.size() != 1) {
            return "categories, exceptions or games were reset: " + Fingerprint(c) + " (before " + kept_before + ")";
        }
        const std::string once = Fingerprint(r.config);
        r.act.Reset();
        if (Fingerprint(r.config) != once) return "second Reset differs";
        AppConfig back = r.Reload();
        back.memory.is_uma_architecture = r.config.memory.is_uma_architecture; // the flag is not stored in config.json
        if (Fingerprint(back) != once) return "file differs from the screen after Reset";
        return "";
    });

    Attack("Test 80.20: the same edit twice writes the same file and keeps one audit line per saved edit", [&]() -> std::string {
        Rig r;
        r.act.SetToggle("heavy_work_as_game", true);
        const std::string first = r.fs.files[kPath];
        r.act.SetToggle("heavy_work_as_game", true);
        if (r.fs.files[kPath] != first) return "second identical edit changed the file";
        if (r.audit.size() != 2) return "audit lines " + std::to_string(r.audit.size());
        r.act.SetToggle("heavy_work_as_game", false);
        r.act.SetToggle("restrict_cpu_sets", false);
        if (r.config.process_policy.heavy_work_as_game || r.config.restrict_game_cpu_sets) return "toggles not off";
        return "";
    });
}

} // namespace vftest
