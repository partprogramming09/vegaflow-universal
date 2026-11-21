#![allow(clippy::not_unsafe_ptr_arg_deref)]

// The exported functions catch panics with `ffi_guard`, which only works when panics unwind. With
// panic = "abort" a bug in the core would kill the whole daemon, so refuse to build that way.
#[cfg(not(panic = "unwind"))]
compile_error!("vegaflow-core must be built with panic = \"unwind\" so ffi_guard can catch panics at the C boundary");

pub mod ffi;
pub mod policy;

pub use ffi::*;

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::ffi_guard;
    use crate::policy::{cpu_governor::*, memory_purge::VfMemoryPurgeInput};
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
    fn test_memory_purge_ffi_and_threshold() {
        assert!((vf_rust_vram_pressure_threshold() - 0.85).abs() < 1e-9);

        let mb = 1024u64 * 1024u64;
        let input = VfMemoryPurgeInput {
            total_physical_bytes: 16384 * mb,
            free_and_zero_bytes: 3000 * mb,
            standby_bytes: 2048 * mb,
            local_budget_bytes: 4096 * mb,
            gpu_shared_usage_bytes: 3686 * mb, // ~90%
            gpu_dedicated_usage_bytes: 0,
            last_purge_level: 0,
            vram_valid: true,
            memory_lists_valid: true,
            is_uma_gpu: true,
            allow_ram_purge: true,
            min_free_ram_mb: 1200,
            min_standby_mb: 1500,
            cooldown_seconds: 90,
            elapsed_since_last_purge_ms: -1,
            game_disk_read_bytes_per_sec: 0,
            max_game_disk_read_bytes_per_sec: 15 * mb,
            pressure_threshold_override: 0.0,
            prev_interventions: 2,
            dry_run: false,
        };
        let dec = vf_rust_evaluate_memory_purge(&input);
        assert_eq!(dec.purge_level, 1);
        assert!(dec.should_trim_background);
        assert!(dec.guard_triggered);
        assert!(dec.dxgi_over_ceiling);
        assert_eq!(dec.total_interventions, 3);

        let null_dec = vf_rust_evaluate_memory_purge(std::ptr::null());
        assert_eq!(null_dec.purge_level, 0);
        assert!(!null_dec.should_trim_background);
        assert!(!null_dec.guard_triggered);

        let obs = CString::new("C:\\Program Files\\obs64.exe").unwrap();
        assert!(vf_rust_is_factory_memory_protected_exe(obs.as_ptr(), obs.as_bytes().len()));
        let chrome = CString::new("chrome.exe").unwrap();
        assert!(!vf_rust_is_factory_memory_protected_exe(chrome.as_ptr(), chrome.as_bytes().len()));
        assert!(vf_rust_is_factory_memory_protected_exe(std::ptr::null(), 5));
    }

    #[test]
    fn test_topology_symmetric_ryzen_4600h() {
        // Ryzen 5 4600H: 6C/12T, 2 CCX (Cores 0..2 on LLC 0, Cores 3..5 on LLC 1)
        let mut entries = [VfCpuCoreEntry::default(); 12];

        for i in 0..12 {
            let core = (i / 2) as u32;
            let llc = if core < 3 { 0 } else { 1 };
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: core,
                logical_processor_index: i as u32,
                group: 0,
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
        let mut entries = [VfCpuCoreEntry::default(); 12];

        for i in 0..8 {
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: (i / 2) as u32,
                logical_processor_index: i as u32,
                group: 0,
                last_level_cache_index: 0,
                efficiency_class: 0,
            };
        }
        for i in 8..12 {
            entries[i] = VfCpuCoreEntry {
                cpu_set_id: 256 + i as u32,
                core_index: 4 + ((i - 8) / 2) as u32,
                logical_processor_index: i as u32,
                group: 0,
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
                group: 0,
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
                group: 0,
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
                group: 0,
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
                group: 0,
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
                group: 0,
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
                group: 0,
                last_level_cache_index: 0,
                efficiency_class: 1, // P-Core
            });
        }
        for i in 2..6 {
            entries.push(VfCpuCoreEntry {
                cpu_set_id: 500 + i,
                core_index: i,
                logical_processor_index: i,
                group: 0,
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
        let too_many_single_group = [VfCpuCoreEntry::default(); 65];
        assert!(!vf_rust_partition_cpu_topology(std::ptr::null(), 4, &mut out));
        assert!(!vf_rust_partition_cpu_topology(too_many_single_group.as_ptr(), 0, &mut out));
        assert!(!vf_rust_partition_cpu_topology(too_many_single_group.as_ptr(), 65, &mut out));
        assert!(!vf_rust_partition_cpu_topology(too_many_single_group.as_ptr(), 4, std::ptr::null_mut()));
        assert!(!vf_rust_partition_cpu_topology_group(too_many_single_group.as_ptr(), 65, 0, &mut out));
        assert!(!vf_rust_partition_cpu_topology_group(too_many_single_group.as_ptr(), 4, 1, &mut out));
    }

    #[test]
    fn test_topology_shared_tsv_vectors() {
        let manifest_dir = env!("CARGO_MANIFEST_DIR");
        let path = format!("{}/../../tests/vectors/cpu_topology.tsv", manifest_dir);
        let text = std::fs::read_to_string(&path).expect("read tests/vectors/cpu_topology.tsv");

        fn parse_id_list(spec: &str) -> Vec<u32> {
            let mut out = Vec::new();
            if spec == "-" || spec.is_empty() {
                return out;
            }
            for part in spec.split(',') {
                if let Some((a, b)) = part.split_once("..") {
                    let start: u32 = a.parse().unwrap();
                    let end: u32 = b.parse().unwrap();
                    for id in start..=end {
                        out.push(id);
                    }
                } else {
                    out.push(part.parse().unwrap());
                }
            }
            out
        }

        let mut tested = 0usize;
        for (line_no, raw) in text.lines().enumerate() {
            let line = raw.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let cols: Vec<&str> = line.split('\t').collect();
            assert_eq!(cols.len(), 9, "line {}: expected 9 columns", line_no + 1);
            let name = cols[0];
            let specs = cols[1];
            let target_group: u16 = cols[2].parse().unwrap();
            let expected_game = parse_id_list(cols[3]);
            let expected_bg = parse_id_list(cols[4]);
            let expected_phys: u32 = cols[5].parse().unwrap();
            let expected_logical: u32 = cols[6].parse().unwrap();
            let expected_multi_ccx = cols[7] == "1";
            let expected_hybrid = cols[8] == "1";

            // Spec item: core_count@group:smt:llc:eff:start_core:start_cpu_id
            let mut entries = Vec::new();
            let mut logical_idx_per_group = [0u32; 8];
            for item in specs.split(';') {
                let (cnt_str, rest) = item.split_once('@').unwrap();
                let core_count: u32 = cnt_str.parse().unwrap();
                let nums: Vec<u32> = rest.split(':').map(|s| s.parse().unwrap()).collect();
                assert_eq!(nums.len(), 6, "{name}: bad segment '{item}'");
                let group = nums[0] as u16;
                let smt = nums[1];
                let llc = nums[2] as u8;
                let eff = nums[3] as u8;
                let start_core = nums[4];
                let start_id = nums[5];
                for c in 0..core_count {
                    for t in 0..smt {
                        let lp = logical_idx_per_group[group as usize];
                        logical_idx_per_group[group as usize] += 1;
                        entries.push(VfCpuCoreEntry {
                            cpu_set_id: start_id + c * smt + t,
                            core_index: start_core + c,
                            logical_processor_index: lp,
                            group,
                            last_level_cache_index: llc,
                            efficiency_class: eff,
                        });
                    }
                }
            }

            let mut out = VfTopologyPartitionOut::default();
            let ok = vf_rust_partition_cpu_topology_group(
                entries.as_ptr(),
                entries.len() as u32,
                target_group,
                &mut out,
            );
            assert!(ok, "{name}: partition failed");
            assert_eq!(out.physical_cores, expected_phys, "{name}: physical_cores");
            assert_eq!(out.logical_processors, expected_logical, "{name}: logical_processors");
            assert_eq!(out.has_multiple_ccx, expected_multi_ccx, "{name}: has_multiple_ccx");
            assert_eq!(out.has_hybrid_cores, expected_hybrid, "{name}: has_hybrid_cores");
            assert_eq!(&out.game_cpu_set_ids[..out.game_count as usize], expected_game.as_slice(), "{name}: game_cpu_set_ids");
            assert_eq!(&out.background_cpu_set_ids[..out.background_count as usize], expected_bg.as_slice(), "{name}: background_cpu_set_ids");
            tested += 1;
        }
        assert_eq!(tested, 18);
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

    #[test]
    fn bridge_layout_with_a_null_buffer_only_reports_the_total() {
        let total = vf_rust_bridge_layout(std::ptr::null_mut(), 1000);
        assert!(total > 0);
    }

    #[test]
    fn bridge_layout_writes_at_most_cap_numbers() {
        let total = vf_rust_bridge_layout(std::ptr::null_mut(), 0);
        let mut buf = vec![usize::MAX; total + 4];
        assert_eq!(vf_rust_bridge_layout(buf.as_mut_ptr(), 3), total);
        assert!(buf[..3].iter().all(|&n| n != usize::MAX), "the first 3 numbers are written");
        assert!(buf[3..].iter().all(|&n| n == usize::MAX), "nothing is written past cap");
        assert_eq!(vf_rust_bridge_layout(buf.as_mut_ptr(), buf.len()), total);
        assert!(buf[total..].iter().all(|&n| n == usize::MAX), "nothing is written past the total");
    }
}
