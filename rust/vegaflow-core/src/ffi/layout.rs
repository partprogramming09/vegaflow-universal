//! What C++ Test 34 checks against rust_bridge.h: struct layouts and the constants the header repeats by hand.

use super::config::{
    VfConfigLoad, VfConfigSettings, VfProcessExceptionEntry, VfProcessPolicyConfig, VF_CONFIG_SAVE_BLOCKED,
    VF_CONFIG_SAVE_INVALID, VF_CONFIG_SAVE_OK,
};
use super::ffi_guard;
use super::folder_trust::{VfAceEntry, VF_SID_ADMINISTRATORS, VF_SID_OTHER, VF_SID_SYSTEM, VF_SID_TRUSTED_INSTALLER};
use super::ini::VfIniKv;
use super::paths::VfAllowedRoot;
use super::process::{VfProcessClassifyInput, VfProcessTreeNode};
use crate::policy::apu_tier::VfTierInput;
use crate::policy::cpu_governor::{VfCpuCoreEntry, VfTopologyPartitionOut};
use crate::policy::device_profile::{
    VfDeviceProfileDecision, VfDeviceProfileInput, VfGpuAdapterInput, VF_ARCH_ARM64_EMULATED, VF_ARCH_ARM64_NATIVE,
    VF_ARCH_UNKNOWN, VF_ARCH_X64, VF_ARCH_X86, VF_DISK_HDD, VF_DISK_NVME, VF_DISK_SSD, VF_DISK_UNKNOWN, VF_GPU_CONN_DISCRETE,
    VF_GPU_CONN_EXTERNAL, VF_GPU_CONN_INTEGRATED, VF_GPU_CONN_UNKNOWN, VF_GPU_PREF_DEFAULT, VF_GPU_PREF_HIGH_PERF,
    VF_GPU_PREF_POWER_SAVING,
};
use crate::policy::ini_merge::{
    VF_INI_ERR_ENCODING, VF_INI_ERR_INVALID_INPUT, VF_INI_ERR_MISSING_END_MARKER, VF_INI_ERR_UNMATCHED_END_MARKER, VF_INI_OK,
};
use crate::policy::memory_purge::{VfMemoryPurgeDecision, VfMemoryPurgeInput};
use crate::policy::process_class::{
    VfProcessCategoryActions, VfProcessDecision, VF_PROC_ACTION_BOOST_AS_GAME, VF_PROC_ACTION_DO_NOT_TOUCH,
    VF_PROC_ACTION_ECO_ONLY, VF_PROC_ACTION_THROTTLE, VF_PROC_CLASS_BROWSERS, VF_PROC_CLASS_CALLS_VOICE,
    VF_PROC_CLASS_CLOUD_SYNC, VF_PROC_CLASS_DEVELOPER_IDE, VF_PROC_CLASS_HEAVY_WORK, VF_PROC_CLASS_MESSAGING,
    VF_PROC_CLASS_MUSIC, VF_PROC_CLASS_POSSIBLE_GAME, VF_PROC_CLASS_RECORDING_STREAMING, VF_PROC_CLASS_STORES_LAUNCHERS,
    VF_PROC_CLASS_UNKNOWN, VF_PROC_CLASS_WINDOWS_SYSTEM,
};

