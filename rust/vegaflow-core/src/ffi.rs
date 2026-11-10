//! Every function exported to C++ (`vf_rust_*`), in one place. Each one checks its pointers, catches panics with
//! `ffi_guard` and calls the rule in `policy/` (or the ntdll call in `nt_memory`). The C declarations are in
//! `src/bridge/rust_bridge.h`.

use std::os::raw::{c_char, c_void};

use crate::nt_memory;
use crate::policy::apu_tier::{self, VfTierInput, TIER_ENTRY, TIER_MID};
use crate::policy::config_file::{self, SaveRefused, Settings};
use crate::policy::config_rules;
use crate::policy::cpu_governor::{self, VfCpuCoreEntry, VfTopologyPartitionOut, VF_MAX_LOGICAL_PROCESSORS};
use crate::policy::file_access::{self, AllowedRoot};
use crate::policy::folder_trust::{self, Ace, SidKind};
use crate::policy::path_policy::{self, VF_PATH_INTERNAL_ERROR};
use crate::policy::sandbox_core;
use crate::policy::steam_library;
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

/// One allowed folder for `vf_rust_decide_file_access`: UTF-16, lower-cased, not owned.
#[repr(C)]
pub struct VfAllowedRoot {
    pub path: *const u16,
    pub len: usize,
    pub writable: bool,
}

/// See `file_access::decide`. Null pointers count as empty strings; null `roots` counts as no roots.
#[no_mangle]
pub extern "C" fn vf_rust_decide_file_access(
    path: *const u16,
    path_len: usize,
    win_dir: *const u16,
    win_dir_len: usize,
    sys_dir: *const u16,
    sys_dir_len: usize,
    write: bool,
    anticheat: bool,
    roots: *const VfAllowedRoot,
    root_count: usize,
) -> u32 {
    ffi_guard(VF_PATH_INTERNAL_ERROR, || {
        let (path, win_dir, sys_dir) = unsafe {
            (
                slice_or_empty(path, path_len),
                slice_or_empty(win_dir, win_dir_len),
                slice_or_empty(sys_dir, sys_dir_len),
            )
        };
        let raw_roots: &[VfAllowedRoot] =
            if roots.is_null() || root_count == 0 { &[] } else { unsafe { core::slice::from_raw_parts(roots, root_count) } };
        let roots: Vec<AllowedRoot> = raw_roots
            .iter()
            .map(|r| AllowedRoot { path: unsafe { slice_or_empty(r.path, r.len) }, writable: r.writable })
            .collect();
        file_access::decide(path, win_dir, sys_dir, write, anticheat, &roots)
    })
}

/// Called once per library folder found by `vf_rust_steam_library_paths`: UTF-8, not NUL-terminated, valid only during the call.
pub type VfSteamLibraryFn = Option<unsafe extern "C" fn(ctx: *mut c_void, utf8_path: *const u8, len: usize)>;

