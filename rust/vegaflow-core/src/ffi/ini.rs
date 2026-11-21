//! INI and Unity `boot.config` editing: merge, remove and query keys.

use std::os::raw::c_void;

use super::{bytes_or_empty, ffi_guard};
use crate::policy::ini_merge::{self, VF_INI_ERR_INVALID_INPUT, VF_INI_OK};

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
