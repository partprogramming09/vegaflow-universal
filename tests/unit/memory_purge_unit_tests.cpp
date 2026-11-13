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

    vegaflow::SystemMemoryTelemetry SampleSystemMemory() const override {
        return telemetry;
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
    bool next_purge_succeeds = true;

    uint32_t TrimProcessWorkingSets(const std::vector<uint32_t>& pids) override {
        ++trim_call_count;
        for (uint32_t pid : pids) {
            trimmed_pids.push_back(pid);
        }
        return static_cast<uint32_t>(pids.size());
    }

    vegaflow::StandbyPurgeOutcome PurgeStandbyList(bool dry_run) override {
        vegaflow::StandbyPurgeOutcome out{};
        if (dry_run) {
            out.succeeded = true;
            out.detail = "Dry-run";
            return out;
        }
        ++purge_call_count;
        out.succeeded = next_purge_succeeds;
        out.detail = out.succeeded ? "Standby purged (fake)" : "Standby purge failed (fake)";
        return out;
    }
};

bool MatchesOutcome(const VfMemoryPurgeDecision& dec, const std::string& expected) {
    if (expected == "NONE") {
        return !dec.should_purge_standby && !dec.should_trim_background &&
               !dec.skipped_due_to_cooldown && !dec.skipped_due_to_disk_io;
    }
    if (expected == "INVALID") {
        return !dec.should_purge_standby && !dec.should_trim_background && !dec.guard_triggered;
    }
    if (expected == "PURGE_AND_TRIM") {
        return dec.should_purge_standby && dec.should_trim_background;
    }
    if (expected == "UMA_TRIM_ONLY") {
        return !dec.should_purge_standby && dec.should_trim_background && dec.guard_triggered;
    }
    if (expected == "COOLDOWN") {
        return !dec.should_purge_standby && !dec.should_trim_background && dec.skipped_due_to_cooldown;
    }
    if (expected == "DISK_IO") {
        return !dec.should_purge_standby && !dec.should_trim_background && dec.skipped_due_to_disk_io;
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
        if (col.size() != 16) {
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
        in.local_usage_bytes = std::stoull(col[5]) * kMB;
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
    const bool vectors_ok = (vec_total >= 18 && vec_mismatch == 0);

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
        probe.telemetry.vram.local_usage_bytes = 3900 * kMB;
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
        probe.telemetry.vram.local_usage_bytes = 3686 * kMB; // 90% >= 85%

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

} // namespace vftest