/// See `steam_library::library_paths`. Calls `on_path` for each library in file order and returns how many there were.
#[no_mangle]
pub extern "C" fn vf_rust_steam_library_paths(vdf: *const u8, len: usize, on_path: VfSteamLibraryFn, ctx: *mut c_void) -> usize {
    ffi_guard(0, || {
        if vdf.is_null() || len == 0 {
            return 0;
        }
        let paths = steam_library::library_paths(unsafe { core::slice::from_raw_parts(vdf, len) });
        if let Some(callback) = on_path {
            for p in &paths {
                unsafe { callback(ctx, p.as_ptr(), p.len()) };
            }
        }
        paths.len()
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

/// Called once per string handed back to C++: UTF-8, not NUL-terminated, valid only during the call.
pub type VfUtf8Fn = Option<unsafe extern "C" fn(ctx: *mut c_void, utf8: *const u8, len: usize)>;

/// Every setting of `config.json`; the game names travel apart.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfConfigSettings {
    pub poll_interval_ms: u32,
    pub timer_resolution_ns100: u32,
    pub cooldown_seconds: u32,
    pub timer_forced_manual: bool,
    pub restrict_game_cpu_sets: bool,
    pub min_free_ram_mb: u64,
    pub min_standby_mb: u64,
    pub max_game_disk_read_mb_s: u64,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfConfigLoad {
    pub settings: VfConfigSettings,
    /// `VF_CONFIG_*` status bits (`policy::config_file`).
    pub status: u32,
    /// `VF_CONFIG_KEY_*` bits of the keys that took their factory value.
    pub rejected_keys: u32,
}

pub const VF_CONFIG_SAVE_OK: u32 = 0;
/// The current file is corrupt, has a rejected key or is from a newer version: nothing written.
pub const VF_CONFIG_SAVE_BLOCKED: u32 = 1;
/// A setting or game name breaks its rule, or the arguments are malformed: nothing written.
pub const VF_CONFIG_SAVE_INVALID: u32 = 2;

fn to_ffi(s: &Settings) -> VfConfigSettings {
    VfConfigSettings {
        poll_interval_ms: s.poll_interval_ms,
        timer_resolution_ns100: s.timer_resolution_ns100,
        cooldown_seconds: s.cooldown_seconds,
        timer_forced_manual: s.timer_forced_manual,
        restrict_game_cpu_sets: s.restrict_game_cpu_sets,
        min_free_ram_mb: s.min_free_ram_mb,
        min_standby_mb: s.min_standby_mb,
        max_game_disk_read_mb_s: s.max_game_disk_read_mb_s,
    }
}

/// Game names as C++ passes them: each one UTF-8 and followed by a NUL. None if the buffer breaks that shape.
fn game_names(buf: &[u8]) -> Option<Vec<String>> {
    if buf.is_empty() {
        return Some(Vec::new());
    }
    let body = buf.strip_suffix(&[0])?;
    body.split(|&b| b == 0).map(|n| String::from_utf8(n.to_vec()).ok()).collect()
}

/// See `config_file::load`. Fills `out` and calls `on_game_exe` once per accepted game name. False only for bad pointers.
#[no_mangle]
pub extern "C" fn vf_rust_config_load(
    bytes: *const u8,
    len: usize,
    out: *mut VfConfigLoad,
    on_game_exe: VfUtf8Fn,
    ctx: *mut c_void,
) -> bool {
    ffi_guard(false, || {
        if out.is_null() || (bytes.is_null() && len != 0) {
            return false;
        }
        let text = if len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(bytes, len) } };
        let loaded = config_file::load(text);
        unsafe {
            *out = VfConfigLoad {
                settings: to_ffi(&loaded.settings),
                status: loaded.status,
                rejected_keys: loaded.rejected_keys,
            }
        };
        if let Some(callback) = on_game_exe {
            for name in &loaded.settings.custom_game_exes {
                unsafe { callback(ctx, name.as_ptr(), name.len()) };
            }
        }
        true
    })
}

/// See `config_file::save`. `game_exes` holds each name followed by a NUL. On `VF_CONFIG_SAVE_OK` calls `on_text` once
/// with the whole new file; otherwise it is not called.
#[no_mangle]
pub extern "C" fn vf_rust_config_save(
    original: *const u8,
    original_len: usize,
    settings: *const VfConfigSettings,
    game_exes: *const u8,
    game_exes_len: usize,
    on_text: VfUtf8Fn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_CONFIG_SAVE_INVALID, || {
        if settings.is_null() || (original.is_null() && original_len != 0) || (game_exes.is_null() && game_exes_len != 0) {
            return VF_CONFIG_SAVE_INVALID;
        }
        let original = if original_len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(original, original_len) } };
        let names = if game_exes_len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(game_exes, game_exes_len) } };
        let Some(custom_game_exes) = game_names(names) else {
            return VF_CONFIG_SAVE_INVALID;
        };
        let s = unsafe { &*settings };
        let settings = Settings {
            poll_interval_ms: s.poll_interval_ms,
            timer_resolution_ns100: s.timer_resolution_ns100,
            timer_forced_manual: s.timer_forced_manual,
            restrict_game_cpu_sets: s.restrict_game_cpu_sets,
            min_free_ram_mb: s.min_free_ram_mb,
            min_standby_mb: s.min_standby_mb,
            cooldown_seconds: s.cooldown_seconds,
            max_game_disk_read_mb_s: s.max_game_disk_read_mb_s,
            custom_game_exes,
        };
        match config_file::save(original, &settings) {
            Ok(text) => {
                if let Some(callback) = on_text {
                    unsafe { callback(ctx, text.as_ptr(), text.len()) };
                }
                VF_CONFIG_SAVE_OK
            }
            Err(SaveRefused::Blocked(_)) => VF_CONFIG_SAVE_BLOCKED,
            Err(SaveRefused::InvalidSetting(_)) => VF_CONFIG_SAVE_INVALID,
        }
    })
}

