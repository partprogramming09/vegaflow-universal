//! Every function exported to C++ (`vf_rust_*`), in one place. Each one checks its pointers, catches panics with
//! `ffi_guard` and calls the rule in `policy/`. The C declarations are in `src/bridge/rust_bridge.h`.

use std::os::raw::{c_char, c_void};

use crate::policy::apu_tier::{self, VfTierInput, TIER_ENTRY, TIER_MID};
use crate::policy::config_file::{self, SaveRefused, Settings};
use crate::policy::config_rules;
use crate::policy::cpu_governor::{self, VfCpuCoreEntry, VfTopologyPartitionOut, VF_MAX_LOGICAL_PROCESSORS};
use crate::policy::device_profile::{
    self, ParsedGpuAdapter, VfDeviceProfileDecision, VfDeviceProfileInput, VfGpuAdapterInput, VF_ARCH_ARM64_EMULATED,
    VF_ARCH_ARM64_NATIVE, VF_ARCH_UNKNOWN, VF_ARCH_X64, VF_ARCH_X86, VF_DISK_HDD, VF_DISK_NVME, VF_DISK_SSD, VF_DISK_UNKNOWN,
    VF_GPU_CONN_DISCRETE, VF_GPU_CONN_EXTERNAL, VF_GPU_CONN_INTEGRATED, VF_GPU_CONN_UNKNOWN, VF_GPU_PREF_DEFAULT,
    VF_GPU_PREF_HIGH_PERF, VF_GPU_PREF_POWER_SAVING,
};
use crate::policy::file_access::{self, AllowedRoot};
use crate::policy::folder_trust::{self, Ace, SidKind};
use crate::policy::ini_merge::{
    self, VF_INI_ERR_ENCODING, VF_INI_ERR_INVALID_INPUT, VF_INI_ERR_MISSING_END_MARKER, VF_INI_ERR_UNMATCHED_END_MARKER, VF_INI_OK,
};
use crate::policy::memory_purge::{self, VfMemoryPurgeDecision, VfMemoryPurgeInput, DEFAULT_VRAM_PRESSURE_THRESHOLD};
use crate::policy::path_policy::{self, VF_PATH_INTERNAL_ERROR};
use crate::policy::sandbox_core;
use crate::policy::steam_library;

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
pub extern "C" fn vf_rust_vram_pressure_threshold() -> f64 {
    ffi_guard(DEFAULT_VRAM_PRESSURE_THRESHOLD, || DEFAULT_VRAM_PRESSURE_THRESHOLD)
}

