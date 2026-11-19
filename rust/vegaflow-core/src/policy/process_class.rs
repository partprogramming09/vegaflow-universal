//! Pure process classification and action policy (Phase 5).
//! Classifies a process from its full image path, executable name, foreground 3D state,
//! anti-cheat signals, user category actions, per-program exceptions, and custom/excluded games.
//! Shared vectors: `tests/vectors/process_class.tsv`.

use super::{config_rules, sandbox_core};

pub const VF_PROC_CLASS_UNKNOWN: u8 = 0;
pub const VF_PROC_CLASS_WINDOWS_SYSTEM: u8 = 1;
pub const VF_PROC_CLASS_RECORDING_STREAMING: u8 = 2;
pub const VF_PROC_CLASS_CALLS_VOICE: u8 = 3;
pub const VF_PROC_CLASS_MESSAGING: u8 = 4;
pub const VF_PROC_CLASS_BROWSERS: u8 = 5;
pub const VF_PROC_CLASS_STORES_LAUNCHERS: u8 = 6;
pub const VF_PROC_CLASS_MUSIC: u8 = 7;
pub const VF_PROC_CLASS_CLOUD_SYNC: u8 = 8;
pub const VF_PROC_CLASS_DEVELOPER_IDE: u8 = 9;
pub const VF_PROC_CLASS_HEAVY_WORK: u8 = 10;
pub const VF_PROC_CLASS_POSSIBLE_GAME: u8 = 11;

pub const VF_PROC_ACTION_DO_NOT_TOUCH: u8 = 0;
pub const VF_PROC_ACTION_ECO_ONLY: u8 = 1;
pub const VF_PROC_ACTION_THROTTLE: u8 = 2;
pub const VF_PROC_ACTION_BOOST_AS_GAME: u8 = 3;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VfProcessCategoryActions {
    pub recording_streaming: u8,
    pub calls_voice: u8,
    pub messaging: u8,
    pub browsers: u8,
    pub stores_launchers: u8,
    pub music: u8,
    pub cloud_sync: u8,
}

impl Default for VfProcessCategoryActions {
    fn default() -> Self {
        Self {
            recording_streaming: VF_PROC_ACTION_DO_NOT_TOUCH,
            calls_voice: VF_PROC_ACTION_DO_NOT_TOUCH,
            messaging: VF_PROC_ACTION_THROTTLE,
            browsers: VF_PROC_ACTION_THROTTLE,
            stores_launchers: VF_PROC_ACTION_THROTTLE,
            music: VF_PROC_ACTION_ECO_ONLY,
            cloud_sync: VF_PROC_ACTION_THROTTLE,
        }
    }
}

impl VfProcessCategoryActions {
    pub fn are_valid(&self) -> bool {
        [
            self.recording_streaming,
            self.calls_voice,
            self.messaging,
            self.browsers,
            self.stores_launchers,
            self.music,
            self.cloud_sync,
        ]
        .iter()
        .all(|&a| is_valid_bg_action(a))
    }
}

pub fn is_valid_bg_action(action: u8) -> bool {
    matches!(
        action,
        VF_PROC_ACTION_DO_NOT_TOUCH | VF_PROC_ACTION_ECO_ONLY | VF_PROC_ACTION_THROTTLE
    )
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfProcessDecision {
    pub process_class: u8,
    pub action: u8,
    pub protected_by_anticheat: bool,
    pub path_verified: bool,
}

pub struct ProcessPolicyInput<'a> {
    pub full_path: &'a str,
    pub exe_name: &'a str,
    pub category_actions: VfProcessCategoryActions,
    pub exceptions: &'a [(&'a str, u8)],
    pub custom_game_exes: &'a [&'a str],
    pub excluded_game_exes: &'a [&'a str],
    pub heavy_work_as_game: bool,
    pub is_foreground_3d: bool,
    pub has_anticheat_signal: bool,
}

fn normalize_win_path(raw: &str) -> String {
    let trimmed = raw.trim();
    let stripped = trimmed
        .strip_prefix("\\\\?\\")
        .or_else(|| trimmed.strip_prefix("//?/"))
        .unwrap_or(trimmed);
    stripped.replace('/', "\\").to_ascii_lowercase()
}

