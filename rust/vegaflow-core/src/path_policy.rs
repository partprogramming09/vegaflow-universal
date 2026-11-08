//! SandboxGuard path blacklist: which paths may never be touched, and why.
//!
//! Pure string rules over UTF-16 code units, the unit the C++ side already holds. The caller passes the
//! path and the Windows/System directories already lower-cased with the same `towlower` it always used, so
//! this module never changes case and cannot disagree with C++ about non-ASCII text.
//! The shared vector file `tests/vectors/sandbox_blacklist.tsv` is checked here and by C++ Test 33.

use crate::ffi_guard;

pub const VF_PATH_ALLOWED: u32 = 0;
pub const VF_PATH_EMPTY: u32 = 1;
pub const VF_PATH_DRIVE_ROOT: u32 = 2;
pub const VF_PATH_CRITICAL_FILE: u32 = 3;
pub const VF_PATH_WINDOWS_DIR: u32 = 4;
pub const VF_PATH_WINDOWS_OTHER_DRIVE: u32 = 5;
pub const VF_PATH_COMMON_FILES: u32 = 6;
pub const VF_PATH_SYSTEM_VOLUME_INFORMATION: u32 = 7;
/// Returned if the classifier itself fails (a bug): not zero, so the path is refused.
pub const VF_PATH_INTERNAL_ERROR: u32 = 8;

const BACKSLASH: u16 = b'\\' as u16;
const SLASH: u16 = b'/' as u16;
const COLON: u16 = b':' as u16;

const CRITICAL_SYSTEM_FILES: &[&str] = &[
    "bootmgr",
    "pagefile.sys",
    "hiberfil.sys",
    "swapfile.sys",
    "ntldr",
    "bootsect.bak",
];

const WINDOWS_DIR_FRAGMENTS: &[&str] = &["\\windows\\system32", "\\windows\\syswow64", "\\windows\\winsxs"];
const COMMON_FILES_FRAGMENTS: &[&str] = &["\\program files\\common files", "\\program files (x86)\\common files"];
const SYSTEM_VOLUME_INFORMATION: &str = "\\system volume information";
/// `\windows\` right after `X:` marks the Windows folder of any drive.
const OTHER_DRIVE_WINDOWS: &str = "\\windows\\";

#[inline]
fn is_separator(u: u16) -> bool {
    u == BACKSLASH || u == SLASH
}

#[inline]
fn matches_ascii(unit: u16, byte: u8) -> bool {
    unit == byte as u16
}

fn eq_ascii(units: &[u16], text: &str) -> bool {
    units.len() == text.len() && units.iter().zip(text.bytes()).all(|(&u, b)| matches_ascii(u, b))
}

fn starts_with(hay: &[u16], prefix: &[u16]) -> bool {
    hay.len() >= prefix.len() && hay[..prefix.len()] == *prefix
}

fn contains_ascii(hay: &[u16], needle: &str) -> bool {
    let n = needle.len();
    if n == 0 {
        return true;
    }
    if n > hay.len() {
        return false;
    }
    hay.windows(n).any(|w| w.iter().zip(needle.bytes()).all(|(&u, b)| matches_ascii(u, b)))
}

/// Length of the root part: `X:` or `X:\` for a drive, or the run of leading separators.
/// Any single unit followed by `:` counts as a drive (`1:`, `$:`, `::`, `é:`), exactly as the C++ standard library
/// the old code relied on does; being stricter here would let such paths through. A separator in first place is
/// the exception: `\:\x` is a rooted path whose first folder is called `:`.
fn root_len(path: &[u16]) -> usize {
    let is_drive_letter = path.len() >= 2 && path[1] == COLON && !is_separator(path[0]);
    if is_drive_letter {
        return if path.len() > 2 && is_separator(path[2]) { 3 } else { 2 };
    }
    path.iter().take_while(|&&u| is_separator(u)).count()
}