/// Size, alignment and the offset and size of each field of every struct shared with C++, in the order documented in rust_bridge.h.
/// rust_bridge.h is written by hand; C++ Test 34 compares these numbers with its own sizeof/alignof/offsetof.
fn bridge_layout() -> Vec<usize> {
    use core::mem::{align_of, offset_of, size_of};
    // Size of a field, from an accessor: Rust has no sizeof(T::field).
    fn field_size<T, F>(_: fn(&T) -> &F) -> usize {
        size_of::<F>()
    }
    macro_rules! layout {
        ($v:ident, $t:ty, $($f:ident),+) => {
            $v.push(size_of::<$t>());
            $v.push(align_of::<$t>());
            $($v.push(offset_of!($t, $f)); $v.push(field_size(|x: &$t| &x.$f));)+
        };
    }
    let mut v = Vec::new();
    layout!(v, VfMemoryPurgeInput, total_physical_bytes, free_and_zero_bytes, standby_bytes, local_budget_bytes,
        gpu_shared_usage_bytes, gpu_dedicated_usage_bytes, vram_valid, memory_lists_valid, is_uma_gpu, allow_ram_purge,
        min_free_ram_mb, min_standby_mb, cooldown_seconds, elapsed_since_last_purge_ms, game_disk_read_bytes_per_sec,
        max_game_disk_read_bytes_per_sec, pressure_threshold_override, prev_interventions, last_purge_level, dry_run);
    layout!(v, VfMemoryPurgeDecision, purge_level, should_trim_background, skipped_due_to_cooldown,
        skipped_due_to_disk_io, cooldown_remaining_seconds, guard_triggered, dxgi_over_ceiling, free_ram_critical,
        memory_pressure, dxgi_budget_utilization, available_os_headroom_mb, total_interventions);
    layout!(v, VfCpuCoreEntry, cpu_set_id, core_index, logical_processor_index, group, last_level_cache_index, efficiency_class);
    layout!(v, VfTopologyPartitionOut, game_cpu_set_ids, game_count, background_cpu_set_ids, background_count,
        physical_cores, logical_processors, has_multiple_ccx, has_hybrid_cores);
    layout!(v, VfTierInput, total_ram_mb, physical_cores, arch_gen, dedicated_vram, is_nvidia);
    layout!(v, VfAllowedRoot, path, len, writable);
    layout!(v, VfConfigSettings, poll_interval_ms, cooldown_seconds,
        restrict_game_cpu_sets, min_free_ram_mb, min_standby_mb, max_game_disk_read_mb_s, auto_vram_reporting,
        auto_priority_separation, auto_game_dvr_off, auto_power_profile, auto_engine_presets);
    layout!(v, VfConfigLoad, settings, status, rejected_keys);
    layout!(v, VfAceEntry, mask, sid_kind, allows, inherit_only);
    layout!(v, VfIniKv, key, key_len, value, value_len);
    layout!(v, VfGpuAdapterInput, vendor_id, device_id, dedicated_vram_mb, shared_sys_mb, dxcore_available,
        dxcore_integrated, dxcore_detachable, removal_detachable, name_ptr, name_len);
    layout!(v, VfDeviceProfileInput, total_ram_mb, physical_cores, cpu_family, cpu_model, user_gpu_pref,
        system_arch, power_known, has_battery, ac_online, battery_saver, dxcore_available, ram_probe_ok,
        experimental_egpu_streaming, override_arch_gen);
    layout!(v, VfDeviceProfileDecision, selected_gpu_index, gpu_probe_ok, gpu_connection, gpu_arch_gen,
        apu_tier, apply_visual_cuts, apply_uma_memory_guard, apply_uma_vram_reg_override, allow_ram_purge,
        force_high_perf_power_plan, block_driver_tweaks, apply_egpu_streaming_tweaks, safe_vram_pool_mb,
        amd_tessellation_cap, dxvk_compiler_threads, disable_dx12_async_compute, reasons);
    layout!(v, VfProcessCategoryActions, recording_streaming, calls_voice, messaging, browsers,
        stores_launchers, music, cloud_sync);
    layout!(v, VfProcessDecision, process_class, action, protected_by_anticheat, path_verified);
    layout!(v, VfProcessPolicyConfig, category_actions, heavy_work_as_game);
    layout!(v, VfProcessExceptionEntry, exe_ptr, exe_len, action);
    layout!(v, VfProcessClassifyInput, full_path_ptr, full_path_len, exe_name_ptr, exe_name_len,
        category_actions, exceptions_ptr, exceptions_count, custom_games_nul_ptr, custom_games_nul_len,
        excluded_games_nul_ptr, excluded_games_nul_len, heavy_work_as_game, is_foreground_3d, has_anticheat_signal);
    layout!(v, VfProcessTreeNode, pid, parent_pid, creation_time, process_class, action, has_user_exception,
        has_anticheat_signal);
    v
}

