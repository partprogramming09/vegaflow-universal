// Unit tests: Phase 4 unified memory purge & UMA guard policy (Test 53).
// Runs in-process with FakeMemoryProbe, FakeMemoryControl, and a manual Clock (no real Windows side effects).

#include "../test_support.hpp"

#include "bridge/rust_bridge.h"
#include "optimize/memory_manager.hpp"
#include "ports/clock.hpp"
#include "ports/memory_control.hpp"
#include "ports/memory_probe.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vftest {

namespace {

class ManualClock final : public vegaflow::Clock {
public:
    std::chrono::steady_clock::time_point now{std::chrono::hours(2000)};
    std::chrono::steady_clock::time_point Now() const override { return now; }
};

class FakeMemoryProbe final : public vegaflow::MemoryProbe {
public:
    vegaflow::SystemMemoryTelemetry telemetry{};
    std::map<uint32_t, uint64_t> disk_read_bytes_by_pid;
    std::map<uint32_t, std::string> image_name_by_pid;
    uint32_t self_pid = 9999;

    // E1.1: the LUID the manager asked for, and a different VRAM answer per adapter.
    mutable uint64_t last_luid = 0;
    std::map<uint64_t, vegaflow::VideoMemoryTelemetry> vram_by_luid;

    vegaflow::SystemMemoryTelemetry SampleSystemMemory(uint64_t target_luid = 0) const override {
        last_luid = target_luid;
        vegaflow::SystemMemoryTelemetry t = telemetry;
        if (target_luid != 0) {
            auto it = vram_by_luid.find(target_luid);
            t.vram = it != vram_by_luid.end() ? it->second : vegaflow::VideoMemoryTelemetry{};
        }
        return t;
    }

    bool SampleProcessDiskReadBytes(uint32_t pid, uint64_t& out_read_bytes) const override {
        auto it = disk_read_bytes_by_pid.find(pid);
        if (it == disk_read_bytes_by_pid.end()) return false;
        out_read_bytes = it->second;
        return true;
    }

    std::string QueryProcessImageName(uint32_t pid) const override {
        auto it = image_name_by_pid.find(pid);
        if (it == image_name_by_pid.end()) return {};
        return it->second;
    }

    uint32_t CurrentProcessId() const override {
        return self_pid;
    }
};

class FakeMemoryControl final : public vegaflow::MemoryControl {
public:
    std::vector<uint32_t> trimmed_pids;
    int trim_call_count = 0;
    int purge_call_count = 0;
    std::vector<int> purge_levels;  // E2.3: level of every real (non dry-run) NT purge call
    bool next_purge_succeeds = true;

    uint32_t TrimProcessWorkingSets(const std::vector<uint32_t>& pids) override {
        ++trim_call_count;
        for (uint32_t pid : pids) {
            trimmed_pids.push_back(pid);
        }
        return static_cast<uint32_t>(pids.size());
    }

    vegaflow::StandbyPurgeOutcome PurgeStandbyList(uint8_t level, bool dry_run) override {
        vegaflow::StandbyPurgeOutcome out{};
        if (dry_run) {
            out.succeeded = true;
            out.detail = "Dry-run";
            return out;
        }
        ++purge_call_count;
        purge_levels.push_back(level);
        out.succeeded = next_purge_succeeds;
        out.detail = out.succeeded ? "Standby purged (fake)" : "Standby purge failed (fake)";
        return out;
    }
};

bool MatchesOutcome(const VfMemoryPurgeDecision& dec, const std::string& expected) {
    const bool none = dec.purge_level == VF_PURGE_LEVEL_NONE;
    if (expected == "NONE") {
        return none && !dec.should_trim_background && !dec.skipped_due_to_cooldown && !dec.skipped_due_to_disk_io;
    }
    if (expected == "INVALID") {
        return none && !dec.should_trim_background && !dec.guard_triggered;
    }
    if (expected == "PURGE_LOW") {
        return dec.purge_level == VF_PURGE_LEVEL_LOW && dec.should_trim_background;
    }
    if (expected == "PURGE_FULL") {
        return dec.purge_level == VF_PURGE_LEVEL_FULL && dec.should_trim_background;
    }
    if (expected == "UMA_TRIM_ONLY") {
        return none && dec.should_trim_background && dec.guard_triggered;
    }
    if (expected == "COOLDOWN") {
        return none && !dec.should_trim_background && dec.skipped_due_to_cooldown;
    }
    if (expected == "DISK_IO") {
        return none && !dec.should_trim_background && dec.skipped_due_to_disk_io;
    }
    return false;
}

} // namespace