/// A path that is a root, or sits directly under one (`C:\`, `C:\file`, `\file`, `file`).
/// A "direct child" means nothing after the root contains another separator.
fn is_drive_root_or_direct_child(path: &[u16]) -> bool {
    let rest = &path[root_len(path)..];
    // Repeated separators count as one (`C:\\foo` is `C:\foo`), but a trailing one does not disappear.
    let skipped = rest.iter().take_while(|&&u| is_separator(u)).count();
    let rest = &rest[skipped..];
    if rest.is_empty() || !rest.iter().any(|&u| is_separator(u)) {
        return true;
    }
    path.len() <= 3 && path.contains(&COLON)
}

fn file_name(path: &[u16]) -> &[u16] {
    let start = path.iter().rposition(|&u| is_separator(u)).map_or(0, |i| i + 1);
    &path[start..]
}

fn is_critical_file(path: &[u16]) -> bool {
    let name = file_name(path);
    CRITICAL_SYSTEM_FILES.iter().any(|c| eq_ascii(name, c))
}

fn is_windows_dir(path: &[u16], win_dir: &[u16], sys_dir: &[u16]) -> bool {
    starts_with(path, win_dir)
        || starts_with(path, sys_dir)
        || WINDOWS_DIR_FRAGMENTS.iter().any(|f| contains_ascii(path, f))
}

/// `X:\windows\...` on any drive: the 9 units after `X:` must be `\windows\`, so the path needs at least 11 units.
fn is_other_drive_windows(path: &[u16]) -> bool {
    let end = 2 + OTHER_DRIVE_WINDOWS.len();
    path.len() >= end
        && path[1] == COLON
        && path[2..end].iter().zip(OTHER_DRIVE_WINDOWS.bytes()).all(|(&u, b)| matches_ascii(u, b))
}

unsafe fn slice_or_empty<'a>(ptr: *const u16, len: usize) -> &'a [u16] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

