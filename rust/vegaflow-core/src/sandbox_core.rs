use crate::ffi_guard;
use std::os::raw::c_char;

const ANTI_CHEAT_SIGNATURES: &[&[u8]] = &[
    b"easyanticheat",
    b"battleye",
    b"vanguard",
    b"vgk.sys",
    b"randgrid.sys",
    b"anticheatexpert",
    b"hoyokprotect",
    b"mhyprot",
    b"mhypbase",
    b"acebase.dll",
    b"acebase",
    b"ace-base",
    b"beservice",
    b"blackcipher",
    b"equ8",
    b"nprotect",
    b"gameguard",
    b"xigncode",
    b"genshinimpact",
    b"zenlesszonezero",
    b"starrail",
];

const CRITICAL_SYSTEM_FILES: &[&[u8]] = &[
    b"bootmgr",
    b"pagefile.sys",
    b"hiberfil.sys",
    b"swapfile.sys",
    b"ntldr",
    b"bootsect.bak",
];

#[inline]
fn contains_ignore_case(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.is_empty() {
        return true;
    }
    if needle.len() > haystack.len() {
        return false;
    }
    haystack.windows(needle.len()).any(|window| {
        window
            .iter()
            .zip(needle.iter())
            .all(|(&h, &n)| h.eq_ignore_ascii_case(&n))
    })
}

#[inline]
fn eq_ignore_case(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    a.iter()
        .zip(b.iter())
        .all(|(&x, &y)| x.eq_ignore_ascii_case(&y))
}

#[inline]
fn extract_filename(path: &[u8]) -> &[u8] {
    let mut start = 0;
    for (i, &b) in path.iter().enumerate() {
        if b == b'/' || b == b'\\' {
            start = i + 1;
        }
    }
    &path[start..]
}

#[no_mangle]
pub extern "C" fn vf_rust_has_anticheat_signature(
    utf8_path: *const c_char,
    len: usize,
) -> bool {
    ffi_guard(true, || {
        if utf8_path.is_null() || len == 0 {
            return false;
        }

        let path_bytes = unsafe { core::slice::from_raw_parts(utf8_path as *const u8, len) };

        for &sig in ANTI_CHEAT_SIGNATURES {
            if contains_ignore_case(path_bytes, sig) {
                return true;
            }
        }

        false
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_is_critical_system_filename(
    utf8_name: *const c_char,
    len: usize,
) -> bool {
    ffi_guard(true, || {
        if utf8_name.is_null() || len == 0 {
            return false;
        }

        let raw_bytes = unsafe { core::slice::from_raw_parts(utf8_name as *const u8, len) };
        let filename = extract_filename(raw_bytes);

        for &crit in CRITICAL_SYSTEM_FILES {
            if eq_ignore_case(filename, crit) {
                return true;
            }
        }

        false
    })
}
