//! Behavior tests that pin `policy::process_class` (written against surviving cargo-mutants mutants).

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

const SPOTIFY: &str = r"C:\Users\u\AppData\Roaming\Spotify\spotify.exe";

// --- is_valid_bg_action / are_valid ---

#[test]
fn only_the_three_background_actions_are_valid() {
    assert!(is_valid_bg_action(VF_PROC_ACTION_DO_NOT_TOUCH));
    assert!(is_valid_bg_action(VF_PROC_ACTION_ECO_ONLY));
    assert!(is_valid_bg_action(VF_PROC_ACTION_THROTTLE));
    assert!(!is_valid_bg_action(VF_PROC_ACTION_BOOST_AS_GAME));
    assert!(!is_valid_bg_action(4));
    assert!(!is_valid_bg_action(255));
}

#[test]
fn default_category_actions_are_valid() {
    assert!(VfProcessCategoryActions::default().are_valid());
}

#[test]
fn each_category_field_with_an_invalid_action_makes_the_set_invalid() {
    let d = VfProcessCategoryActions::default;
    let bad = VF_PROC_ACTION_BOOST_AS_GAME;
    let cases = [
        VfProcessCategoryActions { recording_streaming: bad, ..d() },
        VfProcessCategoryActions { calls_voice: bad, ..d() },
        VfProcessCategoryActions { messaging: bad, ..d() },
        VfProcessCategoryActions { browsers: bad, ..d() },
        VfProcessCategoryActions { stores_launchers: bad, ..d() },
        VfProcessCategoryActions { music: bad, ..d() },
        VfProcessCategoryActions { cloud_sync: 200, ..d() },
    ];
    for (n, c) in cases.iter().enumerate() {
        assert!(!c.are_valid(), "case {n}");
    }
}

// --- Windows processes: path_verified (in_windows_root) ---

#[test]
fn a_windows_process_in_the_windows_folder_is_path_verified() {
    for p in [
        r"C:\Windows\System32\svchost.exe",
        r"D:\Windows\svchost.exe",
        r"C:\ProgramData\Microsoft\Windows Defender\Platform\svchost.exe",
        r"C:\ProgramData\VegaFlow\svchost.exe",
        r"C:\Program Files\WindowsApps\Pkg\svchost.exe",
        r"C:\ProgramData\VegaFlow\bin\svchost.exe",
    ] {
        let d = classify_process(&input(p, "svchost.exe"));
        assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM, "{p}");
        assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH, "{p}");
        assert!(d.path_verified, "{p}");
    }
}

#[test]
fn a_windows_named_process_outside_the_windows_folder_is_not_path_verified() {
    let d = classify_process(&input(r"C:\Users\u\Downloads\svchost.exe", "svchost.exe"));
    assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM);
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
    assert!(!d.path_verified);
}

#[test]
fn a_windows_named_process_without_a_path_is_not_path_verified() {
    let d = classify_process(&input("", "svchost.exe"));
    assert_eq!(d.process_class, VF_PROC_CLASS_WINDOWS_SYSTEM);
    assert!(!d.path_verified);
}

// --- find_exception (through classify_process) ---

#[test]
fn an_exception_for_the_same_exe_wins_over_the_category_action() {
    let ex = [("spotify.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(SPOTIFY, "spotify.exe");
    i.exceptions = &ex;
    let d = classify_process(&i);
    assert_eq!(d.process_class, VF_PROC_CLASS_MUSIC);
    assert_eq!(d.action, VF_PROC_ACTION_THROTTLE);
    assert!(d.path_verified);
}

#[test]
fn an_exception_for_another_exe_does_not_apply() {
    let ex = [("other.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(SPOTIFY, "spotify.exe");
    i.exceptions = &ex;
    assert_eq!(classify_process(&i).action, VF_PROC_ACTION_ECO_ONLY);
}

#[test]
fn an_exception_with_an_invalid_action_is_ignored() {
    let ex = [("spotify.exe", VF_PROC_ACTION_BOOST_AS_GAME)];
    let mut i = input(SPOTIFY, "spotify.exe");
    i.exceptions = &ex;
    assert_eq!(classify_process(&i).action, VF_PROC_ACTION_ECO_ONLY);
}

#[test]
fn an_exception_given_as_a_path_matches_by_file_name_and_the_first_valid_one_wins() {
    let ex = [
        (r"C:\x\other.exe", VF_PROC_ACTION_THROTTLE),
        (r"C:\x\Spotify.EXE", 9),
        (r"D:\y\spotify.exe", VF_PROC_ACTION_DO_NOT_TOUCH),
        (r"E:\z\spotify.exe", VF_PROC_ACTION_THROTTLE),
    ];
    let mut i = input(SPOTIFY, "spotify.exe");
    i.exceptions = &ex;
    assert_eq!(classify_process(&i).action, VF_PROC_ACTION_DO_NOT_TOUCH);
}

// --- unknown executables: exceptions apply, spoofed known names ignore them ---

#[test]
fn an_exception_applies_to_an_unknown_executable() {
    let ex = [("mytool.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(r"C:\Tools\mytool.exe", "mytool.exe");
    i.exceptions = &ex;
    let d = classify_process(&i);
    assert_eq!(d.process_class, VF_PROC_CLASS_UNKNOWN);
    assert_eq!(d.action, VF_PROC_ACTION_THROTTLE);
    assert!(!d.path_verified);
}

#[test]
fn an_unknown_executable_without_exception_is_left_alone() {
    let d = classify_process(&input(r"C:\Tools\mytool.exe", "mytool.exe"));
    assert_eq!(d.process_class, VF_PROC_CLASS_UNKNOWN);
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
}

#[test]
fn a_spoofed_known_name_in_the_wrong_folder_ignores_the_exception() {
    let ex = [("discord.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(r"C:\Temp\discord.exe", "discord.exe");
    i.exceptions = &ex;
    let d = classify_process(&i);
    assert_eq!(d.process_class, VF_PROC_CLASS_UNKNOWN);
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
    assert!(!d.path_verified);
}

#[test]
fn a_known_name_under_a_temp_folder_is_a_spoof_even_inside_a_standard_install_root() {
    // The path satisfies the standard root and the name marker; only the temp rule rejects it.
    let ex = [("discord.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(r"C:\Users\u\AppData\Local\Temp\Discord\discord.exe", "discord.exe");
    i.exceptions = &ex;
    let d = classify_process(&i);
    assert_eq!(d.process_class, VF_PROC_CLASS_UNKNOWN);
    assert_eq!(d.action, VF_PROC_ACTION_DO_NOT_TOUCH);
}

#[test]
fn the_same_known_name_in_the_real_install_folder_is_recognised() {
    let ex = [("discord.exe", VF_PROC_ACTION_THROTTLE)];
    let mut i = input(r"C:\Users\u\AppData\Local\Discord\app-1\discord.exe", "discord.exe");
    i.exceptions = &ex;
    let d = classify_process(&i);
    assert_eq!(d.process_class, VF_PROC_CLASS_CALLS_VOICE);
    assert_eq!(d.action, VF_PROC_ACTION_THROTTLE);
}
