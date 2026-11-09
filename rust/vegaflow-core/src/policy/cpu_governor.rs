pub const VF_MAX_LOGICAL_PROCESSORS: usize = 64;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VfCpuCoreEntry {
    pub cpu_set_id: u32,
    pub core_index: u32,
    pub logical_processor_index: u32,
    pub last_level_cache_index: u8,
    pub efficiency_class: u8,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VfTopologyPartitionOut {
    pub game_cpu_set_ids: [u32; VF_MAX_LOGICAL_PROCESSORS],
    pub game_count: u32,
    pub background_cpu_set_ids: [u32; VF_MAX_LOGICAL_PROCESSORS],
    pub background_count: u32,
    pub physical_cores: u32,
    pub logical_processors: u32,
    pub has_multiple_ccx: bool,
    pub has_hybrid_cores: bool,
}

impl Default for VfTopologyPartitionOut {
    fn default() -> Self {
        Self {
            game_cpu_set_ids: [0; VF_MAX_LOGICAL_PROCESSORS],
            game_count: 0,
            background_cpu_set_ids: [0; VF_MAX_LOGICAL_PROCESSORS],
            background_count: 0,
            physical_cores: 0,
            logical_processors: 0,
            has_multiple_ccx: false,
            has_hybrid_cores: false,
        }
    }
}

pub fn partition(entries_slice: &[VfCpuCoreEntry], out_ref: &mut VfTopologyPartitionOut) -> bool {
    *out_ref = VfTopologyPartitionOut::default();

    let mut physical_core_indices = [0u32; VF_MAX_LOGICAL_PROCESSORS];
    let mut physical_core_count = 0usize;

    let mut llc_indices = [0u8; VF_MAX_LOGICAL_PROCESSORS];
    let mut llc_indices_count = 0usize;

    let mut llc_counts = [0u32; 256];
    let mut min_eff: u8 = 255;
    let mut max_eff: u8 = 0;

    for entry in entries_slice {
        // Collect unique physical core indices
        if !physical_core_indices[..physical_core_count].contains(&entry.core_index) {
            physical_core_indices[physical_core_count] = entry.core_index;
            physical_core_count += 1;
        }

        // Collect unique LLC indices
        if !llc_indices[..llc_indices_count].contains(&entry.last_level_cache_index) {
            llc_indices[llc_indices_count] = entry.last_level_cache_index;
            llc_indices_count += 1;
        }

        llc_counts[entry.last_level_cache_index as usize] += 1;

        if entry.efficiency_class < min_eff {
            min_eff = entry.efficiency_class;
        }
        if entry.efficiency_class > max_eff {
            max_eff = entry.efficiency_class;
        }
    }

    // Sort unique collections to match std::set behavior
    physical_core_indices[..physical_core_count].sort_unstable();
    llc_indices[..llc_indices_count].sort_unstable();

    out_ref.logical_processors = entries_slice.len() as u32;
    out_ref.physical_cores = physical_core_count as u32;
    out_ref.has_multiple_ccx = llc_indices_count > 1;
    out_ref.has_hybrid_cores = min_eff != max_eff;

    let threads_per_core = (out_ref.logical_processors / out_ref.physical_cores.max(1)).max(1) as usize;
    let bg_limit = if out_ref.physical_cores >= 4 { threads_per_core } else { 1 };

    // 1. Hybrid topology (P-cores vs E-cores)
    if out_ref.has_hybrid_cores {
        let mut high_perf_cores = [0u32; VF_MAX_LOGICAL_PROCESSORS];
        let mut high_perf_count = 0usize;

        for entry in entries_slice {
            if entry.efficiency_class == max_eff
                && !high_perf_cores[..high_perf_count].contains(&entry.core_index)
            {
                high_perf_cores[high_perf_count] = entry.core_index;
                high_perf_count += 1;
            }
        }

        let exclude_core_zero = high_perf_count >= 4;

        for entry in entries_slice {
            if entry.efficiency_class == max_eff {
                if !exclude_core_zero || entry.core_index != 0 {
                    out_ref.game_cpu_set_ids[out_ref.game_count as usize] = entry.cpu_set_id;
                    out_ref.game_count += 1;
                }
            } else {
                out_ref.background_cpu_set_ids[out_ref.background_count as usize] = entry.cpu_set_id;
                out_ref.background_count += 1;
            }
        }
        return true;
    }

    // 2. Multiple CCX with 6+ physical cores (e.g. Ryzen 5 4600H or Zen 4 + Zen 4c)
    if out_ref.has_multiple_ccx && out_ref.physical_cores >= 6 {
        let mut min_count = u32::MAX;
        let mut max_count = 0u32;

        for &llc in &llc_indices[..llc_indices_count] {
            let cnt = llc_counts[llc as usize];
            if cnt < min_count { min_count = cnt; }
            if cnt > max_count { max_count = cnt; }
        }

        let mut secondary_llc = llc_indices[llc_indices_count - 1];
        if min_count != max_count {
            for &llc in llc_indices[..llc_indices_count].iter().rev() {
                if llc_counts[llc as usize] == min_count {
                    secondary_llc = llc;
                    break;
                }
            }
        }

        // Asymmetric compact island check (e.g. Zen 4c where secondary CCX has <= 33% threads)
        if llc_counts[secondary_llc as usize] <= (out_ref.logical_processors / 3) {
            let exclude_core_zero = out_ref.physical_cores >= 4;
            let mut candidate_game_sets = [VfCpuCoreEntry {
                cpu_set_id: 0,
                core_index: 0,
                logical_processor_index: 0,
                last_level_cache_index: 0,
                efficiency_class: 0,
            }; VF_MAX_LOGICAL_PROCESSORS];
            let mut candidate_count = 0usize;

            for entry in entries_slice {
                if entry.last_level_cache_index == secondary_llc {
                    out_ref.background_cpu_set_ids[out_ref.background_count as usize] = entry.cpu_set_id;
                    out_ref.background_count += 1;
                } else {
                    if !exclude_core_zero || entry.core_index != 0 {
                        candidate_game_sets[candidate_count] = *entry;
                        candidate_count += 1;
                    }
                }
            }

            candidate_game_sets[..candidate_count].sort_unstable_by(|a, b| {
                if a.last_level_cache_index != b.last_level_cache_index {
                    a.last_level_cache_index.cmp(&b.last_level_cache_index)
                } else if a.core_index != b.core_index {
                    a.core_index.cmp(&b.core_index)
                } else {
                    a.logical_processor_index.cmp(&b.logical_processor_index)
                }
            });

            for item in &candidate_game_sets[..candidate_count] {
                out_ref.game_cpu_set_ids[out_ref.game_count as usize] = item.cpu_set_id;
                out_ref.game_count += 1;
            }
            return true;
        }

        // Symmetric multiple CCX (e.g. 2 CCX of 3 cores / 6 threads in Ryzen 5 4600H)
        let last_core_index = physical_core_indices[physical_core_count - 1];
        let exclude_core_zero = out_ref.physical_cores >= 4;

        let mut candidate_game_sets = [VfCpuCoreEntry {
            cpu_set_id: 0,
            core_index: 0,
            logical_processor_index: 0,
            last_level_cache_index: 0,
            efficiency_class: 0,
        }; VF_MAX_LOGICAL_PROCESSORS];
        let mut candidate_count = 0usize;
        let mut bg_sets_per_llc = [0usize; 256];
        let mut bg_taken = 0usize;

        for entry in entries_slice {
            if entry.core_index == last_core_index && bg_taken < bg_limit {
                bg_taken += 1;
                out_ref.background_cpu_set_ids[out_ref.background_count as usize] = entry.cpu_set_id;
                out_ref.background_count += 1;
                bg_sets_per_llc[entry.last_level_cache_index as usize] += 1;
            } else {
                if !exclude_core_zero || entry.core_index != 0 {
                    candidate_game_sets[candidate_count] = *entry;
                    candidate_count += 1;
                }
            }
        }

        let mut available_sets_per_llc = [0usize; 256];
        for item in &candidate_game_sets[..candidate_count] {
            available_sets_per_llc[item.last_level_cache_index as usize] += 1;
        }

        candidate_game_sets[..candidate_count].sort_unstable_by(|a, b| {
            let bg_a = bg_sets_per_llc[a.last_level_cache_index as usize];
            let bg_b = bg_sets_per_llc[b.last_level_cache_index as usize];
            if bg_a != bg_b {
                return bg_a.cmp(&bg_b);
            }
            let avail_a = available_sets_per_llc[a.last_level_cache_index as usize];
            let avail_b = available_sets_per_llc[b.last_level_cache_index as usize];
            if avail_a != avail_b {
                return avail_b.cmp(&avail_a); // Descending: more available sets first
            }
            if a.last_level_cache_index != b.last_level_cache_index {
                return a.last_level_cache_index.cmp(&b.last_level_cache_index);
            }
            if a.core_index != b.core_index {
                return a.core_index.cmp(&b.core_index);
            }
            a.logical_processor_index.cmp(&b.logical_processor_index)
        });

        for item in &candidate_game_sets[..candidate_count] {
            out_ref.game_cpu_set_ids[out_ref.game_count as usize] = item.cpu_set_id;
            out_ref.game_count += 1;
        }
        return true;
    }

    // 3. Monolithic / Uniform or fallback topology
    let last_core_index = physical_core_indices[physical_core_count - 1];
    let exclude_core_zero = out_ref.physical_cores >= 4;

    let mut candidate_game_sets = [VfCpuCoreEntry {
        cpu_set_id: 0,
        core_index: 0,
        logical_processor_index: 0,
        last_level_cache_index: 0,
        efficiency_class: 0,
    }; VF_MAX_LOGICAL_PROCESSORS];
    let mut candidate_count = 0usize;
    let mut bg_taken = 0usize;

    for entry in entries_slice {
        if entry.core_index == last_core_index && bg_taken < bg_limit {
            bg_taken += 1;
            out_ref.background_cpu_set_ids[out_ref.background_count as usize] = entry.cpu_set_id;
            out_ref.background_count += 1;
        } else {
            if !exclude_core_zero || entry.core_index != 0 {
                candidate_game_sets[candidate_count] = *entry;
                candidate_count += 1;
            }
        }
    }

    let mut llc_core_counts = [0usize; 256];
    for item in &candidate_game_sets[..candidate_count] {
        llc_core_counts[item.last_level_cache_index as usize] += 1;
    }

    candidate_game_sets[..candidate_count].sort_unstable_by(|a, b| {
        let count_a = llc_core_counts[a.last_level_cache_index as usize];
        let count_b = llc_core_counts[b.last_level_cache_index as usize];
        if count_a != count_b {
            return count_b.cmp(&count_a); // Descending
        }
        if a.last_level_cache_index != b.last_level_cache_index {
            return a.last_level_cache_index.cmp(&b.last_level_cache_index);
        }
        if a.core_index != b.core_index {
            return a.core_index.cmp(&b.core_index);
        }
        a.logical_processor_index.cmp(&b.logical_processor_index)
    });

    for item in &candidate_game_sets[..candidate_count] {
        out_ref.game_cpu_set_ids[out_ref.game_count as usize] = item.cpu_set_id;
        out_ref.game_count += 1;
    }
    true
}

pub fn eval_bg_confinement(logical_cores_used: f64, currently_confined: bool) -> bool {
    if currently_confined {
        logical_cores_used >= 0.25
    } else {
        logical_cores_used >= 0.50
    }
}
