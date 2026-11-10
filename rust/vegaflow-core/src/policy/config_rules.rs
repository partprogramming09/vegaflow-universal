//! The limits of each `config.json` value and which program names may be listed as a game.
//! Vectors shared with C++ Test 45: `tests/vectors/config_rules.tsv`.

/// One numeric setting: where it lives, its accepted range and its factory value.
pub struct NumberRule {
    pub key: &'static str,
    pub in_memory: bool,
    pub min: u64,
    pub max: u64,
    pub default: u64,
}

pub const POLL_INTERVAL_MS: NumberRule = NumberRule { key: "poll_interval_ms", in_memory: false, min: 250, max: 60_000, default: 1500 };
pub const TIMER_RESOLUTION_NS100: NumberRule =
    NumberRule { key: "timer_resolution_ns100", in_memory: false, min: 5000, max: 156_250, default: 5000 };
pub const MIN_FREE_RAM_MB: NumberRule = NumberRule { key: "min_free_ram_mb", in_memory: true, min: 128, max: 1_048_576, default: 1200 };
pub const MIN_STANDBY_MB: NumberRule = NumberRule { key: "min_standby_mb", in_memory: true, min: 128, max: 1_048_576, default: 1500 };
pub const COOLDOWN_SECONDS: NumberRule = NumberRule { key: "cooldown_seconds", in_memory: true, min: 10, max: 3600, default: 90 };
pub const MAX_GAME_DISK_READ_MB_S: NumberRule =
    NumberRule { key: "max_game_disk_read_mb_s", in_memory: true, min: 1, max: 100_000, default: 15 };

/// Most program names accepted in `custom_game_exes`.
pub const MAX_GAME_EXES: usize = 1024;

/// Windows programs that are never a game: treating one as a game would change how the system itself is scheduled.
const WINDOWS_PROCESSES: &[&str] = &[
    "smss.exe", "csrss.exe", "wininit.exe", "services.exe", "lsass.exe", "lsaiso.exe", "svchost.exe",
    "fontdrvhost.exe", "winlogon.exe", "sihost.exe", "spoolsv.exe", "dwm.exe", "explorer.exe", "taskhostw.exe",
    "shellexperiencehost.exe", "startmenuexperiencehost.exe", "searchhost.exe", "searchindexer.exe", "searchapp.exe",
    "systemsettings.exe", "applicationframehost.exe", "textinputhost.exe", "runtimebroker.exe", "ctfmon.exe",
    "securityhealthservice.exe", "securityhealthsystray.exe", "smartscreen.exe", "msmpeng.exe", "audiodg.exe",
    "conhost.exe", "taskmgr.exe", "wlanext.exe", "dashost.exe", "dllhost.exe", "wudfhost.exe", "wmpnetwk.exe",
    "logonui.exe", "userinit.exe", "consent.exe", "wmiprvse.exe", "trustedinstaller.exe", "tiworker.exe", "wermgr.exe",
    "vegaflow_daemon.exe",
];

/// Device names Windows reserves whatever the extension (`CON.exe` names the console, not a file).
const DEVICE_NAMES: &[&str] = &[
    "con", "prn", "aux", "nul", "com0", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9", "com¹",
    "com²", "com³", "lpt0", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9", "lpt¹", "lpt²", "lpt³",
];

/// The part before the first dot, without trailing spaces, is a reserved device name.
fn is_device_name(lower: &str) -> bool {
    let base = lower.split('.').next().unwrap_or("").trim_end_matches(' ');
    DEVICE_NAMES.contains(&base)
}

/// The value of `rule` when `text` (a JSON number as written) is a plain non-negative integer inside its range.
pub fn number_in_range(rule: &NumberRule, text: &str) -> Option<u64> {
    let plain = !text.is_empty() && text.bytes().all(|b| b.is_ascii_digit()) && (text == "0" || !text.starts_with('0'));
    if !plain {
        return None;
    }
    text.parse::<u64>().ok().filter(|n| (rule.min..=rule.max).contains(n))
}

/// Longest Windows file name, in UTF-16 units.
const MAX_FILE_NAME_UNITS: usize = 255;

/// A bare program file name ending in `.exe` that Windows reads exactly as written and that is not a Windows process.
pub fn is_allowed_game_exe(name: &str) -> bool {
    let lower = name.to_ascii_lowercase();
    let stem_ok = lower.strip_suffix(".exe").is_some_and(|stem| !stem.is_empty());
    let chars_ok = !name.chars().any(|c| (c as u32) < 0x20 || "\\/:*?\"<>|".contains(c));
    let ends_ok = !name.starts_with(' ') && !name.ends_with(' ') && !name.ends_with('.');
    let len_ok = name.encode_utf16().count() <= MAX_FILE_NAME_UNITS;
    stem_ok && chars_ok && ends_ok && len_ok && !is_device_name(&lower) && !WINDOWS_PROCESSES.contains(&lower.as_str())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_number_is_accepted_only_as_a_plain_integer_inside_the_range() {
        assert_eq!(number_in_range(&COOLDOWN_SECONDS, "10"), Some(10));
        assert_eq!(number_in_range(&COOLDOWN_SECONDS, "3600"), Some(3600));
        for bad in ["0", "9", "3601", "-10", "10.0", "1e2", "010", "", "18446744073709551616"] {
            assert_eq!(number_in_range(&COOLDOWN_SECONDS, bad), None, "accepted {bad}");
        }
    }

    #[test]
    fn every_factory_value_is_inside_its_own_range() {
        for r in [&POLL_INTERVAL_MS, &TIMER_RESOLUTION_NS100, &MIN_FREE_RAM_MB, &MIN_STANDBY_MB, &COOLDOWN_SECONDS,
            &MAX_GAME_DISK_READ_MB_S]
        {
            assert!(r.min > 0 && r.min <= r.default && r.default <= r.max, "{}", r.key);
        }
    }

    #[test]
    fn the_disk_limit_in_bytes_cannot_overflow() {
        assert!(MAX_GAME_DISK_READ_MB_S.max.checked_mul(1024 * 1024).is_some());
    }

    #[test]
    fn windows_processes_are_refused_whatever_the_case() {
        for name in WINDOWS_PROCESSES {
            assert!(!is_allowed_game_exe(name), "{name}");
            assert!(!is_allowed_game_exe(&name.to_ascii_uppercase()), "{name}");
        }
    }

    #[test]
    fn a_game_name_is_a_bare_exe_file_name() {
        assert!(is_allowed_game_exe("MyGame.exe"));
        assert!(is_allowed_game_exe("juego ñ.EXE"));
        assert!(is_allowed_game_exe(&format!("{}.exe", "a".repeat(251))));
        assert!(!is_allowed_game_exe(&format!("{}.exe", "a".repeat(252))));
        for bad in ["", ".exe", "game", "game.txt", "a\\game.exe", "a/game.exe", "c:game.exe", "game.exe ", " game.exe",
            "game.exe.", "ga\u{0}me.exe", "ga\u{1f}me.exe", "ga*me.exe", "ga?me.exe", "ga\"me.exe", "ga<me.exe",
            "ga>me.exe", "ga|me.exe"]
        {
            assert!(!is_allowed_game_exe(bad), "accepted {bad:?}");
        }
    }
}
