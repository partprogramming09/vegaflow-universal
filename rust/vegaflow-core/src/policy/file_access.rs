//! SandboxGuard's whole decision for one canonical, lower-cased path: may VegaFlow read or write it?
//! Vectors shared with C++ Test 40: `tests/vectors/file_access.tsv`.

use super::path_policy::{self, VF_PATH_ALLOWED};

/// Writing to a folder where anti-cheat was detected (or the caller says the game is protected).
pub const VF_PATH_ANTICHEAT: u32 = 9;
/// No allowed folder contains the path (or only a read-only one does and the action is a write).
pub const VF_PATH_NOT_WHITELISTED: u32 = 10;
/// Not a plain local path: device or UNC prefix, relative, an alternate data stream, or not in canonical form
/// (`/`, empty folder names, a name ending in `.` or space, NUL), which Windows could read as another path.
pub const VF_PATH_NOT_LOCAL_PATH: u32 = 11;

const BACKSLASH: u16 = b'\\' as u16;
const SLASH: u16 = b'/' as u16;
const COLON: u16 = b':' as u16;
const DOT: u16 = b'.' as u16;
const SPACE: u16 = b' ' as u16;

/// One allowed folder, lower-cased.
pub struct AllowedRoot<'a> {
    pub path: &'a [u16],
    pub writable: bool,
}

#[inline]
fn is_separator(u: u16) -> bool {
    u == BACKSLASH || u == SLASH
}

/// `x:\` followed by the rest of the path, where `x` is not a separator.
fn has_drive_prefix(path: &[u16]) -> bool {
    path.len() >= 3 && !is_separator(path[0]) && path[1] == COLON && is_separator(path[2])
}

/// The folders after `x:\`. Empty names are skipped, but decide only gets here for paths without them.
fn components(path: &[u16]) -> impl Iterator<Item = &[u16]> {
    path[3..].split(|&u| is_separator(u)).filter(|c| !c.is_empty())
}

/// A plain local path in the form CanonicalizePath gives: `x:\`, then folder names joined by `\`, each non-empty,
/// not ending in `.` or space (so no `.` or `..`); no `:` after the drive, no `/`, no NUL.
fn is_plain_local_path(path: &[u16]) -> bool {
    if !has_drive_prefix(path) || path[2..].iter().any(|&u| u == COLON || u == SLASH || u == 0) {
        return false;
    }
    path.len() == 3
        || path[3..]
            .split(|&u| u == BACKSLASH)
            .all(|name| matches!(name.last(), Some(&last) if last != DOT && last != SPACE))
}

/// A usable allowed folder: a plain local path with at least one folder after the drive.
fn is_usable_root(root: &[u16]) -> bool {
    is_plain_local_path(root) && components(root).next().is_some()
}

/// Same drive and every folder of `root` matches the start of `path`, folder by folder.
fn is_inside(path: &[u16], root: &[u16]) -> bool {
    if path[0] != root[0] {
        return false;
    }
    let mut path_parts = components(path);
    components(root).all(|r| path_parts.next() == Some(r))
}