#[no_mangle]
pub extern "C" fn vf_rust_evaluate_memory_purge(input: *const VfMemoryPurgeInput) -> VfMemoryPurgeDecision {
    ffi_guard(unsafe { core::mem::zeroed() }, || {
        if input.is_null() {
            return VfMemoryPurgeDecision {
                should_purge_standby: false,
                should_trim_background: false,
                skipped_due_to_cooldown: false,
                skipped_due_to_disk_io: false,
                cooldown_remaining_seconds: 0,
                guard_triggered: false,
                dxgi_over_ceiling: false,
                free_ram_critical: false,
                dxgi_budget_utilization: 0.0,
                available_os_headroom_mb: 0,
                total_interventions: 0,
            };
        }
        memory_purge::evaluate_memory_purge(unsafe { &*input })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_is_factory_memory_protected_exe(utf8_name: *const c_char, len: usize) -> bool {
    ffi_guard(true, || {
        if utf8_name.is_null() || len == 0 {
            return true;
        }
        let bytes = unsafe { core::slice::from_raw_parts(utf8_name as *const u8, len) };
        match std::str::from_utf8(bytes) {
            Ok(s) => memory_purge::is_factory_memory_protected_exe(s),
            Err(_) => true,
        }
    })
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

/// Called once with raw output bytes (may contain UTF-16 NULs), valid only during the call.
pub type VfBytesFn = Option<unsafe extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize)>;

/// Called once per key/value pair read from an INI section; UTF-8, not NUL-terminated.
pub type VfKvFn = Option<unsafe extern "C" fn(ctx: *mut c_void, key: *const u8, key_len: usize, val: *const u8, val_len: usize)>;

/// One key/value update for INI / boot.config editing.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VfIniKv {
    pub key: *const u8,
    pub key_len: usize,
    pub value: *const u8,
    pub value_len: usize,
}

unsafe fn bytes_or_empty<'a>(ptr: *const u8, len: usize) -> Option<&'a [u8]> {
    if len == 0 {
        Some(&[])
    } else if ptr.is_null() {
        None
    } else {
        Some(core::slice::from_raw_parts(ptr, len))
    }
}

unsafe fn extract_kv_pairs<'a>(pairs: *const VfIniKv, pair_count: usize) -> Option<Vec<(&'a str, &'a str)>> {
    if pair_count == 0 {
        return Some(Vec::new());
    }
    if pairs.is_null() {
        return None;
    }
    let slice = core::slice::from_raw_parts(pairs, pair_count);
    let mut out = Vec::with_capacity(pair_count);
    for p in slice {
        let kb = bytes_or_empty(p.key, p.key_len)?;
        let vb = bytes_or_empty(p.value, p.value_len)?;
        let k = std::str::from_utf8(kb).ok()?;
        let v = std::str::from_utf8(vb).ok()?;
        out.push((k, v));
    }
    Some(out)
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_merge_unreal(
    original: *const u8,
    original_len: usize,
    block: *const u8,
    block_len: usize,
    on_out: VfBytesFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(block_bytes) = (unsafe { bytes_or_empty(block, block_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Ok(block_str) = std::str::from_utf8(block_bytes) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        match ini_merge::merge_unreal_uma_block(orig_bytes, block_str) {
            Ok(out) => {
                if let Some(cb) = on_out {
                    unsafe { cb(ctx, out.as_ptr(), out.len()) };
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_remove_unreal(
    original: *const u8,
    original_len: usize,
    out_changed: *mut bool,
    on_out: VfBytesFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        match ini_merge::remove_unreal_uma_block(orig_bytes) {
            Ok((out, changed)) => {
                if !out_changed.is_null() {
                    unsafe { *out_changed = changed };
                }
                if let Some(cb) = on_out {
                    unsafe { cb(ctx, out.as_ptr(), out.len()) };
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_merge_unity(
    original: *const u8,
    original_len: usize,
    pairs: *const VfIniKv,
    pair_count: usize,
    on_out: VfBytesFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(kv) = (unsafe { extract_kv_pairs(pairs, pair_count) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        match ini_merge::merge_unity_boot_config(orig_bytes, &kv) {
            Ok(out) => {
                if let Some(cb) = on_out {
                    unsafe { cb(ctx, out.as_ptr(), out.len()) };
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_apply_section(
    original: *const u8,
    original_len: usize,
    section: *const u8,
    section_len: usize,
    pairs: *const VfIniKv,
    pair_count: usize,
    on_out: VfBytesFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(sec_bytes) = (unsafe { bytes_or_empty(section, section_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Ok(sec_str) = std::str::from_utf8(sec_bytes) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(kv) = (unsafe { extract_kv_pairs(pairs, pair_count) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        match ini_merge::apply_ini_section_keys(orig_bytes, sec_str, &kv) {
            Ok(out) => {
                if let Some(cb) = on_out {
                    unsafe { cb(ctx, out.as_ptr(), out.len()) };
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_remove_section_keys(
    original: *const u8,
    original_len: usize,
    section: *const u8,
    section_len: usize,
    keys: *const VfIniKv,
    key_count: usize,
    out_changed: *mut bool,
    on_out: VfBytesFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(sec_bytes) = (unsafe { bytes_or_empty(section, section_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Ok(sec_str) = std::str::from_utf8(sec_bytes) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(kv) = (unsafe { extract_kv_pairs(keys, key_count) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let key_names: Vec<&str> = kv.into_iter().map(|(k, _)| k).collect();
        match ini_merge::remove_ini_section_keys(orig_bytes, sec_str, &key_names) {
            Ok((out, changed)) => {
                if !out_changed.is_null() {
                    unsafe { *out_changed = changed };
                }
                if let Some(cb) = on_out {
                    unsafe { cb(ctx, out.as_ptr(), out.len()) };
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_ini_query_section(
    original: *const u8,
    original_len: usize,
    section: *const u8,
    section_len: usize,
    on_kv: VfKvFn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_INI_ERR_INVALID_INPUT, || {
        let Some(orig_bytes) = (unsafe { bytes_or_empty(original, original_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Some(sec_bytes) = (unsafe { bytes_or_empty(section, section_len) }) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        let Ok(sec_str) = std::str::from_utf8(sec_bytes) else {
            return VF_INI_ERR_INVALID_INPUT;
        };
        match ini_merge::query_ini_section_keys(orig_bytes, sec_str) {
            Ok(pairs) => {
                if let Some(cb) = on_kv {
                    for (k, v) in &pairs {
                        unsafe { cb(ctx, k.as_ptr(), k.len(), v.as_ptr(), v.len()) };
                    }
                }
                VF_INI_OK
            }
            Err(e) => e.code(),
        }
    })
}

/// Evaluates the universal GPU / hardware device profile decision.
#[no_mangle]
pub extern "C" fn vf_rust_evaluate_device_profile(
    adapters: *const VfGpuAdapterInput,
    adapter_count: usize,
    input: *const VfDeviceProfileInput,
    out: *mut VfDeviceProfileDecision,
) -> bool {
    ffi_guard(false, || {
        if input.is_null() || out.is_null() || (adapters.is_null() && adapter_count != 0) {
            return false;
        }
        let raw_slice: &[VfGpuAdapterInput] = if adapter_count == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(adapters, adapter_count) }
        };
        let mut parsed: Vec<ParsedGpuAdapter<'_>> = Vec::with_capacity(raw_slice.len());
        for a in raw_slice {
            let name = match unsafe { bytes_or_empty(a.name_ptr, a.name_len) } {
                Some(b) => core::str::from_utf8(b).unwrap_or(""),
                None => "",
            };
            parsed.push(ParsedGpuAdapter {
                vendor_id: a.vendor_id,
                device_id: a.device_id,
                dxcore_available: a.dxcore_available,
                dxcore_integrated: a.dxcore_integrated,
                dxcore_detachable: a.dxcore_detachable,
                removal_detachable: a.removal_detachable,
                name,
            });
        }
        let decision = device_profile::evaluate_profile(&parsed, unsafe { &*input });
        unsafe { *out = decision };
        true
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
    layout!(v, VfMemoryPurgeInput, total_physical_bytes, free_and_zero_bytes, standby_bytes, local_budget_bytes,
        local_usage_bytes, vram_valid, memory_lists_valid, is_uma_gpu, allow_ram_purge, min_free_ram_mb, min_standby_mb,
        cooldown_seconds, elapsed_since_last_purge_ms, game_disk_read_bytes_per_sec, max_game_disk_read_bytes_per_sec,
        pressure_threshold_override, prev_interventions, dry_run);
    layout!(v, VfMemoryPurgeDecision, should_purge_standby, should_trim_background, skipped_due_to_cooldown,
        skipped_due_to_disk_io, cooldown_remaining_seconds, guard_triggered, dxgi_over_ceiling, free_ram_critical,
        dxgi_budget_utilization, available_os_headroom_mb, total_interventions);
    layout!(v, VfCpuCoreEntry, cpu_set_id, core_index, logical_processor_index, last_level_cache_index, efficiency_class);
    layout!(v, VfTopologyPartitionOut, game_cpu_set_ids, game_count, background_cpu_set_ids, background_count,
        physical_cores, logical_processors, has_multiple_ccx, has_hybrid_cores);
    layout!(v, VfTierInput, total_ram_mb, physical_cores, arch_gen, dedicated_vram, is_nvidia);
    layout!(v, VfAllowedRoot, path, len, writable);
    layout!(v, VfConfigSettings, poll_interval_ms, timer_resolution_ns100, cooldown_seconds, timer_forced_manual,
        restrict_game_cpu_sets, min_free_ram_mb, min_standby_mb, max_game_disk_read_mb_s);
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
        amd_tessellation_cap, dxvk_compiler_threads, disable_dx12_async_compute);
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
            VF_INI_OK, VF_INI_ERR_INVALID_INPUT, VF_INI_ERR_ENCODING, VF_INI_ERR_MISSING_END_MARKER,
            VF_INI_ERR_UNMATCHED_END_MARKER,
            VF_GPU_CONN_UNKNOWN as u32, VF_GPU_CONN_INTEGRATED as u32, VF_GPU_CONN_DISCRETE as u32, VF_GPU_CONN_EXTERNAL as u32,
            VF_DISK_UNKNOWN as u32, VF_DISK_HDD as u32, VF_DISK_SSD as u32, VF_DISK_NVME as u32,
            VF_ARCH_UNKNOWN as u32, VF_ARCH_X64 as u32, VF_ARCH_ARM64_EMULATED as u32, VF_ARCH_ARM64_NATIVE as u32, VF_ARCH_X86 as u32,
            VF_GPU_PREF_DEFAULT as u32, VF_GPU_PREF_POWER_SAVING as u32, VF_GPU_PREF_HIGH_PERF as u32,
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