fn split_parent_and_exe(norm_path: &str, fallback_exe: &str) -> (String, String) {
    if norm_path.is_empty() {
        return (String::new(), normalize_win_path(fallback_exe));
    }
    match norm_path.rfind('\\') {
        Some(idx) => {
            let parent = &norm_path[..idx];
            let file = &norm_path[idx + 1..];
            let exe = if file.is_empty() {
                normalize_win_path(fallback_exe)
            } else {
                file.to_string()
            };
            (parent.to_string(), exe)
        }
        None => (String::new(), norm_path.to_string()),
    }
}

fn has_any_marker(haystack: &str, markers: &[&str]) -> bool {
    markers.iter().any(|m| haystack.contains(m))
}

/// True when `path` is `<drive>:` + `root` or lives under it (`root` has no trailing `\\`).
/// A plain `contains` would accept `d:\games\program files` and `c:\windowsx`.
fn under_drive_root(path: &str, root: &str) -> bool {
    let b = path.as_bytes();
    if b.len() < 2 || !b[0].is_ascii_alphabetic() || b[1] != b':' {
        return false;
    }
    let rest = &path[2..];
    rest == root || rest.strip_prefix(root).is_some_and(|r| r.starts_with('\\'))
}

/// `<drive>:\users\<name>\appdata\local|roaming`, exact component match.
fn in_user_appdata(path: &str) -> bool {
    let b = path.as_bytes();
    if b.len() < 2 || !b[0].is_ascii_alphabetic() || b[1] != b':' {
        return false;
    }
    let mut parts = path[2..].split('\\');
    parts.next() == Some("")
        && parts.next() == Some("users")
        && parts.next().is_some_and(|u| !u.is_empty())
        && parts.next() == Some("appdata")
        && matches!(parts.next(), Some("local") | Some("roaming"))
}

fn in_standard_install_root(parent: &str) -> bool {
    in_user_appdata(parent)
        || ["\\program files", "\\program files (x86)", "\\programdata\\chocolatey"]
            .iter()
            .any(|r| under_drive_root(parent, r))
        // Library folders can sit anywhere on any drive, so these two stay unanchored but component-bound.
        || has_any_marker(parent, &["\\scoop\\apps\\", "\\steamapps\\common\\"])
}

fn in_windows_root(parent: &str) -> bool {
    ["\\windows", "\\program files\\windowsapps", "\\programdata\\microsoft\\windows defender", "\\programdata\\vegaflow"]
        .iter()
        .any(|r| under_drive_root(parent, r))
}

#[derive(Clone, Copy)]
enum KnownRule {
    App {
        exe: &'static str,
        class: u8,
        need_std_root: bool,
        path_markers: &'static [&'static str],
    },
}

