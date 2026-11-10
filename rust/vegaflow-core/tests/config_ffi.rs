//! The exported config and folder-trust functions, called the way C++ calls them.

use std::os::raw::c_void;
use vegaflow_core::ffi::{
    vf_rust_config_load, vf_rust_config_save, vf_rust_is_allowed_game_exe, vf_rust_only_admins_can_write, VfAceEntry, VfConfigLoad, VfConfigSettings,
    VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_CONFIG_SAVE_OK, VF_SID_ADMINISTRATORS, VF_SID_SYSTEM,
};
use vegaflow_core::policy::config_file::{VF_CONFIG_CORRUPT, VF_CONFIG_EMPTY, VF_CONFIG_KEY_REJECTED};

unsafe extern "C" fn collect(ctx: *mut c_void, utf8: *const u8, len: usize) {
    let out = &mut *(ctx as *mut Vec<String>);
    out.push(String::from_utf8(std::slice::from_raw_parts(utf8, len).to_vec()).unwrap());
}

fn load(text: &[u8]) -> (bool, VfConfigLoad, Vec<String>) {
    let mut out = VfConfigLoad::default();
    let mut names: Vec<String> = Vec::new();
    let ok = vf_rust_config_load(text.as_ptr(), text.len(), &mut out, Some(collect), &mut names as *mut _ as *mut c_void);
    (ok, out, names)
}

fn save(original: &[u8], s: &VfConfigSettings, exes: &[u8]) -> (u32, Vec<String>) {
    let mut texts: Vec<String> = Vec::new();
    let r = vf_rust_config_save(
        original.as_ptr(),
        original.len(),
        s,
        exes.as_ptr(),
        exes.len(),
        Some(collect),
        &mut texts as *mut _ as *mut c_void,
    );
    (r, texts)
}

#[test]
fn load_returns_the_settings_and_each_game_name() {
    let (ok, out, names) =
        load(br#"{"version": 1, "poll_interval_ms": 2000, "memory": {"cooldown_seconds": 30}, "custom_game_exes": ["a.exe", "b.exe"]}"#);
    assert!(ok);
    assert_eq!(out.status, 0);
    assert_eq!(out.settings.poll_interval_ms, 2000);
    assert_eq!(out.settings.cooldown_seconds, 30);
    assert_eq!(out.settings.max_game_disk_read_mb_s, 15);
    assert_eq!(names, vec!["a.exe", "b.exe"]);
}

#[test]
fn load_reports_rejected_keys_and_corrupt_files() {
    let (_, out, _) = load(br#"{"version": 1, "poll_interval_ms": 0}"#);
    assert_eq!(out.status, VF_CONFIG_KEY_REJECTED);
    let (_, out, _) = load(b"{");
    assert_eq!(out.status, VF_CONFIG_CORRUPT);
}

#[test]
fn load_with_null_pointers() {
    let mut out = VfConfigLoad::default();
    assert!(vf_rust_config_load(std::ptr::null(), 0, &mut out, None, std::ptr::null_mut()));
    assert_eq!(out.status, VF_CONFIG_EMPTY);
    assert!(!vf_rust_config_load(std::ptr::null(), 5, &mut out, None, std::ptr::null_mut()));
    assert!(!vf_rust_config_load(b"{}".as_ptr(), 2, std::ptr::null_mut(), None, std::ptr::null_mut()));
}

#[test]
fn save_keeps_unknown_keys_and_writes_the_game_names() {
    let (_, loaded, _) = load(b"{}");
    let (r, texts) = save(br#"{"note": "keep"}"#, &loaded.settings, b"a.exe\0b.exe\0");
    assert_eq!(r, VF_CONFIG_SAVE_OK);
    assert_eq!(texts.len(), 1);
    let (_, again, names) = load(texts[0].as_bytes());
    assert_eq!(again.status, 0);
    assert_eq!(names, vec!["a.exe", "b.exe"]);
    assert!(texts[0].contains("\"note\": \"keep\""));
}

#[test]
fn save_refuses_a_blocked_file_or_bad_input_without_calling_back() {
    let (_, loaded, _) = load(b"{}");
    assert_eq!(save(b"{", &loaded.settings, b""), (VF_CONFIG_SAVE_BLOCKED, vec![]));
    assert_eq!(save(b"{}", &loaded.settings, b"a.exe"), (VF_CONFIG_SAVE_INVALID, vec![]), "names must end in NUL");
    assert_eq!(save(b"{}", &loaded.settings, b"\xff.exe\0"), (VF_CONFIG_SAVE_INVALID, vec![]));
    assert_eq!(save(b"{}", &loaded.settings, b"explorer.exe\0"), (VF_CONFIG_SAVE_INVALID, vec![]));
    let mut bad = loaded.settings;
    bad.poll_interval_ms = 0;
    assert_eq!(save(b"{}", &bad, b""), (VF_CONFIG_SAVE_INVALID, vec![]));
}

#[test]
fn folder_trust_through_the_bridge() {
    let admins_full = VfAceEntry { mask: 0x001F_01FF, sid_kind: VF_SID_ADMINISTRATORS, allows: true, inherit_only: false };
    let users_write = VfAceEntry { mask: 0x2, sid_kind: 0, allows: true, inherit_only: false };
    let unknown_kind = VfAceEntry { mask: 0x2, sid_kind: 200, allows: true, inherit_only: false };
    assert!(vf_rust_only_admins_can_write(true, VF_SID_SYSTEM, &admins_full, 1));
    assert!(!vf_rust_only_admins_can_write(true, VF_SID_SYSTEM, [admins_full, users_write].as_ptr(), 2));
    assert!(!vf_rust_only_admins_can_write(true, VF_SID_SYSTEM, &unknown_kind, 1), "an unknown kind is another account");
    assert!(!vf_rust_only_admins_can_write(true, 200, &admins_full, 1));
    assert!(vf_rust_only_admins_can_write(true, VF_SID_ADMINISTRATORS, std::ptr::null(), 0));
    assert!(!vf_rust_only_admins_can_write(true, VF_SID_ADMINISTRATORS, std::ptr::null(), 3));
}

#[test]
fn game_name_rule_through_the_bridge() {
    let ok = "MyGame.exe";
    assert!(vf_rust_is_allowed_game_exe(ok.as_ptr(), ok.len()));
    for bad in [&b"lsass.exe"[..], b"CON.exe", b"\xff.exe", b""] {
        assert!(!vf_rust_is_allowed_game_exe(bad.as_ptr(), bad.len()));
    }
    assert!(!vf_rust_is_allowed_game_exe(std::ptr::null(), 3));
}

#[test]
fn constants_shared_with_cpp_have_fixed_values() {
    // Same order and values as the #defines in rust_bridge.h; C++ Test 34 compares them too.
    let expected: [u32; 24] = [
        1, 2, 4, 8, 16, 7, // VF_CONFIG_CORRUPT .. VF_CONFIG_EMPTY, VF_CONFIG_BLOCKS_SAVE
        1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, // VF_CONFIG_KEY_VERSION .. VF_CONFIG_KEY_MEMORY
        0, 1, 2, // VF_CONFIG_SAVE_OK, _BLOCKED, _INVALID
        0, 1, 2, 3, // VF_SID_OTHER, _SYSTEM, _ADMINISTRATORS, _TRUSTED_INSTALLER
    ];
    let mut actual = [0u32; 32];
    let n = vegaflow_core::ffi::vf_rust_bridge_constants(actual.as_mut_ptr(), actual.len());
    assert_eq!(&actual[..n], &expected[..]);
}
