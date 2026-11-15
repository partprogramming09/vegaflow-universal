#include "optimize/memory_manager.hpp"

#include "bridge/rust_bridge.h"
#include "windows/memory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

MemoryManager::MemoryManager(const MemoryPurgeConfig& config, Clock& clock)
    : MemoryManager(config, clock, DefaultWindowsMemoryProbe(), DefaultWindowsMemoryControl()) {}

MemoryManager::MemoryManager(
    const MemoryPurgeConfig& config,
    Clock& clock,
    MemoryProbe& probe,
    MemoryControl& control)
    : config_(config), clock_(clock), probe_(probe), control_(control) {}

SystemMemoryTelemetry MemoryManager::SampleTelemetry() const {
    return probe_.SampleSystemMemory();
}

int64_t MemoryManager::ElapsedSinceLastPurgeMs(std::chrono::steady_clock::time_point now) const {
    if (last_purge_time_.time_since_epoch().count() == 0) {
        return -1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_purge_time_).count();
    return elapsed >= 0 ? elapsed : 0;
}

uint64_t MemoryManager::SampleDiskReadRateBytesPerSec(
    uint32_t active_game_pid,
    std::chrono::steady_clock::time_point now) {

    if (active_game_pid == 0) {
        last_io_pid_ = 0;
        last_game_read_bytes_ = 0;
        last_io_sample_time_ = {};
        last_disk_read_rate_bytes_per_sec_ = 0;
        return 0;
    }

    uint64_t current_read_bytes = 0;
    if (!probe_.SampleProcessDiskReadBytes(active_game_pid, current_read_bytes)) {
        last_disk_read_rate_bytes_per_sec_ = 0;
        return 0;
    }

    uint64_t rate = 0;
    if (active_game_pid == last_io_pid_ &&
        last_io_sample_time_.time_since_epoch().count() > 0 &&
        now > last_io_sample_time_) {
        const double elapsed_s = std::chrono::duration<double>(now - last_io_sample_time_).count();
        if (elapsed_s > 0.001) {
            const uint64_t delta = (current_read_bytes >= last_game_read_bytes_)
                ? (current_read_bytes - last_game_read_bytes_)
                : 0;
            rate = static_cast<uint64_t>(delta / elapsed_s);
        }
    }

    last_io_pid_ = active_game_pid;
    last_game_read_bytes_ = current_read_bytes;
    last_io_sample_time_ = now;
    last_disk_read_rate_bytes_per_sec_ = rate;
    return rate;
}

std::vector<uint32_t> MemoryManager::FilterTrimmableBackgroundPids(
    uint32_t active_game_pid,
    const std::vector<uint32_t>& background_pids) const {

    std::vector<uint32_t> allowed;
    allowed.reserve(background_pids.size());
    const uint32_t self_pid = probe_.CurrentProcessId();
    for (uint32_t pid : background_pids) {
        if (pid == 0 || pid == 4 || pid == self_pid || (active_game_pid != 0 && pid == active_game_pid)) {
            continue;
        }
        const std::string image = probe_.QueryProcessImageName(pid);
        if (image.empty() || vf_rust_is_factory_memory_protected_exe(image.data(), image.size())) {
            continue;
        }
        allowed.push_back(pid);
    }
    return allowed;
}

