//! Unified memory purge and UMA pressure guard policy (phase 4).
//!
//! Pure decision over telemetry, disk I/O rate, shared cooldown, and factory Do-Not-Trim exclusions.
//! Holds the single source of truth for `DEFAULT_VRAM_PRESSURE_THRESHOLD` (0.85).
//! Does zero Windows I/O (all ntdll/DXGI calls live in C++ `windows/memory.cpp`).

pub const DEFAULT_VRAM_PRESSURE_THRESHOLD: f64 = 0.85;
pub const FREE_RAM_CRITICAL_MB: u64 = 1536;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VfMemoryPurgeInput {
    pub total_physical_bytes: u64,
    pub free_and_zero_bytes: u64,
    pub standby_bytes: u64,
    pub local_budget_bytes: u64,
    pub local_usage_bytes: u64,
    pub vram_valid: bool,
    pub memory_lists_valid: bool,
    pub is_uma_gpu: bool,
    pub allow_ram_purge: bool,
    pub min_free_ram_mb: u64,
    pub min_standby_mb: u64,
    pub cooldown_seconds: u32,
    pub elapsed_since_last_purge_ms: i64, // < 0 means never purged yet
    pub game_disk_read_bytes_per_sec: u64,
    pub max_game_disk_read_bytes_per_sec: u64,
    pub pressure_threshold_override: f64, // <= 0.0 -> use DEFAULT_VRAM_PRESSURE_THRESHOLD
    pub prev_interventions: u32,
    pub dry_run: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VfMemoryPurgeDecision {
    pub should_purge_standby: bool,
    pub should_trim_background: bool,
    pub skipped_due_to_cooldown: bool,
    pub skipped_due_to_disk_io: bool,
    pub cooldown_remaining_seconds: u32,
    pub guard_triggered: bool,
    pub dxgi_over_ceiling: bool,
    pub free_ram_critical: bool,
    pub dxgi_budget_utilization: f64,
    pub available_os_headroom_mb: u64,
    pub total_interventions: u32,
}

pub fn evaluate_memory_purge(input: &VfMemoryPurgeInput) -> VfMemoryPurgeDecision {
    let available_os_headroom_mb = if input.memory_lists_valid {
        input.free_and_zero_bytes.saturating_add(input.standby_bytes) / (1024 * 1024)
    } else {
        0
    };

    let dxgi_budget_utilization = if input.vram_valid && input.local_budget_bytes > 0 {
        (input.local_usage_bytes as f64) / (input.local_budget_bytes as f64)
    } else {
        0.0
    };

    let threshold = if input.pressure_threshold_override > 0.0 {
        input.pressure_threshold_override
    } else {
        DEFAULT_VRAM_PRESSURE_THRESHOLD
    };

    if input.total_physical_bytes == 0 || !input.memory_lists_valid || !input.allow_ram_purge {
        return VfMemoryPurgeDecision {
            should_purge_standby: false,
            should_trim_background: false,
            skipped_due_to_cooldown: false,
            skipped_due_to_disk_io: false,
            cooldown_remaining_seconds: 0,
            guard_triggered: false,
            dxgi_over_ceiling: false,
            free_ram_critical: false,
            dxgi_budget_utilization,
            available_os_headroom_mb,
            total_interventions: input.prev_interventions,
        };
    }

    let free_mb = input.free_and_zero_bytes / (1024 * 1024);
    let standby_mb = input.standby_bytes / (1024 * 1024);

    let dxgi_over_ceiling = input.is_uma_gpu
        && input.vram_valid
        && input.local_budget_bytes > 0
        && dxgi_budget_utilization >= threshold;

    let free_ram_critical = input.is_uma_gpu && free_mb < FREE_RAM_CRITICAL_MB;
    let guard_triggered = dxgi_over_ceiling || free_ram_critical;

    let low_free_ram = free_mb < input.min_free_ram_mb;
    let enough_standby = standby_mb >= input.min_standby_mb;
    let standby_purge_wanted = (low_free_ram || dxgi_over_ceiling) && enough_standby;
    let any_action_wanted = standby_purge_wanted || guard_triggered;

    let total_interventions = if guard_triggered {
        input.prev_interventions.saturating_add(1)
    } else {
        input.prev_interventions
    };

    if !any_action_wanted {
        return VfMemoryPurgeDecision {
            should_purge_standby: false,
            should_trim_background: false,
            skipped_due_to_cooldown: false,
            skipped_due_to_disk_io: false,
            cooldown_remaining_seconds: 0,
            guard_triggered,
            dxgi_over_ceiling,
            free_ram_critical,
            dxgi_budget_utilization,
            available_os_headroom_mb,
            total_interventions,
        };
    }

    // Shared cooldown across BOTH standby purge and UMA pressure guard trims.
    let cooldown_ms = (input.cooldown_seconds as i64).saturating_mul(1000);
    if cooldown_ms > 0 && input.elapsed_since_last_purge_ms >= 0 && input.elapsed_since_last_purge_ms < cooldown_ms {
        let remaining_ms = cooldown_ms - input.elapsed_since_last_purge_ms;
        let remaining_s = (remaining_ms / 1000) as u32;
        return VfMemoryPurgeDecision {
            should_purge_standby: false,
            should_trim_background: false,
            skipped_due_to_cooldown: true,
            skipped_due_to_disk_io: false,
            cooldown_remaining_seconds: remaining_s,
            guard_triggered,
            dxgi_over_ceiling,
            free_ram_critical,
            dxgi_budget_utilization,
            available_os_headroom_mb,
            total_interventions,
        };
    }

    // Disk I/O read peak gating (measured on every tick in C++, evaluated here).
    if input.max_game_disk_read_bytes_per_sec > 0
        && input.game_disk_read_bytes_per_sec > input.max_game_disk_read_bytes_per_sec
    {
        return VfMemoryPurgeDecision {
            should_purge_standby: false,
            should_trim_background: false,
            skipped_due_to_cooldown: false,
            skipped_due_to_disk_io: true,
            cooldown_remaining_seconds: 0,
            guard_triggered,
            dxgi_over_ceiling,
            free_ram_critical,
            dxgi_budget_utilization,
            available_os_headroom_mb,
            total_interventions,
        };
    }

    VfMemoryPurgeDecision {
        should_purge_standby: standby_purge_wanted,
        should_trim_background: any_action_wanted,
        skipped_due_to_cooldown: false,
        skipped_due_to_disk_io: false,
        cooldown_remaining_seconds: 0,
        guard_triggered,
        dxgi_over_ceiling,
        free_ram_critical,
        dxgi_budget_utilization,
        available_os_headroom_mb,
        total_interventions,
    }
}

/// Factory "Do Not Trim" list for memory optimization (Phase 4):
/// Recording, streaming, and voice/call software must never have their working set trimmed,
/// even before Phase 5's user-configurable exclusions arrive.
pub fn is_factory_memory_protected_exe(exe_name_or_path: &str) -> bool {
    let trimmed = exe_name_or_path.trim();
    if trimmed.is_empty() {
        return true;
    }
    let basename = trimmed
        .rsplit(['\\', '/'])
        .next()
        .unwrap_or(trimmed)
        .to_ascii_lowercase();

    matches!(
        basename.as_str(),
        // Recording / streaming
        "obs64.exe"
            | "obs32.exe"
            | "obs.exe"
            | "streamlabs obs.exe"
            | "xsplit.core.exe"
            | "xsplit.broadcaster.exe"
            | "action.exe"
            | "nvidia share.exe"
            | "radeonsoftware.exe"
            // Calls & voice comms
            | "discord.exe"
            | "discordcanary.exe"
            | "discordptb.exe"
            | "teams.exe"
            | "ms-teams.exe"
            | "slack.exe"
            | "skype.exe"
            | "zoom.exe"
            | "mumble.exe"
            | "ts3client_win64.exe"
            | "ts3client_win32.exe"
            // Critical VegaFlow/OS identities
            | "vegaflow_daemon.exe"
            | "vegaflow_tests.exe"
            | "vegaflow.exe"
            | "system"
            | "csrss.exe"
            | "dwm.exe"
            | "lsass.exe"
            | "smss.exe"
            | "wininit.exe"
            | "services.exe"
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/memory_purge.tsv");

    #[test]
    fn shared_vectors_match() {
        let mut count = 0usize;
        for raw in VECTORS.lines() {
            let line = raw.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let c: Vec<&str> = line.split('\t').collect();
            assert_eq!(c.len(), 16, "bad vector row: {line}");

            let mb = 1024u64 * 1024u64;
            let elapsed_s: i64 = c[13].parse().unwrap();
            let input = VfMemoryPurgeInput {
                total_physical_bytes: c[1].parse::<u64>().unwrap() * mb,
                free_and_zero_bytes: c[2].parse::<u64>().unwrap() * mb,
                standby_bytes: c[3].parse::<u64>().unwrap() * mb,
                local_budget_bytes: c[4].parse::<u64>().unwrap() * mb,
                local_usage_bytes: c[5].parse::<u64>().unwrap() * mb,
                vram_valid: c[6] == "1",
                memory_lists_valid: c[7] == "1",
                is_uma_gpu: c[8] == "1",
                allow_ram_purge: true,
                min_free_ram_mb: c[9].parse().unwrap(),
                min_standby_mb: c[10].parse().unwrap(),
                game_disk_read_bytes_per_sec: c[11].parse::<u64>().unwrap() * mb,
                max_game_disk_read_bytes_per_sec: c[12].parse::<u64>().unwrap() * mb,
                elapsed_since_last_purge_ms: if elapsed_s < 0 { -1 } else { elapsed_s * 1000 },
                cooldown_seconds: c[14].parse().unwrap(),
                pressure_threshold_override: 0.0,
                prev_interventions: 0,
                dry_run: false,
            };

            let dec = evaluate_memory_purge(&input);
            match c[15] {
                "NONE" => {
                    assert!(!dec.should_purge_standby, "{}", c[0]);
                    assert!(!dec.should_trim_background, "{}", c[0]);
                    assert!(!dec.skipped_due_to_cooldown, "{}", c[0]);
                    assert!(!dec.skipped_due_to_disk_io, "{}", c[0]);
                }
                "INVALID" => {
                    assert!(!dec.should_purge_standby, "{}", c[0]);
                    assert!(!dec.should_trim_background, "{}", c[0]);
                    assert!(!dec.guard_triggered, "{}", c[0]);
                }
                "PURGE_AND_TRIM" => {
                    assert!(dec.should_purge_standby, "{}", c[0]);
                    assert!(dec.should_trim_background, "{}", c[0]);
                }
                "UMA_TRIM_ONLY" => {
                    assert!(!dec.should_purge_standby, "{}", c[0]);
                    assert!(dec.should_trim_background, "{}", c[0]);
                    assert!(dec.guard_triggered, "{}", c[0]);
                }
                "COOLDOWN" => {
                    assert!(!dec.should_purge_standby, "{}", c[0]);
                    assert!(!dec.should_trim_background, "{}", c[0]);
                    assert!(dec.skipped_due_to_cooldown, "{}", c[0]);
                }
                "DISK_IO" => {
                    assert!(!dec.should_purge_standby, "{}", c[0]);
                    assert!(!dec.should_trim_background, "{}", c[0]);
                    assert!(dec.skipped_due_to_disk_io, "{}", c[0]);
                }
                other => panic!("unknown expected outcome {other}"),
            }
            count += 1;
        }
        assert!(count >= 18);
    }

    #[test]
    fn factory_do_not_trim_protects_recording_and_calls() {
        for exe in [
            "obs64.exe",
            "C:\\Program Files\\obs-studio\\bin\\64bit\\OBS64.EXE",
            "Discord.exe",
            "C:\\Users\\user\\AppData\\Local\\Discord\\app-1.0\\Discord.exe",
            "ms-teams.exe",
            "Slack.exe",
            "Zoom.exe",
        ] {
            assert!(is_factory_memory_protected_exe(exe), "must protect {exe}");
        }
        for exe in ["chrome.exe", "msedge.exe", "steamwebhelper.exe", "onedrive.exe"] {
            assert!(!is_factory_memory_protected_exe(exe), "should not protect {exe}");
        }
    }

    proptest! {
        #[test]
        fn cooldown_never_allows_second_action_before_elapsed(
            elapsed_ms in 0i64..89_999i64,
            free_mb in 0u64..1000u64,
            standby_mb in 1500u64..8000u64,
        ) {
            let mb = 1024 * 1024;
            let dec = evaluate_memory_purge(&VfMemoryPurgeInput {
                total_physical_bytes: 16384 * mb,
                free_and_zero_bytes: free_mb * mb,
                standby_bytes: standby_mb * mb,
                local_budget_bytes: 4096 * mb,
                local_usage_bytes: 3800 * mb,
                vram_valid: true,
                memory_lists_valid: true,
                is_uma_gpu: true,
                allow_ram_purge: true,
                min_free_ram_mb: 1200,
                min_standby_mb: 1500,
                cooldown_seconds: 90,
                elapsed_since_last_purge_ms: elapsed_ms,
                game_disk_read_bytes_per_sec: 0,
                max_game_disk_read_bytes_per_sec: 15 * mb,
                pressure_threshold_override: 0.0,
                prev_interventions: 0,
                dry_run: false,
            });
            prop_assert!(!dec.should_purge_standby);
            prop_assert!(!dec.should_trim_background);
            prop_assert!(dec.skipped_due_to_cooldown);
        }
    }
}
