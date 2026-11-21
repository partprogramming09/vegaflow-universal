//! Adversarial tests for `policy::process_class::classify_process`.
//! A test named `bug_*` reproduces a contract break that was fixed; it guards against regressions.

use proptest::prelude::*;
use vegaflow_core::policy::process_class::*;

fn input<'a>(full_path: &'a str, exe_name: &'a str) -> ProcessPolicyInput<'a> {
    ProcessPolicyInput {
        full_path,
        exe_name,
        category_actions: VfProcessCategoryActions::default(),
        exceptions: &[],
        custom_game_exes: &[],
        excluded_game_exes: &[],
        heavy_work_as_game: false,
        is_foreground_3d: false,
        has_anticheat_signal: false,
    }
}

// ============================================================================
// Bugs: minimal reproducers of contract violations
// ============================================================================

/// Contract: a path that only contains `\windows\` nested under another directory
/// (e.g. `C:\x\windows\`) or outside `<drive>:\windows\...` must not count.
#[test]
fn bug_cmd_and_powershell_accept_nested_windows_folder() {
    for p in [
        r"C:\x\windows\System32\cmd.exe",
        r"D:\Games\Windows\System32\cmd.exe",
        r"C:\Users\alice\windows\sub\cmd.exe",
        r"C:\x\windows\System32\powershell.exe",
        r"C:\Program Files\x\windows\sub\openconsole.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "nested \\windows\\ must not verify or classify as IDE: {p} -> {d:?}"
        );
    }
}

/// Contract: paths with `.` or `..` escaping or inside the root must not count.
#[test]
fn bug_cmd_and_powershell_accept_dot_segments_in_windows_path() {
    for p in [
        r"C:\Windows\..\Games\cmd.exe",
        r"C:\Windows\.\System32\cmd.exe",
        r"C:\Windows\..\Games\powershell.exe",
        r"C:\Windows\.\System32\powershell.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "dot segment must not verify: {p} -> {d:?}"
        );
    }
}

/// Contract: only `<drive>:\...` counts; UNC (`\\server\share\...`), `\\.\`, and
/// non-ASCII-letter drives (`1:`, `ñ:`) must not verify via `\\windows\\`.
#[test]
fn bug_cmd_accepts_unc_device_and_non_letter_drives() {
    for p in [
        r"\\server\share\windows\System32\cmd.exe",
        r"\\.\C:\Windows\System32\cmd.exe",
        r"1:\Windows\System32\cmd.exe",
        r"ñ:\Windows\System32\cmd.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "non-drive/UNC path must not verify: {p} -> {d:?}"
        );
    }
}

/// Contract: paths leaving a root via `.`/`..` must not count, nor UNC or non-letter drives.
#[test]
fn bug_standard_root_scoop_and_steamapps_accept_dot_segments_and_unc() {
    for p in [
        r"C:\Users\alice\scoop\apps\..\Discord\discord.exe",
        r"C:\Users\alice\scoop\apps\.\Discord\discord.exe",
        r"C:\Games\steamapps\common\..\..\Temp2\Discord\discord.exe",
        r"\\server\share\steamapps\common\Discord\discord.exe",
        r"\\.\C:\steamapps\common\Discord\discord.exe",
        r"1:\steamapps\common\Discord\discord.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "scoop/steamapps bypass must not verify: {p} -> {d:?}"
        );
    }
}

/// Contract: paths with `.` or `..` must not verify even for `need_std_root: false` apps.
#[test]
fn bug_known_apps_without_std_root_accept_dot_segments() {
    for p in [
        r"C:\obs-studio\..\Games\obs64.exe",
        r"C:\steam\..\Games\steam.exe",
        r"C:\blender\..\Games\blender.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "dot segment escape must not verify: {p} -> {d:?}"
        );
    }
}

/// Contract: `<drive>:\users\<name>\appdata\local|roaming\...` requires a real user name,
/// not whitespace-only `" "` or all-dots `"..."` (which Win32 strips to empty).
#[test]
fn bug_appdata_accepts_whitespace_or_all_dots_username() {
    for p in [
        r"C:\Users\ \AppData\Local\Discord\discord.exe",
        r"C:\Users\   \AppData\Local\Discord\discord.exe",
        r"C:\Users\...\AppData\Local\Discord\discord.exe",
    ] {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "invalid blank/dots username must not verify: {p} -> {d:?}"
        );
    }
}

// ============================================================================
// New coverage: borders, slashes, UNC, drives, Unicode, trailing spaces/dots, `.` / `..`
// ============================================================================

