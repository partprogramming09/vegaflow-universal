#![allow(clippy::not_unsafe_ptr_arg_deref)]

// The exported functions catch panics with `ffi_guard`, which only works when panics unwind. With
// panic = "abort" a bug in the core would kill the whole daemon, so refuse to build that way.
#[cfg(not(panic = "unwind"))]
compile_error!("vegaflow-core must be built with panic = \"unwind\" so ffi_guard can catch panics at the C boundary");

/// Runs `f` for an `extern "C"` function and returns `fallback` if it panics. A panic must never cross into C++ (that is
/// undefined behaviour) and must not take the daemon down; every fallback is the cautious answer for its function
/// (deny, assume anti-cheat present, do not confine, do not purge).
pub(crate) fn ffi_guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or(fallback)
}

pub mod apu_tier;
pub mod cpu_governor;
pub mod path_policy;
pub mod sandbox_core;
pub mod uma_memory;

pub use apu_tier::*;
pub use cpu_governor::*;
pub use path_policy::*;
pub use sandbox_core::*;
pub use uma_memory::*;

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CString;

    #[test]
    fn ffi_guard_returns_the_fallback_when_the_body_panics() {
        assert_eq!(ffi_guard(7u32, || -> u32 { panic!("simulated bug") }), 7);
    }

    #[test]
    fn ffi_guard_passes_the_value_through_when_nothing_goes_wrong() {
        assert_eq!(ffi_guard(7u32, || 42u32), 42);
    }

    #[test]
    fn test_uma_healthy_telemetry() {
        let input = VfUmaTelemetryInput {
            total_physical_bytes: 16 * 1024 * 1024 * 1024,
            free_and_zero_bytes: 4096 * 1024 * 1024,
            standby_bytes: 2048 * 1024 * 1024,
            local_budget_bytes: 2048 * 1024 * 1024,
            local_usage_bytes: 983 * 1024 * 1024, // ~48%
            runtime_pressure_threshold: 0.85,
            vram_valid: true,
            is_discrete_tier: false,
        };

        let decision = vf_rust_evaluate_uma_pressure(&input, 10);
        assert!(!decision.guard_triggered);
        assert!(!decision.dxgi_over_ceiling);
        assert!(!decision.free_ram_critical);
        assert_eq!(decision.available_os_headroom_mb, 6144);
        assert_eq!(decision.total_interventions, 10);
        assert!((decision.dxgi_budget_utilization - 0.48).abs() < 0.01);
    }

    #[test]
    fn test_uma_high_dxgi_pressure() {
        let input = VfUmaTelemetryInput {
            total_physical_bytes: 16 * 1024 * 1024 * 1024,
            free_and_zero_bytes: 3000 * 1024 * 1024,
            standby_bytes: 1024 * 1024 * 1024,
            local_budget_bytes: 2048 * 1024 * 1024,
            local_usage_bytes: 1843 * 1024 * 1024, // 90%
            runtime_pressure_threshold: 0.85,
            vram_valid: true,
            is_discrete_tier: false,
        };

        let decision = vf_rust_evaluate_uma_pressure(&input, 4);
        assert!(decision.guard_triggered);
        assert!(decision.dxgi_over_ceiling);
        assert!(!decision.free_ram_critical);
        assert_eq!(decision.total_interventions, 5);
        assert!((decision.dxgi_budget_utilization - 0.90).abs() < 0.01);
    }

    #[test]
    fn test_uma_custom_pressure_threshold() {
        let input = VfUmaTelemetryInput {
            total_physical_bytes: 16 * 1024 * 1024 * 1024,
            free_and_zero_bytes: 4096 * 1024 * 1024,
            standby_bytes: 1024 * 1024 * 1024,
            local_budget_bytes: 1000 * 1024 * 1024,
            local_usage_bytes: 750 * 1024 * 1024, // 75%
            runtime_pressure_threshold: 0.70, // custom lower threshold
            vram_valid: true,
            is_discrete_tier: false,
        };

        let decision = vf_rust_evaluate_uma_pressure(&input, 0);
        assert!(decision.guard_triggered);
        assert!(decision.dxgi_over_ceiling);
        assert_eq!(decision.total_interventions, 1);
    }

    #[test]
    fn test_uma_critical_low_free_ram() {
        let input = VfUmaTelemetryInput {
            total_physical_bytes: 16 * 1024 * 1024 * 1024,
            free_and_zero_bytes: 1024 * 1024 * 1024, // 1024 MB < 1536 MB threshold
            standby_bytes: 512 * 1024 * 1024,
            local_budget_bytes: 2048 * 1024 * 1024,
            local_usage_bytes: 800 * 1024 * 1024, // ~39% (low DXGI)
            runtime_pressure_threshold: 0.85,
            vram_valid: true,
            is_discrete_tier: false,
        };

        let decision = vf_rust_evaluate_uma_pressure(&input, 2);
        assert!(decision.guard_triggered);
        assert!(!decision.dxgi_over_ceiling);
        assert!(decision.free_ram_critical);
        assert_eq!(decision.total_interventions, 3);
    }

    #[test]
    fn test_uma_discrete_gpu_passthrough() {
        let input = VfUmaTelemetryInput {
            total_physical_bytes: 16 * 1024 * 1024 * 1024,
            free_and_zero_bytes: 512 * 1024 * 1024, // Low RAM
            standby_bytes: 512 * 1024 * 1024,
            local_budget_bytes: 8192 * 1024 * 1024,
            local_usage_bytes: 7500 * 1024 * 1024, // >90% VRAM
            runtime_pressure_threshold: 0.85,
            vram_valid: true,
            is_discrete_tier: true, // Discrete GPU should not trigger UMA guard
        };

        let decision = vf_rust_evaluate_uma_pressure(&input, 7);
        assert!(!decision.guard_triggered);
        assert_eq!(decision.total_interventions, 7);
    }

    #[test]
    fn test_uma_null_input() {
        let decision = vf_rust_evaluate_uma_pressure(std::ptr::null(), 12);
        assert!(!decision.guard_triggered);
        assert_eq!(decision.total_interventions, 12);
        assert_eq!(decision.available_os_headroom_mb, 0);
    }

    #[test]
    fn test_nt_memory_lists_query_and_purge() {
        let sample = vf_rust_query_nt_memory_lists();
        if sample.valid {
            assert!(sample.free_and_zero_bytes > 0);
        }

        let purge_dry = vf_rust_purge_nt_standby_list(true);
        assert!(purge_dry, "Dry run purge must always succeed");
    }

    #[cfg(not(windows))]
    #[test]
    fn test_nt_memory_calls_report_failure_without_ntdll() {
        // There is no ntdll outside Windows: the query must say "invalid" and a real purge must fail.
        assert!(!vf_rust_query_nt_memory_lists().valid);
        assert!(!vf_rust_purge_nt_standby_list(false));
    }

    #[test]
    fn test_topology_symmetric_ryzen_4600h() {
        // Ryzen 5 4600H: 6C/12T, 2 CCX (Cores 0..2 on LLC 0, Cores 3..5 on LLC 1)
        let mut entries = [VfCpuCoreEntry {
            cpu_set_id: 0,
            core_index: 0,
            logical_processor_index: 0,
            last_level_cache_index: 0,
            efficiency_class: 0,
        }; 12];

        for i in 0..12 {
            let core = (i / 2) as u32;
            let llc = if core < 3 { 0 } else { 1 };
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: core,
                logical_processor_index: i as u32,
                last_level_cache_index: llc,
                efficiency_class: 0,
            };
        }

        let mut out = VfTopologyPartitionOut::default();
        let ok = vf_rust_partition_cpu_topology(entries.as_ptr(), 12, &mut out);
        assert!(ok);
        assert_eq!(out.physical_cores, 6);
        assert_eq!(out.logical_processors, 12);
        assert!(out.has_multiple_ccx);
        assert!(!out.has_hybrid_cores);

        // Core 5 (threads 10, 11 -> cpu_sets 266, 267) goes to background
        assert_eq!(out.background_count, 2);
        assert_eq!(out.background_cpu_set_ids[0], 266);
        assert_eq!(out.background_cpu_set_ids[1], 267);

        // Core 0 (threads 0, 1 -> 256, 257) is excluded as DPC/ISR shield
        // Remaining 8 threads: Cores 1, 2 on CCX 0 prioritized first, then Cores 3, 4 on CCX 1
        assert_eq!(out.game_count, 8);
        assert_eq!(out.game_cpu_set_ids[0], 258);
        assert_eq!(out.game_cpu_set_ids[1], 259);
        assert_eq!(out.game_cpu_set_ids[2], 260);
        assert_eq!(out.game_cpu_set_ids[3], 261);
        assert_eq!(out.game_cpu_set_ids[4], 262);
        assert_eq!(out.game_cpu_set_ids[5], 263);
        assert_eq!(out.game_cpu_set_ids[6], 264);
        assert_eq!(out.game_cpu_set_ids[7], 265);
    }

    #[test]
    fn test_topology_asymmetric_zen4_zen4c() {
        // 4C/8T Zen 4 on LLC 0 (Cores 0..3), 2C/4T Zen 4c on LLC 1 (Cores 4..5)
        let mut entries = [VfCpuCoreEntry {
            cpu_set_id: 0,
            core_index: 0,
            logical_processor_index: 0,
            last_level_cache_index: 0,
            efficiency_class: 0,
        }; 12];

        for i in 0..8 {
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: (i / 2) as u32,
                logical_processor_index: i as u32,
                last_level_cache_index: 0,
                efficiency_class: 0,
            };
        }
        for i in 8..12 {
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: 4 + ((i - 8) / 2) as u32,
                logical_processor_index: i as u32,
                last_level_cache_index: 1,
                efficiency_class: 0,
            };
        }

        let mut out = VfTopologyPartitionOut::default();
        let ok = vf_rust_partition_cpu_topology(entries.as_ptr(), 12, &mut out);
        assert!(ok);
        assert_eq!(out.physical_cores, 6);
        assert_eq!(out.logical_processors, 12);
        assert!(out.has_multiple_ccx);

        // LLC 1 has 4 threads <= 12 / 3 -> secondary compact island -> all 4 threads go to bg
        assert_eq!(out.background_count, 4);
        assert_eq!(&out.background_cpu_set_ids[0..4], &[264, 265, 266, 267]);

        // Core 0 excluded from game sets (physical >= 4), leaving Cores 1, 2, 3 (6 threads: 258..263)
        assert_eq!(out.game_count, 6);
        assert_eq!(&out.game_cpu_set_ids[0..6], &[258, 259, 260, 261, 262, 263]);
    }

    #[test]
    fn test_topology_monolithic_scaling_rules() {
        // 1. 4C/4T monolithic: cores 0..3, 1 thread each
        let entries_4c4t: Vec<VfCpuCoreEntry> = (0..4)
            .map(|i| VfCpuCoreEntry {
                cpu_set_id: 100 + i,
                core_index: i,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0,
            })
            .collect();
        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries_4c4t.as_ptr(), 4, &mut out));
        assert_eq!(out.physical_cores, 4);
        assert_eq!(out.background_count, 1); // 4 >= 4 cores, threads_per_core = 1
        assert_eq!(out.background_cpu_set_ids[0], 103);
        assert_eq!(out.game_count, 2); // core 0 excluded, core 3 background -> cores 1, 2 for game

        // 2. 2C/4T monolithic: cores 0, 1, 2 threads each
        let entries_2c4t: Vec<VfCpuCoreEntry> = (0..4)
            .map(|i| VfCpuCoreEntry {
                cpu_set_id: 100 + i,
                core_index: i / 2,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0,
            })
            .collect();
        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries_2c4t.as_ptr(), 4, &mut out));
        assert_eq!(out.physical_cores, 2);
        assert_eq!(out.background_count, 1); // physical_cores < 4 -> 1 thread
        assert_eq!(out.background_cpu_set_ids[0], 102);
        assert_eq!(out.game_count, 3); // physical_cores < 4 -> core 0 NOT excluded -> threads 100, 101, 103

        // 3. 8C/16T monolithic: cores 0..7, 2 threads each
        let entries_8c16t: Vec<VfCpuCoreEntry> = (0..16)
            .map(|i| VfCpuCoreEntry {
                cpu_set_id: 200 + i,
                core_index: i / 2,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0,
            })
            .collect();
        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries_8c16t.as_ptr(), 16, &mut out));
        assert_eq!(out.physical_cores, 8);
        assert_eq!(out.background_count, 2); // Core 7 (threads 14, 15 -> 214, 215)
        assert_eq!(out.game_count, 12); // Core 0 excluded (2 threads), Core 7 in bg (2 threads) -> 12 game threads

        // 4. 4C/8T monolithic: cores 0..3, 2 threads each
        let entries_4c8t: Vec<VfCpuCoreEntry> = (0..8)
            .map(|i| VfCpuCoreEntry {
                cpu_set_id: 300 + i,
                core_index: i / 2,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0,
            })
            .collect();
        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries_4c8t.as_ptr(), 8, &mut out));
        assert_eq!(out.physical_cores, 4);
        assert_eq!(out.background_count, 2); // Core 3
        assert_eq!(out.game_count, 4); // Core 0 excluded -> Cores 1, 2 for game

        // 5. 6C/6T monolithic: cores 0..5, 1 thread each
        let entries_6c6t: Vec<VfCpuCoreEntry> = (0..6)
            .map(|i| VfCpuCoreEntry {
                cpu_set_id: 400 + i,
                core_index: i,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0,
            })
            .collect();
        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries_6c6t.as_ptr(), 6, &mut out));
        assert_eq!(out.physical_cores, 6);
        assert_eq!(out.background_count, 1);
        assert_eq!(out.game_count, 4); // Core 0 excluded, Core 5 in bg -> Cores 1..4
    }

    #[test]
    fn test_topology_hybrid_cores() {
        // 2 P-Cores (eff=1, cores 0, 1) and 4 E-Cores (eff=0, cores 2..5)
        let mut entries = Vec::new();
        for i in 0..2 {
            entries.push(VfCpuCoreEntry {
                cpu_set_id: 500 + i,
                core_index: i,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 1, // P-Core
            });
        }
        for i in 2..6 {
            entries.push(VfCpuCoreEntry {
                cpu_set_id: 500 + i,
                core_index: i,
                logical_processor_index: i,
                last_level_cache_index: 0,
                efficiency_class: 0, // E-Core
            });
        }

        let mut out = VfTopologyPartitionOut::default();
        assert!(vf_rust_partition_cpu_topology(entries.as_ptr(), 6, &mut out));
        assert!(out.has_hybrid_cores);
        assert_eq!(out.game_count, 2);
        assert_eq!(&out.game_cpu_set_ids[0..2], &[500, 501]);
        assert_eq!(out.background_count, 4);
        assert_eq!(&out.background_cpu_set_ids[0..4], &[502, 503, 504, 505]);
    }

    #[test]
    fn test_topology_invalid_inputs() {
        let mut out = VfTopologyPartitionOut::default();
        assert!(!vf_rust_partition_cpu_topology(std::ptr::null(), 4, &mut out));
        assert!(!vf_rust_partition_cpu_topology(out.game_cpu_set_ids.as_ptr() as *const _, 0, &mut out));
        assert!(!vf_rust_partition_cpu_topology(out.game_cpu_set_ids.as_ptr() as *const _, 65, &mut out));
        assert!(!vf_rust_partition_cpu_topology(out.game_cpu_set_ids.as_ptr() as *const _, 4, std::ptr::null_mut()));
    }

    #[test]
    fn test_background_confinement_hysteresis() {
        // Not currently confined: triggers at >= 0.50
        assert!(!vf_rust_eval_bg_confinement(0.23, false));
        assert!(!vf_rust_eval_bg_confinement(0.49, false));
        assert!(vf_rust_eval_bg_confinement(0.50, false));
        assert!(vf_rust_eval_bg_confinement(0.75, false));

        // Currently confined: stays confined until < 0.25
        assert!(vf_rust_eval_bg_confinement(0.50, true));
        assert!(vf_rust_eval_bg_confinement(0.35, true));
        assert!(vf_rust_eval_bg_confinement(0.25, true));
        assert!(!vf_rust_eval_bg_confinement(0.24, true));
        assert!(!vf_rust_eval_bg_confinement(0.10, true));
    }

    #[test]
    fn test_anticheat_signature_detection() {
        let positive_paths = [
            "C:\\Games\\Genshin Impact\\GenshinImpact.exe",
            "C:\\Games\\ZenlessZoneZero\\ZenlessZoneZero.exe",
            "C:\\Program Files\\AntiCheat\\EasyAntiCheat_EOS.dll",
            "C:\\Windows\\System32\\drivers\\HoYoKProtect.sys",
            "C:\\Riot Games\\VALORANT\\live\\vgk.sys",
            "D:\\BattlEye\\beservice.exe",
            "D:\\Games\\AceBase\\acebase.dll",
            "C:\\Test\\randgrid.sys",
            "C:\\Test\\AntiCheatExpert\\sample.dll",
            "C:\\Test\\mhyprot.sys",
            "C:\\Test\\mhypbase.dll",
            "C:\\Test\\BlackCipher.aes",
            "C:\\Test\\equ8.dll",
            "C:\\Test\\nprotect.sys",
            "C:\\Test\\gameguard.des",
            "C:\\Test\\xigncode.va",
        ];

        for path in positive_paths {
            let cstr = CString::new(path).unwrap();
            assert!(
                vf_rust_has_anticheat_signature(cstr.as_ptr(), path.len()),
                "Expected signature match for: {}",
                path
            );
        }

        let negative_paths = [
            "C:\\Games\\IndieGame\\Engine.ini",
            "C:\\Games\\Skyrim\\SkyrimSE.exe",
            "C:\\Program Files\\Steam\\steamapps\\common\\Half-Life 2\\hl2.exe",
            "C:\\dev\\vegaflow\\vegaflow_daemon.exe",
        ];

        for path in negative_paths {
            let cstr = CString::new(path).unwrap();
            assert!(
                !vf_rust_has_anticheat_signature(cstr.as_ptr(), path.len()),
                "Expected clean result for: {}",
                path
            );
        }

        // Null pointer and 0 length checks
        assert!(!vf_rust_has_anticheat_signature(std::ptr::null(), 10));
        let empty = CString::new("").unwrap();
        assert!(!vf_rust_has_anticheat_signature(empty.as_ptr(), 0));
    }

    #[test]
    fn test_critical_system_filenames() {
        let critical = [
            "bootmgr",
            "BOOTMGR",
            "pagefile.sys",
            "PAGEFILE.SYS",
            "hiberfil.sys",
            "swapfile.sys",
            "ntldr",
            "bootsect.bak",
        ];

        for name in critical {
            let cstr = CString::new(name).unwrap();
            assert!(
                vf_rust_is_critical_system_filename(cstr.as_ptr(), name.len()),
                "Expected critical recognition for: {}",
                name
            );
        }

        let non_critical = [
            "explorer.exe",
            "vegaflow.exe",
            "system.dat",
            "user.ini",
        ];

        for name in non_critical {
            let cstr = CString::new(name).unwrap();
            assert!(
                !vf_rust_is_critical_system_filename(cstr.as_ptr(), name.len()),
                "Expected non-critical for: {}",
                name
            );
        }

        assert!(!vf_rust_is_critical_system_filename(std::ptr::null(), 5));
        let empty = CString::new("").unwrap();
        assert!(!vf_rust_is_critical_system_filename(empty.as_ptr(), 0));
    }
}