MemoryActionReport MemoryManager::EvaluateAndOptimize(
    uint32_t active_game_pid,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    MemoryActionReport report{};
    const auto now = clock_.Now();

    // Sample game disk I/O rate on EVERY cycle before early exits so short bursts are never diluted.
    const uint64_t disk_read_rate = SampleDiskReadRateBytesPerSec(active_game_pid, now);
    const SystemMemoryTelemetry telemetry = probe_.SampleSystemMemory();

    VfMemoryPurgeInput input{};
    input.total_physical_bytes = telemetry.total_physical_bytes;
    input.free_and_zero_bytes = telemetry.free_and_zero_bytes;
    input.standby_bytes = telemetry.standby_bytes;
    input.local_budget_bytes = telemetry.vram.local_budget_bytes;
    input.local_usage_bytes = telemetry.vram.local_usage_bytes;
    input.vram_valid = telemetry.vram.valid;
    input.memory_lists_valid = telemetry.memory_lists_valid;
    input.is_uma_gpu = config_.is_uma_architecture;
    input.allow_ram_purge = true;
    input.min_free_ram_mb = config_.min_free_ram_mb;
    input.min_standby_mb = config_.min_standby_mb;
    input.cooldown_seconds = config_.cooldown_seconds;
    input.elapsed_since_last_purge_ms = ElapsedSinceLastPurgeMs(now);
    input.game_disk_read_bytes_per_sec = disk_read_rate;
    input.max_game_disk_read_bytes_per_sec = config_.max_game_disk_read_bytes_per_sec;
    input.pressure_threshold_override = 0.0; // Single source of truth in Rust memory_purge.rs
    input.prev_interventions = guard_interventions_count_;
    input.dry_run = dry_run;

    const VfMemoryPurgeDecision dec = vf_rust_evaluate_memory_purge(&input);

    if (dec.skipped_due_to_cooldown) {
        report.skipped_due_to_cooldown = true;
        report.reason = "Skipped due to cooldown (" + std::to_string(dec.cooldown_remaining_seconds) + "s remaining)";
        return report;
    }

    if (dec.skipped_due_to_disk_io) {
        report.skipped_due_to_disk_io = true;
        report.reason = "Skipped due to game disk I/O read peak: " +
            std::to_string(disk_read_rate / (1024ULL * 1024ULL)) + " MB/s (max " +
            std::to_string(config_.max_game_disk_read_bytes_per_sec / (1024ULL * 1024ULL)) + " MB/s)";
        return report;
    }

    if (!dec.should_purge_standby && !dec.should_trim_background) {
        if (!telemetry.memory_lists_valid || telemetry.total_physical_bytes == 0) {
            report.reason = "System memory list telemetry unavailable; purge skipped";
        } else {
            report.reason = "Thresholds not met: Free/Zero=" +
                std::to_string(telemetry.free_and_zero_bytes / (1024ULL * 1024ULL)) + "MB (min " +
                std::to_string(config_.min_free_ram_mb) + "MB), Standby=" +
                std::to_string(telemetry.standby_bytes / (1024ULL * 1024ULL)) + "MB (min " +
                std::to_string(config_.min_standby_mb) + "MB)";
        }
        return report;
    }

    if (dry_run) {
        report.reason = "Dry run: purge condition met, no action taken";
        return report;
    }

    const std::vector<uint32_t> trimmable = FilterTrimmableBackgroundPids(active_game_pid, background_pids);
    report.background_processes_trimmed = control_.TrimProcessWorkingSets(trimmable);

    // Record purge/trim timestamp for shared cooldown across both EvaluateAndOptimize and EvaluateUmaPressureGuard.
    last_purge_time_ = clock_.Now();

    if (dec.should_purge_standby) {
        const StandbyPurgeOutcome outcome = control_.PurgeStandbyList(false);
        report.standby_purged = outcome.succeeded;
        if (outcome.succeeded) {
            report.reason = "Standby list purged successfully and background processes trimmed";
        } else if (outcome.privilege_missing) {
            report.reason = "Background Working Sets trimmed (" +
                std::to_string(report.background_processes_trimmed) +
                " processes); Standby List purge skipped (requires Administrator / --elevate)";
        } else {
            report.reason = outcome.detail;
        }
    } else {
        report.reason = "Background Working Sets trimmed (" +
            std::to_string(report.background_processes_trimmed) + " processes)";
    }

    return report;
}

ManualPurgeReport MemoryManager::ForcePurgeNow(const std::vector<uint32_t>& background_pids, bool dry_run) {
    ManualPurgeReport report{};

    const SystemMemoryTelemetry before = probe_.SampleSystemMemory();
    report.free_bytes_before = before.free_and_zero_bytes;
    report.standby_bytes_before = before.standby_bytes;

    if (dry_run) {
        report.free_bytes_after = before.free_and_zero_bytes;
        report.standby_bytes_after = before.standby_bytes;
        report.standby_purge_succeeded = true;
        report.status_message = "Dry-run: purge simulated";
        return report;
    }

    const std::vector<uint32_t> trimmable = FilterTrimmableBackgroundPids(0, background_pids);
    report.trimmed_processes = control_.TrimProcessWorkingSets(trimmable);

    const StandbyPurgeOutcome outcome = control_.PurgeStandbyList(false);
    report.standby_purge_succeeded = outcome.succeeded;
    if (outcome.succeeded) {
        last_purge_time_ = clock_.Now();
        report.status_message = outcome.detail;
    } else if (outcome.privilege_missing) {
        report.status_message = "Failed to purge standby list (NTSTATUS " + std::to_string(outcome.nt_status) +
            "). Administrator privileges required (--elevate) to purge standby list";
    } else {
        report.status_message = outcome.detail;
    }

    const SystemMemoryTelemetry after = probe_.SampleSystemMemory();
    report.free_bytes_after = after.free_and_zero_bytes;
    report.standby_bytes_after = after.standby_bytes;

    return report;
}

uint64_t MemoryManager::GetCurrentProcessPrivateWorkingSetBytes() {
    return QueryCurrentProcessPrivateWorkingSetBytes();
}

bool MemoryManager::PurgeStandbyList(bool dryRun) {
    const StandbyPurgeOutcome outcome = DefaultWindowsMemoryControl().PurgeStandbyList(dryRun);
    if (outcome.succeeded && !dryRun) {
        DefaultWindowsMemoryControl().TrimProcessWorkingSets({DefaultWindowsMemoryProbe().CurrentProcessId()});
    }
    return outcome.succeeded;
}

