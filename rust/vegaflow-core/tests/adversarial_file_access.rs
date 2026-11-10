//! Adversarial tests for `file_access::decide`: paths and roots written to escape the allowed folders or the
//! blacklist. Only the canonical form CanonicalizePath gives is accepted; any other spelling is refused.

use vegaflow_core::ffi::{vf_rust_decide_file_access, VfAllowedRoot};
use vegaflow_core::policy::file_access::{
    decide, AllowedRoot, VF_PATH_ANTICHEAT, VF_PATH_NOT_LOCAL_PATH, VF_PATH_NOT_WHITELISTED,
};
use vegaflow_core::policy::path_policy::*;

fn u(s: &str) -> Vec<u16> {
    s.encode_utf16().collect()
}

const WIN: &str = "c:\\windows";
const SYS: &str = "c:\\windows\\system32";

/// `decide` with the usual Windows dirs and the given (path, writable) roots.
fn verdict(path: &[u16], write: bool, roots: &[(&str, bool)]) -> u32 {
    let owned: Vec<(Vec<u16>, bool)> = roots.iter().map(|(p, w)| (u(p), *w)).collect();
    let roots: Vec<AllowedRoot> = owned
        .iter()
        .map(|(p, w)| AllowedRoot {
            path: p,
            writable: *w,
        })
        .collect();
    decide(path, &u(WIN), &u(SYS), write, false, &roots)
}

fn v(path: &str, write: bool, roots: &[(&str, bool)]) -> u32 {
    verdict(&u(path), write, roots)
}

use proptest::prelude::*;

const LOCAL: &[(&str, bool)] = &[("c:\\dev\\vf", true)];

