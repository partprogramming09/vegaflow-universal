//! Paths and files: blacklisted paths, file access, Steam libraries, anti-cheat and critical system names.

use std::os::raw::{c_char, c_void};

use super::{ffi_guard, slice_or_empty};
use crate::policy::file_access::{self, AllowedRoot};
use crate::policy::path_policy::{self, VF_PATH_INTERNAL_ERROR};
use crate::policy::sandbox_core;
use crate::policy::steam_library;

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