bool MemoryManager::QueryStandbyMemoryMib(uint64_t& standbyMib, uint64_t& freeMib) {
    standbyMib = 0;
    freeMib = 0;

    const SystemMemoryTelemetry sample = DefaultWindowsMemoryProbe().SampleSystemMemory();
    if (!sample.memory_lists_valid) {
        return false;
    }
    standbyMib = sample.standby_bytes / (1024ULL * 1024ULL);
    freeMib = sample.free_and_zero_bytes / (1024ULL * 1024ULL);
    return true;
}

UmaGuardStatus MemoryManager::EvaluateUmaPressureGuard(
    const SystemMemoryTelemetry& telem,
    const ApuOptimizationPolicy& policy,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    const auto now = clock_.Now();
    // Phase 4: UMA pressure guard only acts when the game's GPU is integrated.
    const bool is_uma_gpu = policy.apply_uma_memory_guard &&
                            (policy.tier != ApuTier::Tier4_DiscreteOrCustom) &&
                            (policy.gpu_connection == GpuConnection::Integrated);

    VfMemoryPurgeInput input{};
    input.total_physical_bytes = telem.total_physical_bytes;
    input.free_and_zero_bytes = telem.free_and_zero_bytes;
    input.standby_bytes = telem.standby_bytes;
    input.local_budget_bytes = telem.vram.local_budget_bytes;
    input.local_usage_bytes = telem.vram.local_usage_bytes;
    input.vram_valid = telem.vram.valid;
    input.memory_lists_valid = telem.memory_lists_valid;
    input.is_uma_gpu = is_uma_gpu;
    input.allow_ram_purge = policy.allow_ram_purge;
    input.min_free_ram_mb = config_.min_free_ram_mb;
    input.min_standby_mb = config_.min_standby_mb;
    input.cooldown_seconds = config_.cooldown_seconds;
    // Dry-run reads pressure for reporting/UI without being masked by cooldown; live actions share the 90s cooldown.
    input.elapsed_since_last_purge_ms = dry_run ? -1 : ElapsedSinceLastPurgeMs(now);
    input.game_disk_read_bytes_per_sec = dry_run ? 0 : last_disk_read_rate_bytes_per_sec_;
    input.max_game_disk_read_bytes_per_sec = config_.max_game_disk_read_bytes_per_sec;
    input.pressure_threshold_override = policy.runtime_pressure_threshold;
    input.prev_interventions = guard_interventions_count_;
    input.dry_run = dry_run;

    const VfMemoryPurgeDecision dec = vf_rust_evaluate_memory_purge(&input);

    UmaGuardStatus status{};
    status.available_os_headroom_mb = dec.available_os_headroom_mb;
    status.dxgi_budget_utilization = dec.dxgi_budget_utilization;

    if (!dry_run && (dec.skipped_due_to_cooldown || dec.skipped_due_to_disk_io)) {
        // Suppressed by shared cooldown or disk I/O burst: do not trim, bump interventions, or spam logs.
        status.guard_triggered = false;
        status.total_interventions = guard_interventions_count_;
        status.active_reason = dec.skipped_due_to_cooldown
            ? ("UMA Guard Cooldown (" + std::to_string(dec.cooldown_remaining_seconds) + "s remaining)")
            : "UMA Guard Passive: Disk I/O burst";
        last_dxgi_pressure_ratio_ = status.dxgi_budget_utilization;
        last_guard_triggered_ = false;
        return status;
    }

    guard_interventions_count_ = dec.total_interventions;
    status.guard_triggered = dec.guard_triggered;
    status.total_interventions = dec.total_interventions;

    if (dec.guard_triggered) {
        if (dec.dxgi_over_ceiling && dec.free_ram_critical) {
            status.active_reason = "UMA Guard Triggered: DXGI budget utilization above threshold (" +
                std::to_string(static_cast<int>(status.dxgi_budget_utilization * 100)) + "%) and low free RAM (" +
                std::to_string(telem.free_and_zero_bytes / (1024ULL * 1024ULL)) + " MB)";
        } else if (dec.dxgi_over_ceiling) {
            status.active_reason = "UMA Guard Triggered: DXGI budget utilization above threshold (" +
                std::to_string(static_cast<int>(status.dxgi_budget_utilization * 100)) + "%)";
        } else {
            status.active_reason = "UMA Guard Triggered: Low free RAM (" +
                std::to_string(telem.free_and_zero_bytes / (1024ULL * 1024ULL)) + " MB < 1536 MB)";
        }

        if (!dry_run && dec.should_trim_background) {
            const std::vector<uint32_t> trimmable = FilterTrimmableBackgroundPids(0, background_pids);
            control_.TrimProcessWorkingSets(trimmable);
            last_purge_time_ = clock_.Now();
        }
    } else {
        status.active_reason = "UMA Guard Passive: Memory pressure within safe bounds";
    }

    last_dxgi_pressure_ratio_ = status.dxgi_budget_utilization;
    last_guard_triggered_ = status.guard_triggered;

    return status;
}

} // namespace vegaflow
