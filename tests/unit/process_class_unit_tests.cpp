// Unit tests for Phase 5: process classification TSV vectors, PID+creation_time safety, game switch restoration,
// EcoOnly vs Throttle differentiation, DoNotTouch/unthrottled isolation, and ExcludeGameAndRevert.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "detect/process_detector.hpp"
#include "model/game.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "ports/clock.hpp"
#include "ports/file_system.hpp"
#include "ports/process_control.hpp"
#include "session/config.hpp"
#include "session/game_session.hpp"
#include "session/text_convert.hpp"
#include "windows/change_journal.hpp"

#include "../test_support.hpp"

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace vftest {

namespace {

std::vector<std::string> SplitCsv(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

vegaflow::ProcessAction ParseActionName(const std::string& s) {
    if (s == "do_not_touch") return vegaflow::ProcessAction::DoNotTouch;
    if (s == "eco_only") return vegaflow::ProcessAction::EcoOnly;
    if (s == "throttle") return vegaflow::ProcessAction::Throttle;
    if (s == "boost_as_game") return vegaflow::ProcessAction::BoostAsGame;
    return vegaflow::ProcessAction::DoNotTouch;
}

uint8_t ParseClassName(const std::string& s) {
    if (s == "unknown") return VF_PROC_CLASS_UNKNOWN;
    if (s == "windows_system") return VF_PROC_CLASS_WINDOWS_SYSTEM;
    if (s == "recording_streaming") return VF_PROC_CLASS_RECORDING_STREAMING;
    if (s == "calls_voice") return VF_PROC_CLASS_CALLS_VOICE;
    if (s == "messaging") return VF_PROC_CLASS_MESSAGING;
    if (s == "browsers") return VF_PROC_CLASS_BROWSERS;
    if (s == "stores_launchers") return VF_PROC_CLASS_STORES_LAUNCHERS;
    if (s == "music") return VF_PROC_CLASS_MUSIC;
    if (s == "cloud_sync") return VF_PROC_CLASS_CLOUD_SYNC;
    if (s == "developer_ide") return VF_PROC_CLASS_DEVELOPER_IDE;
    if (s == "heavy_work") return VF_PROC_CLASS_HEAVY_WORK;
    if (s == "possible_game") return VF_PROC_CLASS_POSSIBLE_GAME;
    return 255;
}

std::wstring ExtractFileName(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
}

struct FakeProc {
    uint64_t creation_time = 0;
    bool alive = true;
    uint32_t priority_class = NORMAL_PRIORITY_CLASS;
    bool ecoqos = false;
    bool mem_deprioritized = false;
    bool io_deprioritized = false;
    std::vector<uint32_t> cpu_sets;
    uint64_t cpu_100ns = 0;
    int boost_calls = 0;
    int restore_game_calls = 0;
    int bg_apply_calls = 0;
    int bg_restore_calls = 0;
    int eco_restore_only_calls = 0;
    int full_throttle_restore_calls = 0;
    int recycled_wrong_touch_calls = 0;
};

class FakeProcessControl final : public vegaflow::ProcessControl {
public:
    uint32_t self_pid = 9999;
    uint64_t tick_ms = 1000;
    std::map<uint32_t, FakeProc> procs;

    uint32_t CurrentPid() const override { return self_pid; }
    uint64_t TickCountMs() const override { return tick_ms; }

    uint64_t QueryCreationTime(uint32_t pid) const override {
        auto it = procs.find(pid);
        return (it != procs.end() && it->second.alive) ? it->second.creation_time : 0;
    }

    bool QueryCpuTime100ns(uint32_t pid, uint64_t expected_creation_time, uint64_t& out_total_100ns) const override {
        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) return false;
        out_total_100ns = it->second.cpu_100ns;
        return true;
    }

    bool VerifyProcessIdentity(uint32_t pid, uint64_t expected_creation_time) const override {
        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) return false;
        return true;
    }

    bool BoostGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        bool is_dx12_or_vulkan,
        const std::vector<uint32_t>& game_cpu_sets,
        vegaflow::GameBoostOutcome& out,
        uint32_t& out_orig_priority_class,
        bool dry_run) override {

        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) {
            it->second.recycled_wrong_touch_calls++;
            return false;
        }
        out.full_access = true;
        out_orig_priority_class = it->second.priority_class;
        if (!dry_run) {
            it->second.boost_calls++;
            it->second.priority_class = is_dx12_or_vulkan ? ABOVE_NORMAL_PRIORITY_CLASS : HIGH_PRIORITY_CLASS;
            it->second.cpu_sets = game_cpu_sets;
        }
        out.power_throttling_disabled = true;
        out.memory_priority_elevated = true;
        out.io_priority_elevated = true;
        out.cpu_sets_applied = !game_cpu_sets.empty();
        return true;
    }

    bool BoostPresenterThread(uint32_t, bool) override { return true; }

    bool RestoreGameProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        uint32_t orig_priority_class) override {

        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) {
            it->second.recycled_wrong_touch_calls++;
            return false;
        }
        it->second.restore_game_calls++;
        it->second.priority_class = orig_priority_class;
        it->second.cpu_sets.clear();
        return true;
    }

    vegaflow::BackgroundApplyOutcome ApplyBackgroundPolicy(
        uint32_t pid,
        uint64_t expected_creation_time,
        vegaflow::ProcessAction action,
        const std::vector<uint32_t>& bg_cpu_sets,
        bool dry_run) override {

        vegaflow::BackgroundApplyOutcome out{};
        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return out;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) {
            it->second.recycled_wrong_touch_calls++;
            return out;
        }
        out.modified = true;
        out.power_throttling_applied = true;
        if (!dry_run) {
            it->second.bg_apply_calls++;
            it->second.ecoqos = true;
        }
        if (action == vegaflow::ProcessAction::Throttle) {
            out.memory_deprioritized = true;
            out.io_deprioritized = true;
            out.cpu_sets_applied = !bg_cpu_sets.empty();
            if (!dry_run) {
                it->second.mem_deprioritized = true;
                it->second.io_deprioritized = true;
                it->second.cpu_sets = bg_cpu_sets;
            }
        }
        return out;
    }

    bool UpdateBackgroundCpuSets(
        uint32_t pid,
        uint64_t expected_creation_time,
        const std::vector<uint32_t>& bg_cpu_sets) override {

        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) {
            it->second.recycled_wrong_touch_calls++;
            return false;
        }
        it->second.cpu_sets = bg_cpu_sets;
        return true;
    }

    bool RestoreBackgroundProcess(
        uint32_t pid,
        uint64_t expected_creation_time,
        vegaflow::ProcessAction applied_action) override {

        auto it = procs.find(pid);
        if (it == procs.end() || !it->second.alive) return false;
        if (expected_creation_time != 0 && it->second.creation_time != expected_creation_time) {
            it->second.recycled_wrong_touch_calls++;
            return false;
        }
        it->second.bg_restore_calls++;
        it->second.ecoqos = false;
        if (applied_action == vegaflow::ProcessAction::EcoOnly) {
            it->second.eco_restore_only_calls++;
        } else if (applied_action == vegaflow::ProcessAction::Throttle) {
            it->second.full_throttle_restore_calls++;
            it->second.mem_deprioritized = false;
            it->second.io_deprioritized = false;
            it->second.cpu_sets.clear();
        }
        return true;
    }
};