#[test]
fn borders_empty_drive_root_and_double_trailing_slash() {
    // Empty path and empty exe
    let d = classify_process(&input("", ""));
    assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM);
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
    assert!(!d.path_verified);

    // `C:\` alone
    let d = classify_process(&input(r"C:\", ""));
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
    assert!(!d.path_verified);

    // `C:\` with fallback Windows exe_name -> not verified
    let d = classify_process(&input(r"C:\", "dwm.exe"));
    assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM);
    assert!(!d.path_verified);

    // Exact root without trailing slash as parent: `C:\Windows\dwm.exe` IS verified
    let d = classify_process(&input(r"C:\Windows\dwm.exe", ""));
    assert!(d.path_verified);

    // Double trailing slash after root directory with fallback exe_name
    let d = classify_process(&input(r"C:\Windows\\", "dwm.exe"));
    assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM);
    assert!(d.path_verified);

    // Double trailing slash after full exe path without fallback
    let d = classify_process(&input(r"C:\Windows\System32\dwm.exe\\", ""));
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
    assert!(!d.path_verified);
}

#[test]
fn forward_slashes_and_case_variants_match_canonical() {
    let pairs = [
        (
            r"C:\Windows\System32\dwm.exe",
            "c:/WINDOWS/sYsTeM32/DWM.EXE",
        ),
        (
            r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            "c:/program files (x86)/microsoft/edge/application/msedge.exe",
        ),
        (
            r"C:\Users\Alice\AppData\Roaming\Spotify\Spotify.exe",
            "c:/users/alice/appdata/roaming/spotify/spotify.exe",
        ),
    ];
    for (win, fwd) in pairs {
        assert_eq!(
            classify_process(&input(win, "")),
            classify_process(&input(fwd, "")),
            "{win} vs {fwd}"
        );
    }
}

#[test]
fn unc_device_weird_drives_and_unicode_lookalikes_do_not_verify() {
    let bad_win = [
        r"\\server\share\Windows\System32\dwm.exe",
        r"\\?\UNC\server\share\Windows\System32\dwm.exe",
        r"\\.\C:\Windows\System32\dwm.exe",
        r"1:\Windows\System32\dwm.exe",
        r"ñ:\Windows\System32\dwm.exe",
        "С:\\Windows\\System32\\dwm.exe",       // Cyrillic Es (U+0421)
        "C:\\Wіndows\\System32\\dwm.exe",       // Cyrillic Byelorussian-Ukrainian I (U+0456)
        "C:\\ǀWindows\\System32\\dwm.exe",      // Dental click U+01C0
        "C:\\Windowsǀ\\System32\\dwm.exe",      // Dental click U+01C0
        "C:ǀWindowsǀSystem32ǀdwm.exe",          // Dental click instead of backslash
        "Ｃ:\\Windows\\System32\\dwm.exe",      // Fullwidth C
        // Kelvin sign (U+212A) drive left out: it lowercases to ASCII `k`, and Windows never reports such a drive.
        r"C:\Windows.\System32\dwm.exe",        // Trailing dot on component
        r"C:\Windows \System32\dwm.exe",        // Trailing space on component
        r"C:\Windows\..\Windows\System32\dwm.exe",
        r"C:\Windows\.\System32\dwm.exe",
        r"C:\.\Windows\System32\dwm.exe",
        r"C:\..\Windows\System32\dwm.exe",
        r"\\?\C:\Windows\..\Games\dwm.exe",
        "C:/Windows/../Games/dwm.exe",
    ];
    for p in bad_win {
        let d = classify_process(&input(p, ""));
        assert!(!d.path_verified, "must not verify: {p:?} -> {d:?}");
    }

    let bad_std = [
        r"\\server\share\Program Files\Google\Chrome\Application\chrome.exe",
        r"\\.\C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"1:\Program Files\Google\Chrome\Application\chrome.exe",
        r"ñ:\Users\alice\AppData\Local\Discord\discord.exe",
        "C:\\Program Fіles\\Google\\Chrome\\Application\\chrome.exe",
        r"C:\Program Files.\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files \Google\Chrome\Application\chrome.exe",
        r"C:\Users\alice\AppData.\Local\Discord\discord.exe",
        r"C:\Users\alice\AppData \Local\Discord\discord.exe",
        r"C:\Users\alice\AppData\Local.\Discord\discord.exe",
        r"C:\Users\alice\AppData\Local \Discord\discord.exe",
        r"C:\Users\\AppData\Local\Discord\discord.exe",
        r"C:\Users\..\AppData\Local\Discord\discord.exe",
        r"C:\Users\.\AppData\Local\Discord\discord.exe",
        r"C:\Users\alice\.\AppData\Local\Discord\discord.exe",
        r"C:\Users\alice\AppData\..\AppData\Local\Discord\discord.exe",
        r"C:\Users\alice\AppData\Local\.\Discord\discord.exe",
        r"C:\Users\alice\AppData\Local\Discord\..\Discord\discord.exe",
    ];
    for p in bad_std {
        let d = classify_process(&input(p, ""));
        assert!(
            !d.path_verified && d.process_class == VF_PROC_CLASS_UNKNOWN,
            "must not classify or verify: {p:?} -> {d:?}"
        );
    }
}

// ============================================================================
// Property-based tests (fixed seed, no disk persistence)
// ============================================================================

fn flip_ascii_case(s: &str, mask: u64) -> String {
    s.chars()
        .enumerate()
        .map(|(i, c)| {
            if c.is_ascii_alphabetic() && ((mask >> (i % 64)) & 1) == 1 {
                if c.is_ascii_lowercase() {
                    c.to_ascii_uppercase()
                } else {
                    c.to_ascii_lowercase()
                }
            } else {
                c
            }
        })
        .collect()
}

proptest! {
    #![proptest_config(ProptestConfig {
        cases: 256,
        failure_persistence: None,
        rng_seed: proptest::test_runner::RngSeed::Fixed(0x5eed_e150),
        ..ProptestConfig::default()
    })]

    /// Property (1): Prepending any folder prefix before `\windows\...` must never
    /// convert a non-system/user path into a verified system/IDE path.
    #[test]
    fn bug_prop_prefix_before_windows_never_verifies_user_path(
        drive in prop_oneof![Just("C:"), Just("D:")],
        prefix in "[a-zA-Z0-9_]{1,12}(\\\\[a-zA-Z0-9_]{1,12}){0,3}",
        exe in prop_oneof![Just("dwm.exe"), Just("svchost.exe"), Just("cmd.exe"), Just("powershell.exe")],
    ) {
        let user_path = format!("{drive}\\{prefix}\\{exe}");
        let with_win = format!("{drive}\\{prefix}\\Windows\\System32\\{exe}");
        let d_user = classify_process(&input(&user_path, ""));
        let d_prefixed = classify_process(&input(&with_win, ""));
        prop_assert!(!d_user.path_verified, "{user_path}");
        prop_assert!(!d_prefixed.path_verified, "prefix converted path to verified: {with_win} -> {d_prefixed:?}");
    }

    /// Property (2): Changing ASCII case of any path never changes the decision.
    #[test]
    fn prop_ascii_case_change_preserves_decision(
        path in prop_oneof![
            Just(r"C:\Windows\System32\dwm.exe".to_string()),
            Just(r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe".to_string()),
            Just(r"C:\Users\Alice\AppData\Local\Discord\app-1\Discord.exe".to_string()),
            Just(r"D:\Games\ZenlessZoneZero\ZenlessZoneZero.exe".to_string()),
            "([A-Za-z1ñ]:)?(\\\\|/)?[A-Za-z0-9_. ()ñǀ\\\\/]{0,64}",
        ],
        exe in "[A-Za-z0-9_.]{0,20}",
        mask1 in any::<u64>(),
        mask2 in any::<u64>(),
        fg3d in any::<bool>(),
        ac in any::<bool>(),
    ) {
        let mut a = input(&path, &exe);
        a.is_foreground_3d = fg3d;
        a.has_anticheat_signal = ac;

        let p2 = flip_ascii_case(&path, mask1);
        let e2 = flip_ascii_case(&exe, mask2);
        let mut b = input(&p2, &e2);
        b.is_foreground_3d = fg3d;
        b.has_anticheat_signal = ac;

        prop_assert_eq!(classify_process(&a), classify_process(&b));
    }

    /// Property (3): `classify_process` never panics on arbitrary valid UTF-8 strings.
    #[test]
    fn prop_never_panics_on_arbitrary_utf8(
        full_path in "\\PC{0,128}",
        exe_name in "\\PC{0,64}",
        ex_name in "\\PC{0,32}",
        ex_act in any::<u8>(),
        custom in "\\PC{0,32}",
        excluded in "\\PC{0,32}",
        heavy in any::<bool>(),
        fg3d in any::<bool>(),
        ac in any::<bool>(),
    ) {
        let exceptions = [(ex_name.as_str(), ex_act)];
        let custom_list = [custom.as_str()];
        let excluded_list = [excluded.as_str()];
        let inp = ProcessPolicyInput {
            full_path: &full_path,
            exe_name: &exe_name,
            category_actions: VfProcessCategoryActions::default(),
            exceptions: &exceptions,
            custom_game_exes: &custom_list,
            excluded_game_exes: &excluded_list,
            heavy_work_as_game: heavy,
            is_foreground_3d: fg3d,
            has_anticheat_signal: ac,
        };
        let _ = classify_process(&inp);
    }
}