void Test53() {
    // 1. Verify shared vector table tests/vectors/memory_purge.tsv through C ABI.
    int vec_total = 0;
    int vec_mismatch = 0;
    std::string first_bad;
    ForEachTsvRow("tests/vectors/memory_purge.tsv", [&](const std::vector<std::string>& col, const std::string&) {
        if (col.size() != 17) {
            ++vec_mismatch;
            if (first_bad.empty()) first_bad = "bad columns";
            return;
        }
        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        const int64_t elapsed_s = std::stoll(col[13]);
        VfMemoryPurgeInput in{};
        in.total_physical_bytes = std::stoull(col[1]) * kMB;
        in.free_and_zero_bytes = std::stoull(col[2]) * kMB;
        in.standby_bytes = std::stoull(col[3]) * kMB;
        in.local_budget_bytes = std::stoull(col[4]) * kMB;
        in.gpu_shared_usage_bytes = std::stoull(col[5]) * kMB;
        in.last_purge_level = static_cast<uint8_t>(std::stoul(col[16]));
        in.vram_valid = (col[6] == "1");
        in.memory_lists_valid = (col[7] == "1");
        in.is_uma_gpu = (col[8] == "1");
        in.allow_ram_purge = true;
        in.min_free_ram_mb = std::stoull(col[9]);
        in.min_standby_mb = std::stoull(col[10]);
        in.game_disk_read_bytes_per_sec = std::stoull(col[11]) * kMB;
        in.max_game_disk_read_bytes_per_sec = std::stoull(col[12]) * kMB;
        in.elapsed_since_last_purge_ms = (elapsed_s < 0) ? -1 : (elapsed_s * 1000LL);
        in.cooldown_seconds = static_cast<uint32_t>(std::stoul(col[14]));
        in.pressure_threshold_override = 0.0;

        const VfMemoryPurgeDecision dec = vf_rust_evaluate_memory_purge(&in);
        ++vec_total;
        if (!MatchesOutcome(dec, col[15])) {
            ++vec_mismatch;
            if (first_bad.empty()) first_bad = col[0];
        }
    });
    const bool vectors_ok = (vec_total >= 32 && vec_mismatch == 0);

    // 2. Per-tick disk I/O rate sampling across idle ticks before early exits.
    bool disk_peak_ok = false;
    {
        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        ManualClock clock;
        FakeMemoryProbe probe;
        FakeMemoryControl control;
        probe.telemetry.total_physical_bytes = 16384 * kMB;
        probe.telemetry.free_and_zero_bytes = 4000 * kMB; // Healthy for 40 ticks
        probe.telemetry.standby_bytes = 3000 * kMB;
        probe.telemetry.memory_lists_valid = true;
        probe.disk_read_bytes_by_pid[100] = 0;

        vegaflow::MemoryPurgeConfig cfg{};
        vegaflow::MemoryManager mgr(cfg, clock, probe, control);

        // 40 quiet ticks (60 seconds total) with high free RAM -> early exits must still update I/O baseline
        for (int t = 0; t < 40; ++t) {
            clock.now += std::chrono::milliseconds(1500);
            mgr.EvaluateAndOptimize(100, {200}, false);
        }

        // Sudden 30 MB read over one 1.5s tick (= 20 MB/s > 15 MB/s) alongside low free RAM
        clock.now += std::chrono::milliseconds(1500);
        probe.disk_read_bytes_by_pid[100] = 30 * kMB;
        probe.telemetry.free_and_zero_bytes = 800 * kMB;
        auto burst_rep = mgr.EvaluateAndOptimize(100, {200}, false);
        disk_peak_ok = burst_rep.skipped_due_to_disk_io && (control.purge_call_count == 0) &&
                       (control.trim_call_count == 0);
    }

    // 3. Invalid NT memory lists never pretend standby/ullAvailPhys is free RAM and never purge.
    bool invalid_lists_ok = false;
    {
        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        ManualClock clock;
        FakeMemoryProbe probe;
        FakeMemoryControl control;
        probe.telemetry.total_physical_bytes = 16384 * kMB;
        probe.telemetry.free_and_zero_bytes = 0;
        probe.telemetry.standby_bytes = 0;
        probe.telemetry.memory_lists_valid = false;
        probe.telemetry.vram.valid = true;
        probe.telemetry.vram.local_budget_bytes = 4096 * kMB;
        probe.telemetry.vram.gpu_shared_usage_bytes = 3900 * kMB;
        probe.image_name_by_pid[200] = "chrome.exe";

        vegaflow::MemoryPurgeConfig cfg{};
        vegaflow::MemoryManager mgr(cfg, clock, probe, control);
        auto rep = mgr.EvaluateAndOptimize(0, {200}, false);

        vegaflow::ApuOptimizationPolicy policy{};
        policy.tier = vegaflow::ApuTier::Tier2_MidVegaUMA;
        policy.gpu_connection = vegaflow::GpuConnection::Integrated;
        policy.apply_uma_memory_guard = true;
        auto uma = mgr.EvaluateUmaPressureGuard(probe.telemetry, policy, {200}, false);
        invalid_lists_ok = !rep.standby_purged && !uma.guard_triggered &&
                           (control.purge_call_count == 0) && (control.trim_call_count == 0);
    }

    // 4. Shared 90s cooldown across UMA Guard + Purge AND integrated-GPU gating + Do-Not-Trim list.
    bool shared_cooldown_ok = false;
    bool gpu_gate_ok = false;
    bool do_not_trim_ok = false;
    {
        constexpr uint64_t kMB = 1024ULL * 1024ULL;
        ManualClock clock;
        FakeMemoryProbe probe;
        FakeMemoryControl control;
        probe.telemetry.total_physical_bytes = 16384 * kMB;
        probe.telemetry.free_and_zero_bytes = 3000 * kMB;
        probe.telemetry.standby_bytes = 2500 * kMB;
        probe.telemetry.memory_lists_valid = true;
        probe.telemetry.vram.valid = true;
        probe.telemetry.vram.local_budget_bytes = 4096 * kMB;
        probe.telemetry.vram.gpu_shared_usage_bytes = 3686 * kMB; // 90% >= 85%

        // Background mix: protected apps + trimmable browsers
        probe.image_name_by_pid[10] = "C:\\Program Files\\obs-studio\\bin\\64bit\\OBS64.EXE";
        probe.image_name_by_pid[11] = "C:\\Users\\test\\AppData\\Local\\Discord\\Discord.exe";
        probe.image_name_by_pid[12] = "ms-teams.exe";
        probe.image_name_by_pid[13] = "Zoom.exe";
        probe.image_name_by_pid[20] = "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
        probe.image_name_by_pid[21] = "notepad.exe";
        const std::vector<uint32_t> bg_pids = {10, 11, 12, 13, 20, 21};

        vegaflow::MemoryPurgeConfig cfg{};
        cfg.cooldown_seconds = 90;
        vegaflow::MemoryManager mgr(cfg, clock, probe, control);

        // Discrete GPU must NOT trigger UMA guard
        vegaflow::ApuOptimizationPolicy discrete_policy{};
        discrete_policy.tier = vegaflow::ApuTier::Tier4_DiscreteOrCustom;
        discrete_policy.gpu_connection = vegaflow::GpuConnection::Discrete;
        discrete_policy.apply_uma_memory_guard = false;
        auto disc_uma = mgr.EvaluateUmaPressureGuard(probe.telemetry, discrete_policy, bg_pids, false);
        gpu_gate_ok = !disc_uma.guard_triggered && (control.trim_call_count == 0);

        // Integrated GPU triggers UMA guard -> trims ONLY 20 & 21 and starts 90s cooldown
        vegaflow::ApuOptimizationPolicy uma_policy{};
        uma_policy.tier = vegaflow::ApuTier::Tier2_MidVegaUMA;
        uma_policy.gpu_connection = vegaflow::GpuConnection::Integrated;
        uma_policy.apply_uma_memory_guard = true;
        auto uma1 = mgr.EvaluateUmaPressureGuard(probe.telemetry, uma_policy, bg_pids, false);

        // +10s later: EvaluateAndOptimize MUST be blocked by shared 90s cooldown
        clock.now += std::chrono::seconds(10);
        probe.telemetry.free_and_zero_bytes = 900 * kMB;
        auto purge_blocked = mgr.EvaluateAndOptimize(0, bg_pids, false);

        // +81s later (91s total): EvaluateAndOptimize proceeds and resets cooldown
        clock.now += std::chrono::seconds(81);
        auto purge_ok = mgr.EvaluateAndOptimize(0, bg_pids, false);

        // +5s later: second EvaluateUmaPressureGuard call MUST also be blocked by shared cooldown
        clock.now += std::chrono::seconds(5);
        auto uma2_blocked = mgr.EvaluateUmaPressureGuard(probe.telemetry, uma_policy, bg_pids, false);

        shared_cooldown_ok = uma1.guard_triggered &&
                             purge_blocked.skipped_due_to_cooldown &&
                             purge_ok.standby_purged &&
                             !uma2_blocked.guard_triggered &&
                             (control.purge_call_count == 1) &&
                             (control.trim_call_count == 2);

        // Check every single PID ever trimmed across all calls
        do_not_trim_ok = (control.trimmed_pids == std::vector<uint32_t>{20, 21, 20, 21});
    }

    const bool ok = vectors_ok && disk_peak_ok && invalid_lists_ok &&
                    shared_cooldown_ok && gpu_gate_ok && do_not_trim_ok;
    RecordResult(ok, "Test 53: Phase 4 unified memory purge & UMA guard rules",
                 "Vectors (" + std::to_string(vec_total) + ", mismatch " + std::to_string(vec_mismatch) +
                     (first_bad.empty() ? "" : " first=" + first_bad) + "): " + (vectors_ok ? "PASS" : "FAIL") +
                     ", Per-tick disk I/O: " + (disk_peak_ok ? "PASS" : "FAIL") +
                     ", Invalid lists fail-closed: " + (invalid_lists_ok ? "PASS" : "FAIL") +
                     ", Shared 90s cooldown: " + (shared_cooldown_ok ? "PASS" : "FAIL") +
                     ", Integrated-GPU gate: " + (gpu_gate_ok ? "PASS" : "FAIL") +
                     ", Do-Not-Trim (OBS/Discord/calls): " + (do_not_trim_ok ? "PASS" : "FAIL"));
}

