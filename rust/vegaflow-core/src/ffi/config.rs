//! `config.json`: load and save the settings, game names and the process policy.

use std::os::raw::c_void;

use super::{bytes_or_empty, ffi_guard};
use crate::policy::config_file::{self, SaveRefused, Settings};
use crate::policy::process_class::VfProcessCategoryActions;

/// Called once per string handed back to C++: UTF-8, not NUL-terminated, valid only during the call.
pub type VfUtf8Fn = Option<unsafe extern "C" fn(ctx: *mut c_void, utf8: *const u8, len: usize)>;

/// Every setting of `config.json`; the game names travel apart.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfConfigSettings {
    pub poll_interval_ms: u32,
    pub cooldown_seconds: u32,
    pub restrict_game_cpu_sets: bool,
    pub min_free_ram_mb: u64,
    pub min_standby_mb: u64,
    pub max_game_disk_read_mb_s: u64,
    // E2.2: opt-in system tweaks (all false in a factory file).
    pub auto_vram_reporting: bool,
    pub auto_priority_separation: bool,
    pub auto_game_dvr_off: bool,
    pub auto_power_profile: bool,
    pub auto_engine_presets: bool,
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
        cooldown_seconds: s.cooldown_seconds,
        restrict_game_cpu_sets: s.restrict_game_cpu_sets,
        min_free_ram_mb: s.min_free_ram_mb,
        min_standby_mb: s.min_standby_mb,
        max_game_disk_read_mb_s: s.max_game_disk_read_mb_s,
        auto_vram_reporting: s.auto_vram_reporting,
        auto_priority_separation: s.auto_priority_separation,
        auto_game_dvr_off: s.auto_game_dvr_off,
        auto_power_profile: s.auto_power_profile,
        auto_engine_presets: s.auto_engine_presets,
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
            restrict_game_cpu_sets: s.restrict_game_cpu_sets,
            min_free_ram_mb: s.min_free_ram_mb,
            min_standby_mb: s.min_standby_mb,
            cooldown_seconds: s.cooldown_seconds,
            max_game_disk_read_mb_s: s.max_game_disk_read_mb_s,
            custom_game_exes,
            auto_vram_reporting: s.auto_vram_reporting,
            auto_priority_separation: s.auto_priority_separation,
            auto_game_dvr_off: s.auto_game_dvr_off,
            auto_power_profile: s.auto_power_profile,
            auto_engine_presets: s.auto_engine_presets,
            ..Settings::default()
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

/// Phase 5 process policy config settings passed over FFI alongside `VfConfigLoad`.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfProcessPolicyConfig {
    pub category_actions: VfProcessCategoryActions,
    pub heavy_work_as_game: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VfProcessExceptionEntry {
    pub exe_ptr: *const u8,
    pub exe_len: usize,
    pub action: u8,
}

pub type VfProcessExceptionFn = Option<unsafe extern "C" fn(ctx: *mut c_void, exe: *const u8, len: usize, action: u8)>;

#[no_mangle]
pub extern "C" fn vf_rust_config_load_ex(
    bytes: *const u8,
    len: usize,
    out: *mut VfConfigLoad,
    out_policy: *mut VfProcessPolicyConfig,
    on_game_exe: VfUtf8Fn,
    on_excluded_game_exe: VfUtf8Fn,
    on_exception: VfProcessExceptionFn,
    ctx: *mut c_void,
) -> bool {
    ffi_guard(false, || {
        if out.is_null() || out_policy.is_null() || (bytes.is_null() && len != 0) {
            return false;
        }
        let text = if len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(bytes, len) } };
        let loaded = config_file::load(text);
        unsafe {
            *out = VfConfigLoad {
                settings: to_ffi(&loaded.settings),
                status: loaded.status,
                rejected_keys: loaded.rejected_keys,
            };
            *out_policy = VfProcessPolicyConfig {
                category_actions: loaded.settings.process_category_actions,
                heavy_work_as_game: loaded.settings.heavy_work_as_game,
            };
        }
        if let Some(callback) = on_game_exe {
            for name in &loaded.settings.custom_game_exes {
                unsafe { callback(ctx, name.as_ptr(), name.len()) };
            }
        }
        if let Some(callback) = on_excluded_game_exe {
            for name in &loaded.settings.excluded_game_exes {
                unsafe { callback(ctx, name.as_ptr(), name.len()) };
            }
        }
        if let Some(callback) = on_exception {
            for (exe, action) in &loaded.settings.process_exceptions {
                unsafe { callback(ctx, exe.as_ptr(), exe.len(), *action) };
            }
        }
        true
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_config_save_ex(
    original: *const u8,
    original_len: usize,
    settings: *const VfConfigSettings,
    policy: *const VfProcessPolicyConfig,
    game_exes: *const u8,
    game_exes_len: usize,
    excluded_game_exes: *const u8,
    excluded_game_exes_len: usize,
    exceptions: *const VfProcessExceptionEntry,
    exceptions_count: usize,
    on_text: VfUtf8Fn,
    ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_CONFIG_SAVE_INVALID, || {
        if settings.is_null()
            || policy.is_null()
            || (original.is_null() && original_len != 0)
            || (game_exes.is_null() && game_exes_len != 0)
            || (excluded_game_exes.is_null() && excluded_game_exes_len != 0)
            || (exceptions.is_null() && exceptions_count != 0)
        {
            return VF_CONFIG_SAVE_INVALID;
        }
        let original = if original_len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(original, original_len) } };
        let names = if game_exes_len == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(game_exes, game_exes_len) } };
        let excl = if excluded_game_exes_len == 0 {
            &[][..]
        } else {
            unsafe { core::slice::from_raw_parts(excluded_game_exes, excluded_game_exes_len) }
        };
        let Some(custom_game_exes) = game_names(names) else {
            return VF_CONFIG_SAVE_INVALID;
        };
        let Some(excluded_game_exes) = game_names(excl) else {
            return VF_CONFIG_SAVE_INVALID;
        };
        let mut process_exceptions: Vec<(String, u8)> = Vec::with_capacity(exceptions_count);
        if exceptions_count > 0 {
            let ex_slice = unsafe { core::slice::from_raw_parts(exceptions, exceptions_count) };
            for e in ex_slice {
                let Some(bytes) = (unsafe { bytes_or_empty(e.exe_ptr, e.exe_len) }) else {
                    return VF_CONFIG_SAVE_INVALID;
                };
                let Ok(name) = std::str::from_utf8(bytes) else {
                    return VF_CONFIG_SAVE_INVALID;
                };
                process_exceptions.push((name.to_string(), e.action));
            }
        }
        let s = unsafe { &*settings };
        let p = unsafe { &*policy };
        let settings = Settings {
            poll_interval_ms: s.poll_interval_ms,
            restrict_game_cpu_sets: s.restrict_game_cpu_sets,
            min_free_ram_mb: s.min_free_ram_mb,
            min_standby_mb: s.min_standby_mb,
            cooldown_seconds: s.cooldown_seconds,
            max_game_disk_read_mb_s: s.max_game_disk_read_mb_s,
            custom_game_exes,
            heavy_work_as_game: p.heavy_work_as_game,
            process_category_actions: p.category_actions,
            process_exceptions,
            excluded_game_exes,
            auto_vram_reporting: s.auto_vram_reporting,
            auto_priority_separation: s.auto_priority_separation,
            auto_game_dvr_off: s.auto_game_dvr_off,
            auto_power_profile: s.auto_power_profile,
            auto_engine_presets: s.auto_engine_presets,
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
