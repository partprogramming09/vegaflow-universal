//! Every function exported to C++ (`vf_rust_*`), in one place. Each one checks its pointers, catches panics with
//! `ffi_guard` and calls the rule in `policy/` (or the ntdll call in `nt_memory`). The C declarations are in
//! `src/bridge/rust_bridge.h`.

use std::os::raw::c_char;

use crate::nt_memory;
use crate::policy::apu_tier::{self, VfTierInput, TIER_ENTRY, TIER_MID};
use crate::policy::cpu_governor::{self, VfCpuCoreEntry, VfTopologyPartitionOut, VF_MAX_LOGICAL_PROCESSORS};
use crate::policy::path_policy::{self, VF_PATH_INTERNAL_ERROR};
use crate::policy::sandbox_core;
use crate::policy::uma_memory::{self, VfNtMemoryListSample, VfUmaGuardDecision, VfUmaTelemetryInput};

/// Runs `f` for an `extern "C"` function and returns `fallback` if it panics. A panic must never cross into C++ (that is
/// undefined behaviour) and must not take the daemon down; every fallback is the cautious answer for its function
/// (deny, assume anti-cheat present, do not confine, do not purge).
pub(crate) fn ffi_guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn slice_or_empty<'a>(ptr: *const u16, len: usize) -> &'a [u16] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

/// Returns 1 (entry), 2 (mid), 3 (advanced) or 4 (discrete or custom). A null input yields the mid tier.
#[no_mangle]
pub extern "C" fn vf_rust_classify_apu_tier(input: *const VfTierInput) -> u8 {
    ffi_guard(TIER_ENTRY, || {
        if input.is_null() {
            return TIER_MID;
        }
        apu_tier::classify(unsafe { &*input })
    })
}

/// See `path_policy::classify`. Null pointers count as empty strings.
#[no_mangle]
pub extern "C" fn vf_rust_classify_blacklisted_path(
    path: *const u16,
    path_len: usize,
    win_dir: *const u16,
    win_dir_len: usize,
    sys_dir: *const u16,
    sys_dir_len: usize,
) -> u32 {
    ffi_guard(VF_PATH_INTERNAL_ERROR, || {
        let (path, win_dir, sys_dir) = unsafe {
            (
                slice_or_empty(path, path_len),
                slice_or_empty(win_dir, win_dir_len),
                slice_or_empty(sys_dir, sys_dir_len),
            )
        };
        path_policy::classify(path, win_dir, sys_dir)
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_has_anticheat_signature(utf8_path: *const c_char, len: usize) -> bool {
    ffi_guard(true, || {
        if utf8_path.is_null() || len == 0 {
            return false;
        }
        let path_bytes = unsafe { core::slice::from_raw_parts(utf8_path as *const u8, len) };
        sandbox_core::has_anticheat_signature(path_bytes)
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_is_critical_system_filename(utf8_name: *const c_char, len: usize) -> bool {
    ffi_guard(true, || {
        if utf8_name.is_null() || len == 0 {
            return false;
        }
        let raw_bytes = unsafe { core::slice::from_raw_parts(utf8_name as *const u8, len) };
        sandbox_core::is_critical_system_filename(raw_bytes)
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_evaluate_uma_pressure(
    input: *const VfUmaTelemetryInput,
    prev_interventions: u32,
) -> VfUmaGuardDecision {
    ffi_guard(unsafe { core::mem::zeroed() }, || {
        if input.is_null() {
            return VfUmaGuardDecision {
                available_os_headroom_mb: 0,
                dxgi_budget_utilization: 0.0,
                guard_triggered: false,
                dxgi_over_ceiling: false,
                free_ram_critical: false,
                total_interventions: prev_interventions,
            };
        }
        uma_memory::evaluate_pressure(unsafe { &*input }, prev_interventions)
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_query_nt_memory_lists() -> VfNtMemoryListSample {
    ffi_guard(unsafe { core::mem::zeroed() }, nt_memory::query_memory_lists)
}

#[no_mangle]
pub extern "C" fn vf_rust_purge_nt_standby_list(dry_run: bool) -> bool {
    ffi_guard(false, || nt_memory::purge_standby_list(dry_run))
}

#[no_mangle]
pub extern "C" fn vf_rust_partition_cpu_topology(
    entries: *const VfCpuCoreEntry,
    count: u32,
    out: *mut VfTopologyPartitionOut,
) -> bool {
    ffi_guard(false, || {
        if entries.is_null() || out.is_null() || count == 0 || (count as usize) > VF_MAX_LOGICAL_PROCESSORS {
            return false;
        }
        let entries_slice = unsafe { core::slice::from_raw_parts(entries, count as usize) };
        cpu_governor::partition(entries_slice, unsafe { &mut *out })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_eval_bg_confinement(logical_cores_used: f64, currently_confined: bool) -> bool {
    ffi_guard(false, || cpu_governor::eval_bg_confinement(logical_cores_used, currently_confined))
}
