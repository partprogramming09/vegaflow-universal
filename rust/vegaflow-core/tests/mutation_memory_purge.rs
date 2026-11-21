//! Behavior tests that pin the thresholds and arithmetic of `policy::memory_purge::evaluate_memory_purge`.

use vegaflow_core::policy::memory_purge::*;

const MB: u64 = 1024 * 1024;

/// Valid telemetry, no pressure, no GPU guard: nothing wants to act unless the test changes a field.
fn base() -> VfMemoryPurgeInput {
    VfMemoryPurgeInput {
        total_physical_bytes: 16 * 1024 * MB,
        free_and_zero_bytes: 8 * 1024 * MB,
        standby_bytes: 1024 * MB,
        local_budget_bytes: 0,
        gpu_shared_usage_bytes: 0,
        gpu_dedicated_usage_bytes: 0,
        vram_valid: false,
        memory_lists_valid: true,
        is_uma_gpu: false,
        allow_ram_purge: true,
        min_free_ram_mb: 1000,
        min_standby_mb: 0,
        cooldown_seconds: 0,
        elapsed_since_last_purge_ms: -1,
        game_disk_read_bytes_per_sec: 0,
        max_game_disk_read_bytes_per_sec: 0,
        pressure_threshold_override: 0.0,
        prev_interventions: 0,
        last_purge_level: 0,
        dry_run: false,
    }
}

// --- headroom ---

#[test]
fn headroom_is_free_plus_standby_in_whole_mebibytes() {
    let mut i = base();
    i.free_and_zero_bytes = 3 * MB;
    i.standby_bytes = 5 * MB;
    assert_eq!(evaluate_memory_purge(&i).available_os_headroom_mb, 8);
}

#[test]
fn headroom_truncates_partial_mebibytes() {
    let mut i = base();
    i.free_and_zero_bytes = 3 * MB + MB / 2;
    i.standby_bytes = 5 * MB + MB / 2 - 1;
    assert_eq!(evaluate_memory_purge(&i).available_os_headroom_mb, 8);
}

#[test]
fn headroom_is_zero_when_the_memory_lists_are_invalid() {
    let mut i = base();
    i.memory_lists_valid = false;
    assert_eq!(evaluate_memory_purge(&i).available_os_headroom_mb, 0);
}

// --- dxgi utilization ---

#[test]
fn utilization_is_zero_when_vram_is_not_valid_even_with_a_budget() {
    let mut i = base();
    i.vram_valid = false;
    i.local_budget_bytes = 100;
    i.gpu_shared_usage_bytes = 50;
    assert_eq!(evaluate_memory_purge(&i).dxgi_budget_utilization, 0.0);
}

#[test]
fn utilization_is_usage_over_budget_when_vram_is_valid() {
    let mut i = base();
    i.vram_valid = true;
    i.local_budget_bytes = 200;
    i.gpu_shared_usage_bytes = 50;
    assert_eq!(evaluate_memory_purge(&i).dxgi_budget_utilization, 0.25);
}

#[test]
fn utilization_is_zero_for_a_zero_budget_with_usage() {
    let mut i = base();
    i.vram_valid = true;
    i.local_budget_bytes = 0;
    i.gpu_shared_usage_bytes = 50;
    assert_eq!(evaluate_memory_purge(&i).dxgi_budget_utilization, 0.0);
}

#[test]
fn utilization_is_zero_not_nan_for_a_zero_budget_without_usage() {
    let mut i = base();
    i.vram_valid = true;
    i.local_budget_bytes = 0;
    i.gpu_shared_usage_bytes = 0;
    let d = evaluate_memory_purge(&i);
    assert!(!d.dxgi_budget_utilization.is_nan());
    assert_eq!(d.dxgi_budget_utilization, 0.0);
}

// --- pressure threshold ---