void Test92() {
    // E1.1: VRAM is read from the GPU the profile selected, follows a change of GPU, and never borrows another GPU's numbers.
    constexpr uint64_t kMB = 1024ULL * 1024ULL;
    constexpr uint64_t kIgpu = 0x0000000100001111ULL;
    constexpr uint64_t kEgpu = 0x0000000200002222ULL;
    ManualClock clock;
    FakeMemoryProbe probe;
    FakeMemoryControl control;
    probe.telemetry.total_physical_bytes = 16384 * kMB;
    probe.telemetry.memory_lists_valid = true;
    probe.vram_by_luid[kIgpu] = {1 * kMB, 2048 * kMB, 0, 0, true};
    probe.vram_by_luid[kEgpu] = {5 * kMB, 24576 * kMB, 0, 0, true};
    vegaflow::MemoryPurgeConfig cfg{};
    vegaflow::MemoryManager mgr(cfg, clock, probe, control);

    const bool default_is_zero = (mgr.SampleTelemetry(), probe.last_luid == 0);
    mgr.SetTargetLuid(kIgpu);
    const auto igpu = mgr.SampleTelemetry();
    const bool igpu_ok = probe.last_luid == kIgpu && igpu.vram.valid && igpu.vram.local_budget_bytes == 2048 * kMB;
    mgr.SetTargetLuid(kEgpu); // eGPU plugged in: the target follows without recreating the manager
    const auto egpu = mgr.SampleTelemetry();
    const bool egpu_ok = probe.last_luid == kEgpu && egpu.vram.valid && egpu.vram.local_budget_bytes == 24576 * kMB;
    mgr.SetTargetLuid(0x0000000300003333ULL); // unplugged / unknown: invalid, not the iGPU's numbers
    const auto gone = mgr.SampleTelemetry();
    const bool unknown_is_invalid = !gone.vram.valid && gone.vram.local_budget_bytes == 0;
    mgr.SetTargetLuid(kIgpu);
    const bool back_ok = mgr.SampleTelemetry().vram.local_budget_bytes == 2048 * kMB;
    mgr.EvaluateAndOptimize(100, {200}, true);
    const bool evaluate_uses_target = probe.last_luid == kIgpu; // the purge decision reads the same adapter

    const bool ok = default_is_zero && igpu_ok && egpu_ok && unknown_is_invalid && back_ok && evaluate_uses_target;
    RecordResult(ok, "Test 92: MemoryManager samples VRAM of the selected GPU (LUID) and follows hot-plug",
                 std::string("Default=") + (default_is_zero ? "OK" : "FAIL") + ", Igpu=" + (igpu_ok ? "OK" : "FAIL") +
                     ", Egpu=" + (egpu_ok ? "OK" : "FAIL") + ", Unknown=" + (unknown_is_invalid ? "OK" : "FAIL") +
                     ", Back=" + (back_ok ? "OK" : "FAIL") + ", Evaluate=" + (evaluate_uses_target ? "OK" : "FAIL"));
}

