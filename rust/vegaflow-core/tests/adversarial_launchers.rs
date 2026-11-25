//! Adversarial tests for the Epic and GOG readers: hostile manifests and registry values, through the C entry points.

use proptest::prelude::*;
use std::ffi::c_void;
use vegaflow_core::ffi::{vf_rust_epic_manifest_install_dir, vf_rust_gog_install_dir, VF_LAUNCHER_ERROR};
use vegaflow_core::policy::epic_manifest::parse_manifest;
use vegaflow_core::policy::install_dir::normalize_install_dir;

unsafe extern "C" fn collect(ctx: *mut c_void, p: *const u8, len: usize) {
    let out = &mut *(ctx as *mut Vec<String>);
    out.push(String::from_utf8_lossy(std::slice::from_raw_parts(p, len)).into_owned());
}

fn epic(json: &[u8]) -> (u32, Vec<String>) {
    let mut dirs: Vec<String> = Vec::new();
    let status = vf_rust_epic_manifest_install_dir(json.as_ptr(), json.len(), Some(collect), &mut dirs as *mut _ as *mut c_void);
    (status, dirs)
}

fn item(location: &str) -> String {
    format!("{{\"AppCategories\":[\"games\"],\"bIsIncompleteInstall\":false,\"InstallLocation\":\"{location}\"}}")
}

#[test]
fn an_accepted_manifest_reports_exactly_one_folder() {
    let (status, dirs) = epic(item("C:/Games/X").as_bytes());
    assert_eq!((status, dirs), (0, vec!["C:\\Games\\X".to_string()]));
}

#[test]
fn a_refused_manifest_never_calls_back() {
    for loc in ["C:/Games/../Windows", "C:/Windows", "\\\\\\\\srv\\\\s\\\\g", "relative", ""] {
        let (status, dirs) = epic(item(loc).as_bytes());
        assert!(status != 0 && status != 10 && dirs.is_empty(), "{loc}: {status}");
    }
}

#[test]
fn an_anticheat_manifest_reports_status_10() {
    let (status, dirs) = epic(item("C:/Games/EasyAntiCheat_X").as_bytes());
    assert_eq!((status, dirs.len()), (10, 1));
}

#[test]
fn null_pointers_are_an_error_not_a_crash() {
    assert_eq!(vf_rust_epic_manifest_install_dir(std::ptr::null(), 5, None, std::ptr::null_mut()), VF_LAUNCHER_ERROR);
    let z = std::ptr::null();
    assert_eq!(vf_rust_gog_install_dir(z, 1, z, 0, z, 0, z, 0, None, std::ptr::null_mut()), VF_LAUNCHER_ERROR);
}

#[test]
fn gog_invalid_utf8_is_an_error() {
    let bad = [0xffu8, 0xfe];
    let ok = b"1";
    let r = vf_rust_gog_install_dir(ok.as_ptr(), 1, ok.as_ptr(), 1, bad.as_ptr(), 2, ok.as_ptr(), 0, None, std::ptr::null_mut());
    assert_eq!(r, VF_LAUNCHER_ERROR);
}

#[test]
fn gog_accepts_a_real_looking_entry_through_the_c_entry_point() {
    let (k, p, e) = (b"1207658924", b"C:\\GOG Games\\Sample\\", b"C:\\GOG Games\\Sample\\Sample.exe");
    let mut dirs: Vec<String> = Vec::new();
    let r = vf_rust_gog_install_dir(k.as_ptr(), k.len(), k.as_ptr(), k.len(), p.as_ptr(), p.len(), e.as_ptr(), e.len(), Some(collect), &mut dirs as *mut _ as *mut c_void);
    assert_eq!((r, dirs), (0, vec!["C:\\GOG Games\\Sample".to_string()]));
}

proptest! {
    #[test]
    fn manifest_parsing_never_panics_on_noise(bytes in proptest::collection::vec(any::<u8>(), 0..512)) {
        let _ = parse_manifest(&bytes);
    }

    #[test]
    fn an_accepted_folder_is_always_a_plain_drive_path(s in "\\PC{0,80}") {
        if let Some(d) = normalize_install_dir(&s) {
            let b = d.as_bytes();
            prop_assert!(b[0].is_ascii_alphabetic() && b[1] == b':' && b[2] == b'\\');
            prop_assert!(!d.contains('/') && !d.contains(".."));
            prop_assert!(!d.ends_with('\\'));
            prop_assert!(d[3..].split('\\').all(|p| !p.is_empty() && p != "."));
        }
    }
}