#[test]
fn sibling_with_common_prefix_is_outside() {
    assert_eq!(
        v("c:\\dev\\vfx\\a.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
    assert_eq!(
        v("c:\\dev\\vf.\\a.txt", true, LOCAL),
        VF_PATH_NOT_LOCAL_PATH
    );
    assert_eq!(
        v("c:\\dev\\vf \\a.txt", true, LOCAL),
        VF_PATH_NOT_LOCAL_PATH
    );
    assert_eq!(v("c:\\dev\\v\\a.txt", true, LOCAL), VF_PATH_NOT_WHITELISTED);
}

#[test]
fn root_not_in_canonical_form_allows_nothing() {
    for root in [
        "c:\\dev\\vf\\",
        "c:/dev/vf",
        "c:\\dev\\\\vf\\\\",
        "c:/dev/vf/",
        "c:\\dev\\vf.",
        "c:\\dev\\vf ",
    ] {
        assert_eq!(
            v("c:\\dev\\vf\\a\\b.txt", true, &[(root, true)]),
            VF_PATH_NOT_WHITELISTED,
            "{root}"
        );
    }
}

/// Spellings Windows reads as another path: `/`, doubled or trailing separators, names ending in `.` or space.
/// Each would hide a blacklisted folder or a critical file from the blacklist; all are refused.
#[test]
fn non_canonical_spellings_are_refused() {
    let games: &[(&str, bool)] = &[("d:\\games", true)];
    let local: &[(&str, bool)] = &[("c:\\users\\u\\appdata\\local", true)];
    let service: &[(&str, bool)] = &[("c:\\windows\\temp", true)];
    for (p, roots) in [
        ("d:\\games/windows/system32/x.dll", games),
        ("d:\\games\\windows\\\\system32\\x.dll", games),
        ("d:\\games\\program files/common files\\x", games),
        ("d:\\games/system volume information/x", games),
        ("d:\\games\\windows.\\system32\\x.dll", games),
        ("c:/windows/temp/x.dll", service),
        ("c:\\\\windows\\temp\\x.dll", service),
        ("c:\\users\\u\\appdata\\local\\pagefile.sys\\", local),
        ("c:\\users\\u\\appdata\\local\\bootmgr/", local),
        ("c:\\users\\u\\appdata\\local\\pagefile.sys.", local),
        ("c:\\users\\u\\appdata\\local\\hiberfil.sys ", local),
    ] {
        assert_eq!(v(p, true, roots), VF_PATH_NOT_LOCAL_PATH, "{p:?}");
    }
}

/// Win32 stops a path at a NUL, so the file opened would be the part before it (here a critical file).
#[test]
fn nul_inside_a_path_is_refused() {
    let mut p = u("c:\\users\\u\\appdata\\local\\pagefile.sys");
    p.push(0);
    p.extend(u("\\x"));
    assert_eq!(
        verdict(&p, true, &[("c:\\users\\u\\appdata\\local", true)]),
        VF_PATH_NOT_LOCAL_PATH
    );
}

#[test]
fn device_and_nt_prefixes_are_refused() {
    for p in [
        "\\??\\c:\\dev\\vf\\a.txt",
        "\\\\?\\c:\\dev\\vf\\a.txt",
        "\\\\.\\c:\\dev\\vf\\a.txt",
        "//?/c:/dev/vf/a.txt",
        "\\\\?\\unc\\srv\\s\\a",
        "/c:/dev/vf/a.txt",
        "\\c:\\dev\\vf\\a.txt",
        "c:dev\\vf\\a.txt",
    ] {
        let r = v(p, true, LOCAL);
        assert_ne!(r, VF_PATH_ALLOWED, "{p:?}");
    }
}

#[test]
fn dot_segments_with_any_separator_are_refused() {
    for p in [
        "c:\\dev\\vf\\..\\x\\a",
        "c:/dev/vf/../x/a",
        "c:\\dev\\vf\\.\\a\\b",
        "c:\\dev\\vf/./a/b",
        "c:\\dev\\vf\\a\\..",
    ] {
        assert_eq!(v(p, true, LOCAL), VF_PATH_NOT_LOCAL_PATH, "{p:?}");
    }
}

#[test]
fn alternate_data_streams_are_refused() {
    for p in [
        "c:\\dev\\vf\\a\\b.txt:s",
        "c:\\dev\\vf\\a\\b.txt::$data",
        "c:\\dev\\vf\\a:b\\c.txt",
    ] {
        assert_eq!(v(p, true, LOCAL), VF_PATH_NOT_LOCAL_PATH, "{p:?}");
    }
}

#[test]
fn unusable_roots_allow_nothing() {
    for root in [
        "c:\\",
        "c:",
        "c:/",
        "c:\\\\",
        "c:\\.",
        "c:\\..",
        "c:\\a\\..",
        "\\\\srv\\share\\a",
        "\\??\\c:\\a",
        "relative\\a",
        "",
        "c:\\a:b",
        "\\\\?\\c:\\dev",
    ] {
        assert_eq!(
            v("c:\\dev\\vf\\a\\b.txt", true, &[(root, true)]),
            VF_PATH_NOT_WHITELISTED,
            "{root:?}"
        );
        assert_eq!(
            v("c:\\a\\b\\c.txt", true, &[(root, true)]),
            VF_PATH_NOT_WHITELISTED,
            "{root:?}"
        );
    }
}

#[test]
fn other_drive_never_matches() {
    assert_eq!(
        v("d:\\dev\\vf\\a\\b.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
}

/// The module never folds case (the caller lowers): an upper-case path does not enter a lower-case root.
#[test]
fn case_mismatch_is_refused_not_folded() {
    assert_eq!(
        v("C:\\dev\\vf\\a\\b.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
    assert_eq!(
        v("c:\\DEV\\vf\\a\\b.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
    // Fullwidth, Kelvin-sign and dotless-i look-alikes are distinct units, not folded into ASCII.
    assert_eq!(
        v("c:\\dev\\v\u{FF46}\\a\\b.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
    assert_eq!(
        v("c:\\dev\\vf\u{FF3C}a\\b.txt", true, LOCAL),
        VF_PATH_NOT_WHITELISTED
    );
    assert_eq!(
        v("c:\\\u{212A}\\a.txt", true, &[("c:\\k", true)]),
        VF_PATH_NOT_WHITELISTED
    );
    assert_eq!(
        v("c:\\\u{0131}\\a.txt", true, &[("c:\\i", true)]),
        VF_PATH_NOT_WHITELISTED
    );
}

#[test]
fn read_only_root_inside_writable_root_still_allows_reads() {
    let roots = &[("c:\\dev\\vf", true), ("c:\\dev\\vf\\assets", false)];
    assert_eq!(
        v("c:\\dev\\vf\\assets\\a.dll", false, roots),
        VF_PATH_ALLOWED
    );
}

#[test]
fn anticheat_beats_whitelist_for_writes_only() {
    let roots = [(u("c:\\dev\\vf"), true)];
    let r: Vec<AllowedRoot> = roots
        .iter()
        .map(|(p, w)| AllowedRoot {
            path: p,
            writable: *w,
        })
        .collect();
    assert_eq!(
        decide(&u("c:\\dev\\vf\\a\\b"), &u(WIN), &u(SYS), true, true, &r),
        VF_PATH_ANTICHEAT
    );
    assert_eq!(
        decide(&u("c:\\dev\\vf\\a\\b"), &u(WIN), &u(SYS), false, true, &r),
        VF_PATH_ALLOWED
    );
}

#[test]
fn unpaired_surrogates_do_not_panic_and_do_not_match_ascii_roots() {
    let mut p = u("c:\\dev\\vf\\");
    p.extend([0xD800, 0xDC00, 0xDFFF, 0xD800]);
    p.extend(u("\\x"));
    assert_eq!(verdict(&p, true, LOCAL), VF_PATH_ALLOWED);
    let mut p2 = u("c:\\dev\\");
    p2.push(0xD800);
    p2.extend(u("\\vf\\x"));
    assert_eq!(verdict(&p2, true, LOCAL), VF_PATH_NOT_WHITELISTED);
}

#[test]
fn very_long_paths_do_not_panic() {
    let mut p = String::from("c:\\dev\\vf");
    for _ in 0..200_000 {
        p.push_str("\\a");
    }
    assert_eq!(v(&p, true, LOCAL), VF_PATH_ALLOWED);
    let seps = format!("c:\\dev\\vf{}x\\y", "\\".repeat(1_000_000));
    assert_eq!(v(&seps, true, LOCAL), VF_PATH_NOT_LOCAL_PATH);
}

#[test]
fn ffi_root_with_null_pointer_and_nonzero_len_is_empty() {
    let p = u("c:\\dev\\vf\\a\\b.txt");
    let w = u(WIN);
    let s = u(SYS);
    let roots = [VfAllowedRoot {
        path: core::ptr::null(),
        len: 99,
        writable: true,
    }];
    let r = vf_rust_decide_file_access(
        p.as_ptr(),
        p.len(),
        w.as_ptr(),
        w.len(),
        s.as_ptr(),
        s.len(),
        true,
        false,
        roots.as_ptr(),
        1,
    );
    assert_eq!(r, VF_PATH_NOT_WHITELISTED);
    // Null Windows dirs count as empty, and an empty prefix matches everything: fail closed.
    let r = vf_rust_decide_file_access(
        p.as_ptr(),
        p.len(),
        core::ptr::null(),
        5,
        core::ptr::null(),
        5,
        true,
        false,
        roots.as_ptr(),
        1,
    );
    assert_eq!(r, VF_PATH_WINDOWS_DIR);
}

/// Reference model of "inside, folder by folder": split both on `\`/`/`, drop empties, compare as a prefix.
fn model_inside(path: &str, root: &str) -> bool {
    let parts = |s: &str| {
        s[3..]
            .split(['\\', '/'])
            .filter(|c| !c.is_empty())
            .map(str::to_owned)
            .collect::<Vec<_>>()
    };
    let (p, r) = (parts(path), parts(root));
    path.as_bytes()[0] == root.as_bytes()[0]
        && !r.is_empty()
        && p.len() >= r.len()
        && p[..r.len()] == r[..]
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 2000, failure_persistence: None, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5EED), ..ProptestConfig::default() })]

    /// Paths and roots made of short folders over {a, b, ab}: an allowed verdict must mean "inside" by the model.
    #[test]
    fn allowed_only_if_inside_by_the_model(
        pd in prop_oneof![Just("c"), Just("d")], rd in prop_oneof![Just("c"), Just("d")],
        pp in proptest::collection::vec(prop_oneof![Just("a"), Just("b"), Just("ab"), Just("")], 1..6),
        rp in proptest::collection::vec(prop_oneof![Just("a"), Just("b"), Just("ab"), Just("")], 0..4),
        sep in prop_oneof![Just("\\"), Just("/")],
    ) {
        let path = format!("{pd}:\\x\\{}", pp.join(sep));
        let root = format!("{rd}:\\x\\{}", rp.join(sep));
        let r = v(&path, true, &[(root.as_str(), true)]);
        let inside = model_inside(&path, &root);
        let canonical = |s: &str| !s.contains('/') && !s.contains("\\\\") && !s.ends_with('\\');
        if r == VF_PATH_ALLOWED {
            prop_assert!(inside && canonical(&path) && canonical(&root), "allowed: {path:?} root {root:?}");
        } else if canonical(&path) && canonical(&root) {
            prop_assert_eq!(r == VF_PATH_NOT_WHITELISTED, !inside, "{:?} root {:?}", path, root);
        }
    }
}