class InMemoryFileSystem final : public vegaflow::FileSystem {
public:
    std::map<std::wstring, std::string> files;

    bool Read(const std::wstring& path, std::string& out_content) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out_content = it->second;
        return true;
    }
    bool Write(const std::wstring& path, const std::string& content, bool) override {
        files[path] = content;
        return true;
    }
    bool Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, bool) override {
        auto it = files.find(src);
        if (it == files.end()) return false;
        if (!overwrite && files.count(dst)) return false;
        files[dst] = it->second;
        return true;
    }
    bool Remove(const std::wstring& path, bool) override {
        return files.erase(path) > 0;
    }
    bool Replace(const std::wstring& path, const std::string& content) override {
        files[path] = content;
        return true;
    }
    bool Exists(const std::wstring& path) override {
        return files.find(path) != files.end();
    }
    bool CanModifyGameDir(const std::filesystem::path&) override { return true; }
    bool HasAntiCheatSignature(const std::wstring&) override { return false; }
    bool CanWrite(const std::wstring&) override { return true; }
    void AllowReading(const std::wstring&) override {}
};

} // namespace

void Test54() {
    // Shared process_class.tsv vectors evaluated through C++ -> Rust FFI.
    int checked = 0;
    std::string failures;

    ForEachTsvRow("tests/vectors/process_class.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
        if (col.size() != 13) {
            failures += " [malformed: " + line + "]";
            return;
        }
        const std::string& id = col[0];
        const std::wstring full_path = vegaflow::app::Utf8ToWide(col[1]);
        const std::wstring exe_name = ExtractFileName(full_path);

        vegaflow::ProcessPolicyConfig policy{};
        if (col[2] != "default") {
            for (const auto& entry : SplitCsv(col[2], ',')) {
                auto eq = entry.find('=');
                if (eq == std::string::npos) continue;
                const std::string k = entry.substr(0, eq);
                const auto act = ParseActionName(entry.substr(eq + 1));
                if (k == "recording_streaming") policy.category_actions.recording_streaming = act;
                else if (k == "calls_voice") policy.category_actions.calls_voice = act;
                else if (k == "messaging") policy.category_actions.messaging = act;
                else if (k == "browsers") policy.category_actions.browsers = act;
                else if (k == "stores_launchers") policy.category_actions.stores_launchers = act;
                else if (k == "music") policy.category_actions.music = act;
                else if (k == "cloud_sync") policy.category_actions.cloud_sync = act;
            }
        }
        if (col[3] != "none") {
            for (const auto& entry : SplitCsv(col[3], ',')) {
                auto eq = entry.find('=');
                if (eq == std::string::npos) continue;
                policy.exceptions.push_back({
                    vegaflow::app::Utf8ToWide(entry.substr(0, eq)),
                    ParseActionName(entry.substr(eq + 1)),
                });
            }
        }
        if (col[4] != "none") {
            for (const auto& g : SplitCsv(col[4], ',')) {
                policy.custom_game_exes.push_back(vegaflow::app::Utf8ToWide(g));
            }
        }
        if (col[5] != "none") {
            for (const auto& g : SplitCsv(col[5], ',')) {
                policy.excluded_game_exes.push_back(vegaflow::app::Utf8ToWide(g));
            }
        }
        policy.heavy_work_as_game = (col[6] == "1");
        const bool is_fg_3d = (col[7] == "1");
        const bool has_ac = (col[8] == "1");

        const VfProcessDecision dec = vegaflow::ProcessDetector::ClassifyProcess(
            full_path, exe_name, policy, is_fg_3d, has_ac);

        const uint8_t exp_class = ParseClassName(col[9]);
        const uint8_t exp_action = static_cast<uint8_t>(ParseActionName(col[10]));
        const bool exp_ac = (col[11] == "1");
        const bool exp_pv = (col[12] == "1");

        if (dec.process_class != exp_class || dec.action != exp_action ||
            dec.protected_by_anticheat != exp_ac || dec.path_verified != exp_pv) {
            failures += " [" + id + ": got=(" + std::to_string(dec.process_class) + "," +
                        std::to_string(dec.action) + "," + std::to_string(dec.protected_by_anticheat) + "," +
                        std::to_string(dec.path_verified) + ")]";
        }
        ++checked;
    });

    RecordResult(checked >= 37 && failures.empty(),
                 "Test 54: Process classification matches shared vectors (OBS/Discord default, fake path, overrides)",
                 std::to_string(checked) + " vectors" + (failures.empty() ? ", 0 mismatches" : "; mismatches:" + failures));
}