void Test108() {
    // E2.3: staged standby purge (level 1 then 2 by attempts) and the global GPU counter feeding the UMA guard.
    constexpr uint64_t kMB = 1024ULL * 1024ULL;
    std::string failed;
    auto check = [&failed](bool ok, const char* name) {
        if (!ok) failed += std::string(failed.empty() ? "" : ", ") + name;
    };
    // Pressure from RAM alone: free 1000 MB (< 1200 min), standby 4 GB, PDH not available (vram invalid).
    auto make_probe = [&](FakeMemoryProbe& p) {
        p.telemetry.total_physical_bytes = 16384 * kMB;
        p.telemetry.free_and_zero_bytes = 1000 * kMB;
        p.telemetry.standby_bytes = 4096 * kMB;
        p.telemetry.memory_lists_valid = true;
        p.image_name_by_pid[200] = "chrome.exe";
    };
    const std::chrono::seconds kPause(91);

    {  // 1, 2: level 1 first, level 2 after the pause while the pressure lasts, and it stays at 2
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        auto r1 = mgr.EvaluateAndOptimize(0, {200}, false);
        clock.now += kPause;
        auto r2 = mgr.EvaluateAndOptimize(0, {200}, false);
        clock.now += kPause;
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_levels == std::vector<int>({1, 2, 2}), "1/2 levels 1,2,2");
        check(r1.standby_purged && r1.purge_level == 1 && r2.purge_level == 2, "1/2 report levels");
    }
    {  // 3: the pressure goes away for one tick, then returns: new episode, level 1 again
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        clock.now += kPause;
        probe.telemetry.free_and_zero_bytes = 8000 * kMB;
        mgr.EvaluateAndOptimize(0, {200}, false);
        clock.now += kPause;
        probe.telemetry.free_and_zero_bytes = 1000 * kMB;
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_levels == std::vector<int>({1, 1}), "3 new episode restarts at level 1");
    }
    {  // 3b: global shared usage above the budget (146 %) is clamped to 100 %, no overflow, one level-1 purge
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        probe.telemetry.free_and_zero_bytes = 4000 * kMB;
        probe.telemetry.vram.valid = true;
        probe.telemetry.vram.local_budget_bytes = 4096 * kMB;
        probe.telemetry.vram.gpu_shared_usage_bytes = 6000 * kMB;
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        vegaflow::ApuOptimizationPolicy policy{};
        policy.tier = vegaflow::ApuTier::Tier2_MidVegaUMA;
        policy.gpu_connection = vegaflow::GpuConnection::Integrated;
        policy.apply_uma_memory_guard = true;
        const auto guard = mgr.EvaluateUmaPressureGuard(probe.telemetry, policy, {}, true);
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(guard.guard_triggered && guard.dxgi_budget_utilization == 1.0, "3b utilization clamped to 1.0");
        check(control.purge_levels == std::vector<int>({1}), "3b level follows the rule");
    }
    {  // 3c, 5: the same purge asked twice in one lap -> one NT call; the second is held by the pause
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        auto again = mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_call_count == 1 && again.skipped_due_to_cooldown && again.purge_level == 0, "3c/5 one NT call");
    }
    {  // 4: standby below the minimum, discrete GPU -> no purge and no trim
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        probe.telemetry.standby_bytes = 1000 * kMB;
        vegaflow::MemoryPurgeConfig cfg{};
        cfg.is_uma_architecture = false;
        vegaflow::MemoryManager mgr(cfg, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_call_count == 0 && control.trim_call_count == 0, "4 standby below minimum");
    }
    {  // 7: PDH failed (vram invalid): the GPU signal is ignored, RAM alone decides
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        probe.telemetry.free_and_zero_bytes = 8000 * kMB;
        probe.telemetry.vram.valid = false;
        probe.telemetry.vram.local_budget_bytes = 4096 * kMB;
        probe.telemetry.vram.gpu_shared_usage_bytes = 4000 * kMB;
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        const bool quiet = control.purge_call_count == 0;
        probe.telemetry.free_and_zero_bytes = 900 * kMB;
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(quiet && control.purge_levels == std::vector<int>({1}), "7 RAM alone decides");
    }
    {  // 8: budget 0 -> no division by zero, no GPU signal; 10: discrete GPU near its budget does not count
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        probe.telemetry.free_and_zero_bytes = 8000 * kMB;
        probe.telemetry.vram.valid = true;
        probe.telemetry.vram.local_budget_bytes = 0;
        probe.telemetry.vram.gpu_shared_usage_bytes = 3000 * kMB;
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_call_count == 0, "8 budget 0");

        FakeMemoryProbe probe2; FakeMemoryControl control2;
        make_probe(probe2);
        probe2.telemetry.free_and_zero_bytes = 8000 * kMB;
        probe2.telemetry.vram.valid = true;
        probe2.telemetry.vram.local_budget_bytes = 8192 * kMB;
        probe2.telemetry.vram.gpu_shared_usage_bytes = 8000 * kMB;
        vegaflow::MemoryPurgeConfig cfg{};
        cfg.is_uma_architecture = false;
        vegaflow::MemoryManager mgr2(cfg, clock, probe2, control2);
        mgr2.EvaluateAndOptimize(0, {200}, false);
        check(control2.purge_call_count == 0 && control2.trim_call_count == 0, "10 discrete GPU signal ignored");
    }
    {  // 9: dry_run decides but never calls NT, trims nothing and does not advance the level or the pause
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        auto dry = mgr.EvaluateAndOptimize(0, {200}, true);
        check(control.purge_call_count == 0 && control.trim_call_count == 0 && !dry.standby_purged, "9 dry run touches nothing");
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_levels == std::vector<int>({1}), "9 dry run did not advance the level");
    }
    {  // a failed purge (no privilege) does not climb to level 2: level 2 would fail the same way
        ManualClock clock; FakeMemoryProbe probe; FakeMemoryControl control;
        make_probe(probe);
        control.next_purge_succeeds = false;
        vegaflow::MemoryManager mgr({}, clock, probe, control);
        mgr.EvaluateAndOptimize(0, {200}, false);
        clock.now += kPause;
        mgr.EvaluateAndOptimize(0, {200}, false);
        check(control.purge_levels == std::vector<int>({1, 1}), "failed purge does not escalate");
    }

    RecordResult(failed.empty(), "Test 108: E2.3 staged standby purge (level 1 then 2) and global GPU counter in the UMA guard",
                 failed.empty() ? "all 11 cases pass" : "failed: " + failed);
}

} // namespace vftest