/// First matching rule: blacklist, plain local path, anti-cheat (writes only), then the whitelist folder by folder.
/// Roots that are a drive root, a bare drive, relative or UNC are ignored; read-only roots allow reading only.
pub fn decide(
    path: &[u16],
    win_dir: &[u16],
    sys_dir: &[u16],
    write: bool,
    anticheat: bool,
    roots: &[AllowedRoot],
) -> u32 {
    let blacklisted = path_policy::classify(path, win_dir, sys_dir);
    if blacklisted != VF_PATH_ALLOWED {
        return blacklisted;
    }
    if !is_plain_local_path(path) {
        return VF_PATH_NOT_LOCAL_PATH;
    }
    if write && anticheat {
        return VF_PATH_ANTICHEAT;
    }
    let allowed = roots
        .iter()
        .filter(|r| (r.writable || !write) && is_usable_root(r.path))
        .any(|r| is_inside(path, r.path));
    if allowed {
        VF_PATH_ALLOWED
    } else {
        VF_PATH_NOT_WHITELISTED
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::*;
    use crate::policy::path_policy::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/file_access.tsv");

    fn utf16(s: &str) -> Vec<u16> {
        s.encode_utf16().collect()
    }

    fn code_of(name: &str) -> u32 {
        match name {
            "allowed" => VF_PATH_ALLOWED,
            "empty" => VF_PATH_EMPTY,
            "drive_root" => VF_PATH_DRIVE_ROOT,
            "critical_file" => VF_PATH_CRITICAL_FILE,
            "windows_dir" => VF_PATH_WINDOWS_DIR,
            "anticheat" => VF_PATH_ANTICHEAT,
            "not_whitelisted" => VF_PATH_NOT_WHITELISTED,
            "not_local_path" => VF_PATH_NOT_LOCAL_PATH,
            other => panic!("unknown verdict {other}"),
        }
    }

    /// Runs every case row through the exported function, the way C++ calls it.
    #[test]
    fn decision_matches_the_shared_vectors() {
        let rows: Vec<Vec<&str>> = VECTORS
            .lines()
            .map(str::trim_end)
            .filter(|l| !l.is_empty() && !l.starts_with('#'))
            .map(|l| l.split('\t').collect())
            .collect();
        let root_paths: Vec<(Vec<u16>, bool)> = rows
            .iter()
            .filter(|c| c[0] == "root")
            .map(|c| (utf16(if c[1] == "<empty>" { "" } else { c[1] }), c[2] == "rw"))
            .collect();
        let roots: Vec<VfAllowedRoot> = root_paths
            .iter()
            .map(|(p, w)| VfAllowedRoot { path: p.as_ptr(), len: p.len(), writable: *w })
            .collect();
        let win = utf16("c:\\windows");
        let sys = utf16("c:\\windows\\system32");

        let mut checked = 0;
        for c in rows.iter().filter(|c| c[0] != "root") {
            assert_eq!(c.len(), 5, "bad vector row: {c:?}");
            let path = utf16(if c[1] == "<empty>" { "" } else { c[1] });
            let verdict = vf_rust_decide_file_access(
                path.as_ptr(), path.len(), win.as_ptr(), win.len(), sys.as_ptr(), sys.len(),
                c[2] == "write", c[3] == "1", roots.as_ptr(), roots.len(),
            );
            assert_eq!(verdict, code_of(c[4]), "vector {} ({})", c[0], c[1]);
            checked += 1;
        }
        assert!(checked >= 42, "expected at least 42 vectors, found {checked}");
    }

    #[test]
    fn verdict_codes_follow_the_blacklist_codes() {
        // rust_bridge.h (enum VfPathVerdict) and the C++ switch in sandbox_guard.cpp depend on these exact numbers.
        assert_eq!([VF_PATH_ANTICHEAT, VF_PATH_NOT_WHITELISTED, VF_PATH_NOT_LOCAL_PATH], [9, 10, 11]);
    }

    #[test]
    fn null_roots_allow_nothing() {
        let p = utf16("c:\\users\\u\\appdata\\local\\x.txt");
        let w = utf16("c:\\windows");
        let r = vf_rust_decide_file_access(p.as_ptr(), p.len(), w.as_ptr(), w.len(), w.as_ptr(), w.len(), false, false, core::ptr::null(), 3);
        assert_eq!(r, VF_PATH_NOT_WHITELISTED);
    }

    #[test]
    fn never_panics_on_any_short_path() {
        // Paths and roots up to 6 units over an alphabet with every unit the rules look at.
        let alphabet: Vec<u16> = "a:\\/.b".encode_utf16().collect();
        let base = alphabet.len();
        let win = utf16("c:\\windows");
        let mut all = Vec::new();
        for len in 0..=6u32 {
            for mut n in 0..base.pow(len) {
                let mut p = Vec::with_capacity(len as usize);
                for _ in 0..len {
                    p.push(alphabet[n % base]);
                    n /= base;
                }
                all.push(p);
            }
        }
        let root_paths = [utf16("a:\\a"), utf16("a:\\"), utf16("a:/a/"), Vec::new()];
        let roots: Vec<AllowedRoot> = root_paths.iter().map(|p| AllowedRoot { path: p, writable: true }).collect();
        for p in &all {
            let _ = decide(p, &win, &win, true, false, &roots);
            let _ = decide(&utf16("a:\\a\\b"), &win, &win, true, false, &[AllowedRoot { path: p, writable: true }]);
        }
    }
}