void Test55() {
    // 1. Game switch restoration, PID+creation_time safety, DoNotTouch never touched, EcoOnly vs Throttle.
    FakeProcessControl pc;
    pc.procs[100].creation_time = 1001; // Game A
    pc.procs[200].creation_time = 2002; // Game B
    pc.procs[301].creation_time = 3001; // OBS (DoNotTouch)
    pc.procs[302].creation_time = 3002; // Spotify (EcoOnly)
    pc.procs[303].creation_time = 3003; // Chrome (Throttle)
    pc.procs[304].creation_time = 3004; // Unrelated process (never passed)

    std::vector<vegaflow::BackgroundProcessTarget> targets = {
        {301, 3001, L"obs64.exe", L"C:\\Program Files\\obs-studio\\bin\\64bit\\obs64.exe", vegaflow::ProcessClass::RecordingStreaming, vegaflow::ProcessAction::DoNotTouch},
        {302, 3002, L"spotify.exe", L"C:\\Users\\alice\\AppData\\Roaming\\Spotify\\Spotify.exe", vegaflow::ProcessClass::Music, vegaflow::ProcessAction::EcoOnly},
        {303, 3003, L"chrome.exe", L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", vegaflow::ProcessClass::Browsers, vegaflow::ProcessAction::Throttle},
    };

    vegaflow::CpuOrchestrator orch(pc);
    auto r1 = orch.ApplyOptimizations(100, 1001, 0, true, {0, 1}, {2, 3}, targets, false);
    const bool game_a_boosted = (pc.procs[100].boost_calls == 1 &&
                                 pc.procs[100].priority_class == ABOVE_NORMAL_PRIORITY_CLASS);
    const bool obs_untouched = (pc.procs[301].bg_apply_calls == 0 && !pc.procs[301].ecoqos);
    const bool spotify_eco_only = (pc.procs[302].bg_apply_calls == 1 && pc.procs[302].ecoqos &&
                                   !pc.procs[302].mem_deprioritized && !pc.procs[302].io_deprioritized);
    const bool chrome_throttled = (pc.procs[303].bg_apply_calls == 1 && pc.procs[303].ecoqos &&
                                   pc.procs[303].mem_deprioritized && pc.procs[303].io_deprioritized);
    (void)r1;

    // Switch directly to Game B -> Game A must be restored cleanly!
    orch.ApplyOptimizations(200, 2002, 0, false, {0, 1}, {2, 3}, targets, false);
    const bool game_a_restored_on_switch = (pc.procs[100].restore_game_calls == 1 &&
                                            pc.procs[100].priority_class == NORMAL_PRIORITY_CLASS);
    const bool game_b_boosted = (pc.procs[200].boost_calls == 1 &&
                                 pc.procs[200].priority_class == HIGH_PRIORITY_CLASS);

    // Simulate PID reuse: Chrome (pid=303) exited and Notepad inherited pid=303 with creation_time=9999!
    pc.procs[303].creation_time = 9999;
    pc.procs[303].ecoqos = false;
    pc.procs[303].mem_deprioritized = false;
    pc.procs[303].io_deprioritized = false;
    const int before_recycle_restore = pc.procs[303].bg_restore_calls;

    orch.RestoreAll(false);

    const bool recycled_pid_untouched = (pc.procs[303].bg_restore_calls == before_recycle_restore &&
                                         pc.procs[303].recycled_wrong_touch_calls == 0);
    const bool game_b_restored = (pc.procs[200].restore_game_calls == 1 &&
                                  pc.procs[200].priority_class == NORMAL_PRIORITY_CLASS);
    const bool obs_never_restored = (pc.procs[301].bg_restore_calls == 0);
    const bool unrelated_never_touched = (pc.procs[304].bg_apply_calls == 0 && pc.procs[304].bg_restore_calls == 0);
    const bool spotify_restored_eco_only = (pc.procs[302].eco_restore_only_calls == 1 &&
                                            pc.procs[302].full_throttle_restore_calls == 0 &&
                                            !pc.procs[302].ecoqos);

    // 2. "Me fue mal en este juego" (ExcludeGameAndRevert) + ConfigStore Phase 5 persistence roundtrip.
    InMemoryFileSystem fs;
    const std::wstring cfg_path = L"C:\\mock\\config.json";
    vegaflow::app::ConfigStore store(fs, cfg_path, true);
    vegaflow::app::AppConfig cfg = store.Load();
    cfg.process_policy.heavy_work_as_game = true;
    cfg.process_policy.category_actions.browsers = vegaflow::ProcessAction::EcoOnly;
    cfg.process_policy.exceptions.push_back({L"telegram.exe", vegaflow::ProcessAction::DoNotTouch});

    vegaflow::FileChangeJournal journal(fs, vegaflow::SteadyClock::Instance(), L"C:\\mock\\journal.vfj");
    vegaflow::session::GameSessionState state{};
    state.game_active = true;
    state.active_game_pid = 200;
    state.active_game_exe = L"badgame.exe";

    std::string why;
    const bool excluded_ok = vegaflow::session::ExcludeGameAndRevert(
        store, cfg, L"badgame.exe", nullptr, &orch, &state, &journal, &why);

    vegaflow::app::ConfigStore store2(fs, cfg_path, true);
    const vegaflow::app::AppConfig reloaded = store2.Load();
    const bool roundtrip_ok = reloaded.process_policy.heavy_work_as_game &&
                              reloaded.process_policy.category_actions.browsers == vegaflow::ProcessAction::EcoOnly &&
                              reloaded.process_policy.exceptions.size() == 1 &&
                              reloaded.process_policy.excluded_game_exes.size() == 1 &&
                              reloaded.process_policy.excluded_game_exes[0] == L"badgame.exe";
    const bool session_cleared = !state.game_active && state.active_game_pid == 0;
    const bool journal_audited = !journal.Entries().empty();

    const bool ok = game_a_boosted && obs_untouched && spotify_eco_only && chrome_throttled &&
                    game_a_restored_on_switch && game_b_boosted && recycled_pid_untouched &&
                    game_b_restored && obs_never_restored && unrelated_never_touched &&
                    spotify_restored_eco_only && excluded_ok && roundtrip_ok && session_cleared && journal_audited;

    std::string details =
        "Switch=" + std::string(game_a_restored_on_switch ? "OK" : "FAIL") +
        ", RecycledPidSafe=" + std::string(recycled_pid_untouched ? "OK" : "FAIL") +
        ", EcoOnly=" + std::string((spotify_eco_only && spotify_restored_eco_only) ? "OK" : "FAIL") +
        ", DoNotTouch=" + std::string((obs_untouched && obs_never_restored && unrelated_never_touched) ? "OK" : "FAIL") +
        ", ExcludeAndReload=" + std::string((excluded_ok && roundtrip_ok && session_cleared && journal_audited) ? "OK" : "FAIL");
    RecordResult(ok, "Test 55: CpuOrchestrator PID+FILETIME safety, game switch restore, and ExcludeGameAndRevert", details);
}

} // namespace vftest