/// See `config_rules::is_allowed_game_exe`, for a name in UTF-8. False for invalid UTF-8, bad pointers or a panic.
#[no_mangle]
pub extern "C" fn vf_rust_is_allowed_game_exe(utf8_name: *const u8, len: usize) -> bool {
    ffi_guard(false, || {
        if utf8_name.is_null() {
            return false;
        }
        let bytes = unsafe { core::slice::from_raw_parts(utf8_name, len) };
        std::str::from_utf8(bytes).is_ok_and(config_rules::is_allowed_game_exe)
    })
}

/// Who a SID is, for `vf_rust_only_admins_can_write`; any other number counts as another account.
pub const VF_SID_OTHER: u8 = 0;
pub const VF_SID_SYSTEM: u8 = 1;
pub const VF_SID_ADMINISTRATORS: u8 = 2;
pub const VF_SID_TRUSTED_INSTALLER: u8 = 3;

/// One DACL entry, as C++ reads it.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfAceEntry {
    pub mask: u32,
    pub sid_kind: u8,
    pub allows: bool,
    pub inherit_only: bool,
}

fn sid_kind(k: u8) -> SidKind {
    match k {
        VF_SID_SYSTEM => SidKind::System,
        VF_SID_ADMINISTRATORS => SidKind::Administrators,
        VF_SID_TRUSTED_INSTALLER => SidKind::TrustedInstaller,
        _ => SidKind::Other,
    }
}

/// See `folder_trust::only_admins_can_write`. False for bad pointers or a panic.
#[no_mangle]
pub extern "C" fn vf_rust_only_admins_can_write(has_dacl: bool, owner_kind: u8, aces: *const VfAceEntry, count: usize) -> bool {
    ffi_guard(false, || {
        if aces.is_null() && count != 0 {
            return false;
        }
        let entries = if count == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(aces, count) } };
        let aces: Vec<Ace> = entries
            .iter()
            .map(|e| Ace { mask: e.mask, sid: sid_kind(e.sid_kind), allows: e.allows, inherit_only: e.inherit_only })
            .collect();
        folder_trust::only_admins_can_write(has_dacl, sid_kind(owner_kind), &aces)
    })
}

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
    layout!(v, VfUmaTelemetryInput, total_physical_bytes, free_and_zero_bytes, standby_bytes, local_budget_bytes,
        local_usage_bytes, runtime_pressure_threshold, vram_valid, is_discrete_tier);
    layout!(v, VfUmaGuardDecision, available_os_headroom_mb, dxgi_budget_utilization, guard_triggered, dxgi_over_ceiling,
        free_ram_critical, total_interventions);
    layout!(v, VfNtMemoryListSample, free_and_zero_bytes, standby_bytes, modified_bytes, valid);
    layout!(v, VfCpuCoreEntry, cpu_set_id, core_index, logical_processor_index, last_level_cache_index, efficiency_class);
    layout!(v, VfTopologyPartitionOut, game_cpu_set_ids, game_count, background_cpu_set_ids, background_count,
        physical_cores, logical_processors, has_multiple_ccx, has_hybrid_cores);
    layout!(v, VfTierInput, total_ram_mb, physical_cores, arch_gen, dedicated_vram, is_nvidia);
    layout!(v, VfAllowedRoot, path, len, writable);
    layout!(v, VfConfigSettings, poll_interval_ms, timer_resolution_ns100, cooldown_seconds, timer_forced_manual,
        restrict_game_cpu_sets, min_free_ram_mb, min_standby_mb, max_game_disk_read_mb_s);
    layout!(v, VfConfigLoad, settings, status, rejected_keys);
    layout!(v, VfAceEntry, mask, sid_kind, allows, inherit_only);
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
            VF_CONFIG_KEY_TIMER_RESOLUTION_NS100, VF_CONFIG_KEY_TIMER_FORCED_MANUAL, VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB, VF_CONFIG_KEY_MIN_STANDBY_MB, VF_CONFIG_KEY_COOLDOWN_SECONDS,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, VF_CONFIG_KEY_CUSTOM_GAME_EXES, VF_CONFIG_KEY_MEMORY,
            VF_CONFIG_SAVE_OK, VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_SID_OTHER as u32, VF_SID_SYSTEM as u32,
            VF_SID_ADMINISTRATORS as u32, VF_SID_TRUSTED_INSTALLER as u32,
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