/// The constants rust_bridge.h repeats by hand, in its order: config status bits, config key bits, save results and
/// SID kinds. Writes at most `cap` and returns how many there are; C++ Test 34 compares them with its #defines.
#[no_mangle]
pub extern "C" fn vf_rust_bridge_constants(out: *mut u32, cap: usize) -> usize {
    use crate::policy::config_file::*;
    ffi_guard(0, || {
        let constants = [
            VF_CONFIG_CORRUPT, VF_CONFIG_KEY_REJECTED, VF_CONFIG_NEWER_VERSION, VF_CONFIG_OLD_VERSION, VF_CONFIG_EMPTY,
            VF_CONFIG_BLOCKS_SAVE, VF_CONFIG_KEY_VERSION, VF_CONFIG_KEY_POLL_INTERVAL_MS,
            VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB, VF_CONFIG_KEY_MIN_STANDBY_MB, VF_CONFIG_KEY_COOLDOWN_SECONDS,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, VF_CONFIG_KEY_CUSTOM_GAME_EXES, VF_CONFIG_KEY_MEMORY,
            VF_CONFIG_KEY_HEAVY_WORK_AS_GAME, VF_CONFIG_KEY_PROCESS_CATEGORY_ACTIONS,
            VF_CONFIG_KEY_PROCESS_EXCEPTIONS, VF_CONFIG_KEY_EXCLUDED_GAME_EXES,
            VF_CONFIG_SAVE_OK, VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_SID_OTHER as u32, VF_SID_SYSTEM as u32,
            VF_SID_ADMINISTRATORS as u32, VF_SID_TRUSTED_INSTALLER as u32,
            VF_INI_OK, VF_INI_ERR_INVALID_INPUT, VF_INI_ERR_ENCODING, VF_INI_ERR_MISSING_END_MARKER,
            VF_INI_ERR_UNMATCHED_END_MARKER,
            VF_GPU_CONN_UNKNOWN as u32, VF_GPU_CONN_INTEGRATED as u32, VF_GPU_CONN_DISCRETE as u32, VF_GPU_CONN_EXTERNAL as u32,
            VF_DISK_UNKNOWN as u32, VF_DISK_HDD as u32, VF_DISK_SSD as u32, VF_DISK_NVME as u32,
            VF_ARCH_UNKNOWN as u32, VF_ARCH_X64 as u32, VF_ARCH_ARM64_EMULATED as u32, VF_ARCH_ARM64_NATIVE as u32, VF_ARCH_X86 as u32,
            VF_GPU_PREF_DEFAULT as u32, VF_GPU_PREF_POWER_SAVING as u32, VF_GPU_PREF_HIGH_PERF as u32,
            VF_PROC_CLASS_UNKNOWN as u32, VF_PROC_CLASS_WINDOWS_SYSTEM as u32, VF_PROC_CLASS_RECORDING_STREAMING as u32,
            VF_PROC_CLASS_CALLS_VOICE as u32, VF_PROC_CLASS_MESSAGING as u32, VF_PROC_CLASS_BROWSERS as u32,
            VF_PROC_CLASS_STORES_LAUNCHERS as u32, VF_PROC_CLASS_MUSIC as u32, VF_PROC_CLASS_CLOUD_SYNC as u32,
            VF_PROC_CLASS_DEVELOPER_IDE as u32, VF_PROC_CLASS_HEAVY_WORK as u32, VF_PROC_CLASS_POSSIBLE_GAME as u32,
            VF_PROC_ACTION_DO_NOT_TOUCH as u32, VF_PROC_ACTION_ECO_ONLY as u32,
            VF_PROC_ACTION_THROTTLE as u32, VF_PROC_ACTION_BOOST_AS_GAME as u32,
        ];
        if !out.is_null() {
            let n = constants.len().min(cap);
            unsafe { core::ptr::copy_nonoverlapping(constants.as_ptr(), out, n) };
        }
        constants.len()
    })
}

/// See `bridge_layout`. Writes at most `cap` numbers to `out` and returns how many there are in total.
#[no_mangle]
pub extern "C" fn vf_rust_bridge_layout(out: *mut usize, cap: usize) -> usize {
    ffi_guard(0, || {
        let layout = bridge_layout();
        if !out.is_null() {
            let n = layout.len().min(cap);
            unsafe { core::ptr::copy_nonoverlapping(layout.as_ptr(), out, n) };
        }
        layout.len()
    })
}