fn uma_at(usage: u64, threshold: f64) -> VfMemoryPurgeInput {
    let mut i = base();
    i.is_uma_gpu = true;
    i.vram_valid = true;
    i.local_budget_bytes = 100;
    i.gpu_shared_usage_bytes = usage;
    i.free_and_zero_bytes = 4096 * MB; // not critical
    i.pressure_threshold_override = threshold;
    i
}

#[test]
fn a_positive_override_replaces_the_default_threshold() {
    let d = evaluate_memory_purge(&uma_at(60, 0.5));
    assert!(d.dxgi_over_ceiling);
    assert!(d.guard_triggered);
}

#[test]
fn a_positive_override_above_the_default_raises_the_threshold() {
    let d = evaluate_memory_purge(&uma_at(90, 0.95));
    assert!(!d.dxgi_over_ceiling);
}

#[test]
fn a_zero_override_means_the_default_threshold() {
    assert!(!evaluate_memory_purge(&uma_at(84, 0.0)).dxgi_over_ceiling);
    assert!(evaluate_memory_purge(&uma_at(86, 0.0)).dxgi_over_ceiling);
}

#[test]
fn a_negative_override_means_the_default_threshold() {
    assert!(!evaluate_memory_purge(&uma_at(50, -1.0)).dxgi_over_ceiling);
}

#[test]
fn utilization_exactly_at_the_threshold_is_over_the_ceiling() {
    assert!(evaluate_memory_purge(&uma_at(50, 0.5)).dxgi_over_ceiling);
    assert!(!evaluate_memory_purge(&uma_at(49, 0.5)).dxgi_over_ceiling);
}

// --- free RAM critical (UMA) ---

fn uma_free(free_mb: u64) -> VfMemoryPurgeInput {
    let mut i = base();
    i.is_uma_gpu = true;
    i.free_and_zero_bytes = free_mb * MB;
    i.min_free_ram_mb = 0; // only the critical floor can act
    i
}

#[test]
fn free_ram_one_below_the_critical_floor_is_critical() {
    let d = evaluate_memory_purge(&uma_free(FREE_RAM_CRITICAL_MB - 1));
    assert!(d.free_ram_critical);
    assert!(d.guard_triggered);
    assert!(d.should_trim_background);
    assert_eq!(d.total_interventions, 1);
}

#[test]
fn free_ram_exactly_at_the_critical_floor_is_not_critical() {
    let d = evaluate_memory_purge(&uma_free(FREE_RAM_CRITICAL_MB));
    assert!(!d.free_ram_critical);
    assert!(!d.guard_triggered);
    assert!(!d.should_trim_background);
    assert_eq!(d.total_interventions, 0);
}

#[test]
fn free_ram_one_above_the_critical_floor_is_not_critical() {
    assert!(!evaluate_memory_purge(&uma_free(FREE_RAM_CRITICAL_MB + 1)).free_ram_critical);
}

// --- low free RAM (non-UMA, standby purge only) ---

fn low_free(free_mb: u64) -> VfMemoryPurgeInput {
    let mut i = base();
    i.free_and_zero_bytes = free_mb * MB;
    i.min_free_ram_mb = 2000;
    i
}

#[test]
fn free_ram_one_below_the_minimum_wants_a_standby_purge() {
    let d = evaluate_memory_purge(&low_free(1999));
    assert!(d.purge_level > 0);
    assert!(d.should_trim_background);
}

#[test]
fn free_ram_exactly_at_the_minimum_wants_no_purge() {
    let d = evaluate_memory_purge(&low_free(2000));
    assert_eq!(d.purge_level, 0);
    assert!(!d.should_trim_background);
}

#[test]
fn free_ram_one_above_the_minimum_wants_no_purge() {
    assert_eq!(evaluate_memory_purge(&low_free(2001)).purge_level, 0);
}

// --- shared cooldown ---

fn cooling(elapsed_ms: i64) -> VfMemoryPurgeInput {
    let mut i = low_free(100);
    i.cooldown_seconds = 60;
    i.elapsed_since_last_purge_ms = elapsed_ms;
    i
}