/// Classifies a path, already lower-cased and canonical, against the blacklist. Returns `VF_PATH_ALLOWED` when no
/// rule matches, otherwise the code of the first rule that does. The rule order is part of the contract: root
/// protection, critical boot files, Windows folders, other-drive Windows, Common Files, System Volume Information.
/// An empty `win_dir`/`sys_dir` matches every path (an empty prefix), as the C++ code did.
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

        if path.is_empty() {
            VF_PATH_EMPTY
        } else if is_drive_root_or_direct_child(path) {
            VF_PATH_DRIVE_ROOT
        } else if is_critical_file(path) {
            VF_PATH_CRITICAL_FILE
        } else if is_windows_dir(path, win_dir, sys_dir) {
            VF_PATH_WINDOWS_DIR
        } else if is_other_drive_windows(path) {
            VF_PATH_WINDOWS_OTHER_DRIVE
        } else if COMMON_FILES_FRAGMENTS.iter().any(|f| contains_ascii(path, f)) {
            VF_PATH_COMMON_FILES
        } else if contains_ascii(path, SYSTEM_VOLUME_INFORMATION) {
            VF_PATH_SYSTEM_VOLUME_INFORMATION
        } else {
            VF_PATH_ALLOWED
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const VECTORS: &str = include_str!("../../../tests/vectors/sandbox_blacklist.tsv");

    fn utf16_lower(s: &str) -> Vec<u16> {
        // The vectors are ASCII; C++ lower-cases with towlower, which is the identity outside letters.
        s.encode_utf16().map(|u| if u < 128 { (u as u8).to_ascii_lowercase() as u16 } else { u }).collect()
    }

    fn classify(path: &str) -> u32 {
        let p = utf16_lower(path);
        let win = utf16_lower("C:\\Windows");
        let sys = utf16_lower("C:\\Windows\\System32");
        vf_rust_classify_blacklisted_path(p.as_ptr(), p.len(), win.as_ptr(), win.len(), sys.as_ptr(), sys.len())
    }

    fn code_of(category: &str) -> u32 {
        match category {
            "allowed" => VF_PATH_ALLOWED,
            "empty" => VF_PATH_EMPTY,
            "drive_root" => VF_PATH_DRIVE_ROOT,
            "critical_file" => VF_PATH_CRITICAL_FILE,
            "windows_dir" => VF_PATH_WINDOWS_DIR,
            "windows_other_drive" => VF_PATH_WINDOWS_OTHER_DRIVE,
            "common_files" => VF_PATH_COMMON_FILES,
            "system_volume_information" => VF_PATH_SYSTEM_VOLUME_INFORMATION,
            other => panic!("unknown category {other}"),
        }
    }

    #[test]
    fn classification_matches_the_shared_vectors() {
        let mut checked = 0;
        for line in VECTORS.lines().map(str::trim_end) {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let col: Vec<&str> = line.split('\t').collect();
            assert_eq!(col.len(), 3, "bad vector row: {line}");
            let path = if col[1] == "<empty>" { "" } else { col[1] };
            assert_eq!(classify(path), code_of(col[2]), "vector {} ({})", col[0], col[1]);
            checked += 1;
        }
        assert!(checked >= 52, "expected at least 52 vectors, found {checked}");
    }

    #[test]
    fn never_panics_on_any_short_path() {
        // A panic aborts the daemon (panic = "abort"), so check every path up to 7 units over an alphabet that
        // contains the units each rule looks at. This is what a slice bound off by one would trip over.
        let alphabet: Vec<u16> = "a:\\/?w.".encode_utf16().collect();
        let base = alphabet.len();
        let win = utf16_lower("C:\\Windows");
        let mut checked = 0u32;
        for len in 0..=7u32 {
            for mut n in 0..base.pow(len) {
                let mut path = Vec::with_capacity(len as usize);
                for _ in 0..len {
                    path.push(alphabet[n % base]);
                    n /= base;
                }
                let _ = vf_rust_classify_blacklisted_path(path.as_ptr(), path.len(), win.as_ptr(), win.len(), win.as_ptr(), win.len());
                checked += 1;
            }
        }
        assert!(checked > 100_000, "expected the exhaustive walk to cover many paths, got {checked}");
    }

    #[test]
    fn never_panics_on_every_prefix_of_long_drive_paths() {
        // The "other drive Windows" rule reads 9 units after `X:`, so lengths 9 to 12 are where a bound goes wrong.
        let win = utf16_lower("C:\\Windows");
        for full in ["d:\\windows\\x\\y\\z", "d:\\a\\b\\c\\d\\e", "1:\\windows\\", "d:/windows/x"] {
            let units = utf16_lower(full);
            for len in 0..=units.len() {
                let p = &units[..len];
                let _ = vf_rust_classify_blacklisted_path(p.as_ptr(), p.len(), win.as_ptr(), win.len(), win.as_ptr(), win.len());
            }
        }
    }

    #[test]
    fn verdict_codes_match_the_c_header() {
        // rust_bridge.h (enum VfPathVerdict) and the C++ switch in sandbox_guard.cpp depend on these exact numbers.
        assert_eq!(
            [
                VF_PATH_ALLOWED,
                VF_PATH_EMPTY,
                VF_PATH_DRIVE_ROOT,
                VF_PATH_CRITICAL_FILE,
                VF_PATH_WINDOWS_DIR,
                VF_PATH_WINDOWS_OTHER_DRIVE,
                VF_PATH_COMMON_FILES,
                VF_PATH_SYSTEM_VOLUME_INFORMATION,
                VF_PATH_INTERNAL_ERROR
            ],
            [0, 1, 2, 3, 4, 5, 6, 7, 8]
        );
    }

    #[test]
    fn null_path_is_refused() {
        let win = utf16_lower("C:\\Windows");
        let r = vf_rust_classify_blacklisted_path(core::ptr::null(), 0, win.as_ptr(), win.len(), win.as_ptr(), win.len());
        assert_eq!(r, VF_PATH_EMPTY);
    }
}