const KNOWN_APPS: &[KnownRule] = &[
    // Recording & streaming (default: DoNotTouch)
    KnownRule::App { exe: "obs64.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: false, path_markers: &["\\obs-studio", "\\obs studio"] },
    KnownRule::App { exe: "obs32.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: false, path_markers: &["\\obs-studio", "\\obs studio"] },
    KnownRule::App { exe: "streamlabs obs.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["streamlabs"] },
    KnownRule::App { exe: "streamlabs desktop.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["streamlabs"] },
    KnownRule::App { exe: "xsplit.core.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["xsplit"] },
    KnownRule::App { exe: "medal.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["medal"] },
    KnownRule::App { exe: "medalencoder.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["medal"] },
    KnownRule::App { exe: "overwolf.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["overwolf"] },
    KnownRule::App { exe: "overwolfbrowser.exe", class: VF_PROC_CLASS_RECORDING_STREAMING, need_std_root: true, path_markers: &["overwolf"] },

    // Calls & voice (default: DoNotTouch)
    KnownRule::App { exe: "discord.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["\\discord"] },
    KnownRule::App { exe: "discordcanary.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["\\discordcanary", "\\discord"] },
    KnownRule::App { exe: "discordptb.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["\\discordptb", "\\discord"] },
    KnownRule::App { exe: "teams.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["teams", "microsoft"] },
    KnownRule::App { exe: "ms-teams.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["msteams", "teams", "windowsapps"] },
    KnownRule::App { exe: "zoom.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["\\zoom"] },
    KnownRule::App { exe: "skype.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["skype", "windowsapps"] },
    KnownRule::App { exe: "ts3client_win64.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: false, path_markers: &["teamspeak"] },
    KnownRule::App { exe: "ts5.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: false, path_markers: &["teamspeak"] },
    KnownRule::App { exe: "mumble.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["mumble"] },
    KnownRule::App { exe: "webexmta.exe", class: VF_PROC_CLASS_CALLS_VOICE, need_std_root: true, path_markers: &["webex", "cisco"] },

    // Messaging (default: Throttle)
    KnownRule::App { exe: "slack.exe", class: VF_PROC_CLASS_MESSAGING, need_std_root: true, path_markers: &["\\slack"] },
    KnownRule::App { exe: "telegram.exe", class: VF_PROC_CLASS_MESSAGING, need_std_root: false, path_markers: &["telegram"] },
    KnownRule::App { exe: "whatsapp.exe", class: VF_PROC_CLASS_MESSAGING, need_std_root: true, path_markers: &["whatsapp", "windowsapps"] },
    KnownRule::App { exe: "whatsapp.root.exe", class: VF_PROC_CLASS_MESSAGING, need_std_root: true, path_markers: &["whatsapp", "windowsapps"] },
    KnownRule::App { exe: "signal.exe", class: VF_PROC_CLASS_MESSAGING, need_std_root: true, path_markers: &["signal"] },

    // Browsers (default: Throttle)
    KnownRule::App { exe: "chrome.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["google", "chrome"] },
    KnownRule::App { exe: "msedge.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["microsoft", "edge"] },
    KnownRule::App { exe: "firefox.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["mozilla", "firefox"] },
    KnownRule::App { exe: "brave.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["brave"] },
    KnownRule::App { exe: "opera.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["opera"] },
    KnownRule::App { exe: "operagx.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["opera"] },
    KnownRule::App { exe: "vivaldi.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["vivaldi"] },
    KnownRule::App { exe: "chromium.exe", class: VF_PROC_CLASS_BROWSERS, need_std_root: true, path_markers: &["chromium"] },

    // Stores & launchers (default: Throttle)
    KnownRule::App { exe: "steamwebhelper.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["\\steam"] },
    KnownRule::App { exe: "steam.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["\\steam"] },
    KnownRule::App { exe: "epicgameslauncher.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["epic games", "epicgames"] },
    KnownRule::App { exe: "epicwebhelper.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["epic games", "epicgames"] },
    KnownRule::App { exe: "battlenet.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["battle.net", "blizzard"] },
    KnownRule::App { exe: "origin.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["origin", "electronic arts"] },
    KnownRule::App { exe: "eadesktop.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["ea desktop", "electronic arts"] },
    KnownRule::App { exe: "eabackgroundservice.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["ea desktop", "electronic arts"] },
    KnownRule::App { exe: "ubisoftconnect.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["ubisoft"] },
    KnownRule::App { exe: "upc.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["ubisoft"] },
    KnownRule::App { exe: "galaxyclient.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["galaxy", "gog"] },
    KnownRule::App { exe: "riotclientservices.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["riot"] },
    KnownRule::App { exe: "riotclientux.exe", class: VF_PROC_CLASS_STORES_LAUNCHERS, need_std_root: false, path_markers: &["riot"] },

    // Music (default: EcoOnly)
    KnownRule::App { exe: "spotify.exe", class: VF_PROC_CLASS_MUSIC, need_std_root: true, path_markers: &["spotify", "windowsapps"] },
    KnownRule::App { exe: "applemusic.exe", class: VF_PROC_CLASS_MUSIC, need_std_root: true, path_markers: &["apple", "windowsapps"] },
    KnownRule::App { exe: "tidal.exe", class: VF_PROC_CLASS_MUSIC, need_std_root: true, path_markers: &["tidal"] },
    KnownRule::App { exe: "foobar2000.exe", class: VF_PROC_CLASS_MUSIC, need_std_root: false, path_markers: &["foobar2000"] },
    KnownRule::App { exe: "musicbee.exe", class: VF_PROC_CLASS_MUSIC, need_std_root: false, path_markers: &["musicbee"] },

    // Cloud sync (default: Throttle)
    KnownRule::App { exe: "onedrive.exe", class: VF_PROC_CLASS_CLOUD_SYNC, need_std_root: false, path_markers: &["onedrive", "microsoft"] },
    KnownRule::App { exe: "googledrivefs.exe", class: VF_PROC_CLASS_CLOUD_SYNC, need_std_root: true, path_markers: &["google", "drive"] },
    KnownRule::App { exe: "dropbox.exe", class: VF_PROC_CLASS_CLOUD_SYNC, need_std_root: true, path_markers: &["dropbox"] },
    KnownRule::App { exe: "nextcloud.exe", class: VF_PROC_CLASS_CLOUD_SYNC, need_std_root: true, path_markers: &["nextcloud"] },
    KnownRule::App { exe: "megasync.exe", class: VF_PROC_CLASS_CLOUD_SYNC, need_std_root: true, path_markers: &["mega"] },

    // IDEs & terminals (never treated as game, never throttled by background rules)
    KnownRule::App { exe: "code.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["vs code", "vscode", "code"] },
    KnownRule::App { exe: "devenv.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["visual studio"] },
    KnownRule::App { exe: "trae.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["trae"] },
    KnownRule::App { exe: "powershell.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: false, path_markers: &["\\windows\\", "\\powershell"] },
    KnownRule::App { exe: "pwsh.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["powershell", "windowsapps"] },
    KnownRule::App { exe: "cmd.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: false, path_markers: &["\\windows\\"] },
    KnownRule::App { exe: "windowsterminal.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["windowsterminal", "windowsapps"] },
    KnownRule::App { exe: "openconsole.exe", class: VF_PROC_CLASS_DEVELOPER_IDE, need_std_root: true, path_markers: &["windowsterminal", "windowsapps", "\\windows\\"] },

    // Heavy creative / build work (BoostAsGame only when heavy_work_as_game = true)
    KnownRule::App { exe: "blender.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["blender"] },
    KnownRule::App { exe: "resolve.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["davinci", "blackmagic"] },
    KnownRule::App { exe: "adobe premiere pro.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: true, path_markers: &["adobe", "premiere"] },
    KnownRule::App { exe: "handbrake.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["handbrake"] },
    KnownRule::App { exe: "handbrakecli.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["handbrake"] },
    KnownRule::App { exe: "rustc.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &[".rustup", ".cargo", "rust"] },
    KnownRule::App { exe: "cargo.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &[".rustup", ".cargo", "rust"] },
    KnownRule::App { exe: "clang.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["llvm", "clang", "mingw", "msys", "visual studio"] },
    KnownRule::App { exe: "clang++.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["llvm", "clang", "mingw", "msys", "visual studio"] },
    KnownRule::App { exe: "cl.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["visual studio", "msvc"] },
    KnownRule::App { exe: "msbuild.exe", class: VF_PROC_CLASS_HEAVY_WORK, need_std_root: false, path_markers: &["visual studio", "msbuild"] },
];

fn category_default_or_configured(class: u8, cfg: &VfProcessCategoryActions) -> u8 {
    let raw = match class {
        VF_PROC_CLASS_RECORDING_STREAMING => cfg.recording_streaming,
        VF_PROC_CLASS_CALLS_VOICE => cfg.calls_voice,
        VF_PROC_CLASS_MESSAGING => cfg.messaging,
        VF_PROC_CLASS_BROWSERS => cfg.browsers,
        VF_PROC_CLASS_STORES_LAUNCHERS => cfg.stores_launchers,
        VF_PROC_CLASS_MUSIC => cfg.music,
        VF_PROC_CLASS_CLOUD_SYNC => cfg.cloud_sync,
        _ => VF_PROC_ACTION_DO_NOT_TOUCH,
    };
    if is_valid_bg_action(raw) {
        raw
    } else {
        VF_PROC_ACTION_DO_NOT_TOUCH
    }
}

fn contains_exe(list: &[&str], exe_lower: &str) -> bool {
    list.iter().any(|item| {
        let norm = normalize_win_path(item);
        let (_, name) = split_parent_and_exe(&norm, &norm);
        name == exe_lower
    })
}

fn find_exception(exceptions: &[(&str, u8)], exe_lower: &str) -> Option<u8> {
    for &(name, action) in exceptions {
        let norm = normalize_win_path(name);
        let (_, clean) = split_parent_and_exe(&norm, &norm);
        if clean == exe_lower && is_valid_bg_action(action) {
            return Some(action);
        }
    }
    None
}

pub fn classify_process(input: &ProcessPolicyInput<'_>) -> VfProcessDecision {
    let norm_full = normalize_win_path(input.full_path);
    let (parent_dir, exe_lower) = split_parent_and_exe(&norm_full, input.exe_name);
    if exe_lower.is_empty() {
        return VfProcessDecision {
            process_class: VF_PROC_CLASS_WINDOWS_SYSTEM,
            action: VF_PROC_ACTION_DO_NOT_TOUCH,
            protected_by_anticheat: false,
            path_verified: false,
        };
    }

    // 1. Windows OS & VegaFlow processes: unconditional never-touch, cannot be overridden.
    if config_rules::is_windows_process(&exe_lower) {
        return VfProcessDecision {
            process_class: VF_PROC_CLASS_WINDOWS_SYSTEM,
            action: VF_PROC_ACTION_DO_NOT_TOUCH,
            protected_by_anticheat: false,
            path_verified: !parent_dir.is_empty() && in_windows_root(&parent_dir),
        };
    }

    // 2. Known application identity check (name + folder verification when path is known).
    let mut matched_known_exe = false;
    let mut verified_class: Option<u8> = None;
    for rule in KNOWN_APPS {
        let KnownRule::App { exe, class, need_std_root, path_markers } = *rule;
        if exe == exe_lower {
            matched_known_exe = true;
            if !parent_dir.is_empty()
                && !parent_dir.contains("\\temp\\")
                && !parent_dir.ends_with("\\temp")
                && (!need_std_root || in_standard_install_root(&parent_dir))
                && has_any_marker(&parent_dir, path_markers)
            {
                verified_class = Some(class);
                break;
            }
        }
    }

    if let Some(class) = verified_class {
        if class == VF_PROC_CLASS_DEVELOPER_IDE {
            return VfProcessDecision {
                process_class: VF_PROC_CLASS_DEVELOPER_IDE,
                action: VF_PROC_ACTION_DO_NOT_TOUCH,
                protected_by_anticheat: false,
                path_verified: true,
            };
        }
        if class == VF_PROC_CLASS_HEAVY_WORK {
            let is_excluded = contains_exe(input.excluded_game_exes, &exe_lower);
            let action = if input.heavy_work_as_game && !is_excluded {
                VF_PROC_ACTION_BOOST_AS_GAME
            } else {
                VF_PROC_ACTION_DO_NOT_TOUCH
            };
            return VfProcessDecision {
                process_class: VF_PROC_CLASS_HEAVY_WORK,
                action,
                protected_by_anticheat: false,
                path_verified: true,
            };
        }

        // Categorized background app: per-exe exception wins over category rule, which wins over default.
        let action = find_exception(input.exceptions, &exe_lower)
            .unwrap_or_else(|| category_default_or_configured(class, &input.category_actions));
        return VfProcessDecision {
            process_class: class,
            action,
            protected_by_anticheat: false,
            path_verified: true,
        };
    }

    // 3. Path did not match any genuine categorized app (includes spoofed discord.exe / obs64.exe in other folders!).
    let in_custom = contains_exe(input.custom_game_exes, &exe_lower);
    let is_excluded = contains_exe(input.excluded_game_exes, &exe_lower);

    if input.is_foreground_3d || in_custom {
        let ac_target = if norm_full.is_empty() { exe_lower.as_str() } else { norm_full.as_str() };
        let has_ac = input.has_anticheat_signal || sandbox_core::has_anticheat_signature(ac_target.as_bytes());
        return VfProcessDecision {
            process_class: VF_PROC_CLASS_POSSIBLE_GAME,
            action: if is_excluded { VF_PROC_ACTION_DO_NOT_TOUCH } else { VF_PROC_ACTION_BOOST_AS_GAME },
            protected_by_anticheat: has_ac,
            path_verified: false,
        };
    }

    // Custom exception only applies to non-spoofing unknown executables.
    let bg_action = if !matched_known_exe {
        find_exception(input.exceptions, &exe_lower).unwrap_or(VF_PROC_ACTION_DO_NOT_TOUCH)
    } else {
        VF_PROC_ACTION_DO_NOT_TOUCH
    };

    VfProcessDecision {
        process_class: VF_PROC_CLASS_UNKNOWN,
        action: bg_action,
        protected_by_anticheat: false,
        path_verified: false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse_class(s: &str) -> u8 {
        match s {
            "unknown" => VF_PROC_CLASS_UNKNOWN,
            "windows_system" => VF_PROC_CLASS_WINDOWS_SYSTEM,
            "recording_streaming" => VF_PROC_CLASS_RECORDING_STREAMING,
            "calls_voice" => VF_PROC_CLASS_CALLS_VOICE,
            "messaging" => VF_PROC_CLASS_MESSAGING,
            "browsers" => VF_PROC_CLASS_BROWSERS,
            "stores_launchers" => VF_PROC_CLASS_STORES_LAUNCHERS,
            "music" => VF_PROC_CLASS_MUSIC,
            "cloud_sync" => VF_PROC_CLASS_CLOUD_SYNC,
            "developer_ide" => VF_PROC_CLASS_DEVELOPER_IDE,
            "heavy_work" => VF_PROC_CLASS_HEAVY_WORK,
            "possible_game" => VF_PROC_CLASS_POSSIBLE_GAME,
            _ => panic!("unknown class in vector: {s}"),
        }
    }

    fn parse_action(s: &str) -> u8 {
        match s {
            "do_not_touch" => VF_PROC_ACTION_DO_NOT_TOUCH,
            "eco_only" => VF_PROC_ACTION_ECO_ONLY,
            "throttle" => VF_PROC_ACTION_THROTTLE,
            "boost_as_game" => VF_PROC_ACTION_BOOST_AS_GAME,
            _ => panic!("unknown action in vector: {s}"),
        }
    }

    #[test]
    fn shared_vectors_match() {
        const VECTORS: &str = include_str!("../../../../tests/vectors/process_class.tsv");
        let mut checked = 0;
        for line in VECTORS.lines().filter(|l| !l.trim().is_empty() && !l.starts_with('#')) {
            let cols: Vec<&str> = line.split('\t').collect();
            assert_eq!(cols.len(), 13, "malformed row: {line}");
            let id = cols[0];
            let full_path = cols[1];
            let mut cat = VfProcessCategoryActions::default();
            if cols[2] != "default" {
                for kv in cols[2].split(',') {
                    let (k, v) = kv.split_once('=').unwrap();
                    let a = parse_action(v);
                    match k {
                        "recording_streaming" => cat.recording_streaming = a,
                        "calls_voice" => cat.calls_voice = a,
                        "messaging" => cat.messaging = a,
                        "browsers" => cat.browsers = a,
                        "stores_launchers" => cat.stores_launchers = a,
                        "music" => cat.music = a,
                        "cloud_sync" => cat.cloud_sync = a,
                        _ => panic!("unknown category {k}"),
                    }
                }
            }
            let exceptions: Vec<(&str, u8)> = if cols[3] == "none" {
                Vec::new()
            } else {
                cols[3]
                    .split(',')
                    .map(|kv| {
                        let (k, v) = kv.split_once('=').unwrap();
                        (k, parse_action(v))
                    })
                    .collect()
            };
            let custom_games: Vec<&str> = if cols[4] == "none" { Vec::new() } else { cols[4].split(',').collect() };
            let excluded_games: Vec<&str> = if cols[5] == "none" { Vec::new() } else { cols[5].split(',').collect() };
            let heavy_work_as_game = cols[6] == "1";
            let is_foreground_3d = cols[7] == "1";
            let has_anticheat_signal = cols[8] == "1";

            let dec = classify_process(&ProcessPolicyInput {
                full_path,
                exe_name: "",
                category_actions: cat,
                exceptions: &exceptions,
                custom_game_exes: &custom_games,
                excluded_game_exes: &excluded_games,
                heavy_work_as_game,
                is_foreground_3d,
                has_anticheat_signal,
            });
            assert_eq!(dec.process_class, parse_class(cols[9]), "{id}: process_class");
            assert_eq!(dec.action, parse_action(cols[10]), "{id}: action");
            assert_eq!(dec.protected_by_anticheat, cols[11] == "1", "{id}: protected_by_anticheat");
            assert_eq!(dec.path_verified, cols[12] == "1", "{id}: path_verified");
            checked += 1;
        }
        assert!(checked >= 35, "expected >= 35 process_class vectors, got {checked}");
    }
}