#[test]
fn cooldown_remaining_is_the_difference_in_whole_seconds() {
    let d = evaluate_memory_purge(&cooling(10_000));
    assert!(d.skipped_due_to_cooldown);
    assert_eq!(d.purge_level, 0);
    assert!(!d.should_trim_background);
    assert_eq!(d.cooldown_remaining_seconds, 50);
}

#[test]
fn cooldown_remaining_truncates_partial_seconds() {
    let d = evaluate_memory_purge(&cooling(10_500));
    assert_eq!(d.cooldown_remaining_seconds, 49);
}

#[test]
fn cooldown_just_before_expiry_still_skips() {
    let d = evaluate_memory_purge(&cooling(59_999));
    assert!(d.skipped_due_to_cooldown);
    assert_eq!(d.cooldown_remaining_seconds, 0);
}

#[test]
fn cooldown_exactly_expired_does_not_skip() {
    let d = evaluate_memory_purge(&cooling(60_000));
    assert!(!d.skipped_due_to_cooldown);
    assert!(d.purge_level > 0);
}

#[test]
fn never_purged_before_does_not_cool_down() {
    let d = evaluate_memory_purge(&cooling(-1));
    assert!(!d.skipped_due_to_cooldown);
    assert!(d.purge_level > 0);
}

#[test]
fn zero_cooldown_never_skips_even_right_after_a_purge() {
    let mut i = cooling(0);
    i.cooldown_seconds = 0;
    let d = evaluate_memory_purge(&i);
    assert!(!d.skipped_due_to_cooldown);
    assert!(d.purge_level > 0);
}

// --- disk I/O gating ---

fn reading(read: u64, max: u64) -> VfMemoryPurgeInput {
    let mut i = low_free(100);
    i.game_disk_read_bytes_per_sec = read;
    i.max_game_disk_read_bytes_per_sec = max;
    i
}

#[test]
fn a_zero_disk_limit_disables_the_gate() {
    let d = evaluate_memory_purge(&reading(500 * MB, 0));
    assert!(!d.skipped_due_to_disk_io);
    assert!(d.purge_level > 0);
}

#[test]
fn reading_above_the_limit_skips_the_purge() {
    let d = evaluate_memory_purge(&reading(101, 100));
    assert!(d.skipped_due_to_disk_io);
    assert_eq!(d.purge_level, 0);
    assert!(!d.should_trim_background);
}

#[test]
fn reading_exactly_at_the_limit_does_not_skip() {
    let d = evaluate_memory_purge(&reading(100, 100));
    assert!(!d.skipped_due_to_disk_io);
    assert!(d.purge_level > 0);
}

// --- standby floor and shared cooldown ---

#[test]
fn a_standby_purge_needs_at_least_min_standby_mb_of_standby_memory() {
    let mut i = base();
    i.free_and_zero_bytes = 500 * MB; // below min_free_ram_mb (1000)
    i.standby_bytes = 200 * MB;
    i.min_standby_mb = 200;
    assert!(evaluate_memory_purge(&i).purge_level > 0);
    i.min_standby_mb = 201;
    assert_eq!(evaluate_memory_purge(&i).purge_level, 0);
}

#[test]
fn the_cooldown_also_holds_back_the_uma_guard_trim() {
    let mut i = base();
    i.is_uma_gpu = true;
    i.free_and_zero_bytes = MB; // below the critical floor
    i.cooldown_seconds = 60;
    i.elapsed_since_last_purge_ms = 10_000;
    let held = evaluate_memory_purge(&i);
    assert!(held.free_ram_critical && held.guard_triggered);
    assert!(held.skipped_due_to_cooldown);
    assert!(!held.should_trim_background && held.purge_level == 0);

    i.elapsed_since_last_purge_ms = 60_000;
    let released = evaluate_memory_purge(&i);
    assert!(!released.skipped_due_to_cooldown);
    assert!(released.should_trim_background);
}
