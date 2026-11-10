//! Reads the settings out of the text of `config.json` and writes them back without losing what it does not know.
//! A value outside its rules is ignored (factory value) and blocks saving, like a corrupt file.

use super::config_rules::{self as rules, NumberRule};
use super::json_text::{self, Value};

/// The file is not JSON, or its top level is not an object: every setting takes its factory value.
pub const VF_CONFIG_CORRUPT: u32 = 1 << 0;
/// At least one key broke its rule and took its factory value; `rejected_keys` says which.
pub const VF_CONFIG_KEY_REJECTED: u32 = 1 << 1;
/// Written by a newer VegaFlow (`version` above `CURRENT_VERSION`): read, never overwritten.
pub const VF_CONFIG_NEWER_VERSION: u32 = 1 << 2;
/// No `version` (or 0): the format before phase 1b. Saving moves it to the current one.
pub const VF_CONFIG_OLD_VERSION: u32 = 1 << 3;
/// Empty or only whitespace: factory values, and saving may create the content.
pub const VF_CONFIG_EMPTY: u32 = 1 << 4;
/// Any of these and the file must not be overwritten.
pub const VF_CONFIG_BLOCKS_SAVE: u32 = VF_CONFIG_CORRUPT | VF_CONFIG_KEY_REJECTED | VF_CONFIG_NEWER_VERSION;

pub const VF_CONFIG_KEY_VERSION: u32 = 1 << 0;
pub const VF_CONFIG_KEY_POLL_INTERVAL_MS: u32 = 1 << 1;
pub const VF_CONFIG_KEY_TIMER_RESOLUTION_NS100: u32 = 1 << 2;
pub const VF_CONFIG_KEY_TIMER_FORCED_MANUAL: u32 = 1 << 3;
pub const VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS: u32 = 1 << 4;
pub const VF_CONFIG_KEY_MIN_FREE_RAM_MB: u32 = 1 << 5;
pub const VF_CONFIG_KEY_MIN_STANDBY_MB: u32 = 1 << 6;
pub const VF_CONFIG_KEY_COOLDOWN_SECONDS: u32 = 1 << 7;
pub const VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S: u32 = 1 << 8;
pub const VF_CONFIG_KEY_CUSTOM_GAME_EXES: u32 = 1 << 9;
/// `memory` is there but is not an object.
pub const VF_CONFIG_KEY_MEMORY: u32 = 1 << 10;

pub const CURRENT_VERSION: u64 = 1;

/// Every setting VegaFlow reads from `config.json`.
#[derive(Debug, Clone, PartialEq)]
pub struct Settings {
    pub poll_interval_ms: u32,
    pub timer_resolution_ns100: u32,
    pub timer_forced_manual: bool,
    pub restrict_game_cpu_sets: bool,
    pub min_free_ram_mb: u64,
    pub min_standby_mb: u64,
    pub cooldown_seconds: u32,
    pub max_game_disk_read_mb_s: u64,
    pub custom_game_exes: Vec<String>,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            poll_interval_ms: rules::POLL_INTERVAL_MS.default as u32,
            timer_resolution_ns100: rules::TIMER_RESOLUTION_NS100.default as u32,
            timer_forced_manual: false,
            restrict_game_cpu_sets: false,
            min_free_ram_mb: rules::MIN_FREE_RAM_MB.default,
            min_standby_mb: rules::MIN_STANDBY_MB.default,
            cooldown_seconds: rules::COOLDOWN_SECONDS.default as u32,
            max_game_disk_read_mb_s: rules::MAX_GAME_DISK_READ_MB_S.default,
            custom_game_exes: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Loaded {
    pub settings: Settings,
    /// `VF_CONFIG_*` status bits.
    pub status: u32,
    /// `VF_CONFIG_KEY_*` bits of the keys that took their factory value.
    pub rejected_keys: u32,
}

/// Why `save` wrote nothing.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SaveRefused {
    /// The current file has a `VF_CONFIG_BLOCKS_SAVE` status: the person decides first.
    Blocked(u32),
    /// A setting to write breaks its own rule.
    InvalidSetting(u32),
}

const VERSION: NumberRule = NumberRule { key: "version", in_memory: false, min: 0, max: u64::MAX, default: CURRENT_VERSION };

const NUMBER_KEYS: [(&NumberRule, u32); 6] = [
    (&rules::POLL_INTERVAL_MS, VF_CONFIG_KEY_POLL_INTERVAL_MS),
    (&rules::TIMER_RESOLUTION_NS100, VF_CONFIG_KEY_TIMER_RESOLUTION_NS100),
    (&rules::MIN_FREE_RAM_MB, VF_CONFIG_KEY_MIN_FREE_RAM_MB),
    (&rules::MIN_STANDBY_MB, VF_CONFIG_KEY_MIN_STANDBY_MB),
    (&rules::COOLDOWN_SECONDS, VF_CONFIG_KEY_COOLDOWN_SECONDS),
    (&rules::MAX_GAME_DISK_READ_MB_S, VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S),
];
const BOOL_KEYS: [(&str, u32); 2] = [
    ("timer_forced_manual", VF_CONFIG_KEY_TIMER_FORCED_MANUAL),
    ("restrict_game_cpu_sets", VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS),
];
const GAME_EXES_KEY: &str = "custom_game_exes";
const MEMORY_KEY: &str = "memory";

fn number_of(s: &Settings, rule: &NumberRule) -> u64 {
    match rule.key {
        "poll_interval_ms" => s.poll_interval_ms as u64,
        "timer_resolution_ns100" => s.timer_resolution_ns100 as u64,
        "min_free_ram_mb" => s.min_free_ram_mb,
        "min_standby_mb" => s.min_standby_mb,
        "cooldown_seconds" => s.cooldown_seconds as u64,
        _ => s.max_game_disk_read_mb_s,
    }
}

/// Only called with a value already inside the rule's range, so the u32 fields cannot be truncated.
fn set_number(s: &mut Settings, rule: &NumberRule, n: u64) {
    match rule.key {
        "poll_interval_ms" => s.poll_interval_ms = n as u32,
        "timer_resolution_ns100" => s.timer_resolution_ns100 = n as u32,
        "min_free_ram_mb" => s.min_free_ram_mb = n,
        "min_standby_mb" => s.min_standby_mb = n,
        "cooldown_seconds" => s.cooldown_seconds = n as u32,
        _ => s.max_game_disk_read_mb_s = n,
    }
}

fn bool_of(s: &Settings, key: &str) -> bool {
    if key == "timer_forced_manual" { s.timer_forced_manual } else { s.restrict_game_cpu_sets }
}

fn set_bool(s: &mut Settings, key: &str, b: bool) {
    if key == "timer_forced_manual" { s.timer_forced_manual = b } else { s.restrict_game_cpu_sets = b }
}

fn is_blank(bytes: &[u8]) -> bool {
    let rest = bytes.strip_prefix(b"\xEF\xBB\xBF").unwrap_or(bytes);
    rest.iter().all(|b| matches!(b, b' ' | b'\t' | b'\n' | b'\r'))
}

pub fn load(bytes: &[u8]) -> Loaded {
    let mut out = Loaded { settings: Settings::default(), status: 0, rejected_keys: 0 };
    if is_blank(bytes) {
        out.status = VF_CONFIG_EMPTY;
        return out;
    }
    let root = match json_text::parse(bytes) {
        Ok(v @ Value::Object(_)) => v,
        _ => {
            out.status = VF_CONFIG_CORRUPT;
            return out;
        }
    };
    let mut rejected = 0;

    let version = match root.get(VERSION.key) {
        None => 0,
        Some(Value::Number(t)) => rules::number_in_range(&VERSION, t).unwrap_or_else(|| {
            rejected |= VF_CONFIG_KEY_VERSION;
            CURRENT_VERSION
        }),
        Some(_) => {
            rejected |= VF_CONFIG_KEY_VERSION;
            CURRENT_VERSION
        }
    };
    let old = version == 0;

    let memory = match root.get(MEMORY_KEY) {
        None => None,
        Some(m @ Value::Object(_)) => Some(m),
        Some(_) => {
            rejected |= VF_CONFIG_KEY_MEMORY;
            None
        }
    };

    for (rule, bit) in NUMBER_KEYS {
        // The reader before phase 1b found the memory keys anywhere, so an old file may have them at the top level too.
        // Every place is checked; the memory object wins when both are valid.
        let places = if rule.in_memory {
            [memory.and_then(|m| m.get(rule.key)), if old { root.get(rule.key) } else { None }]
        } else {
            [root.get(rule.key), None]
        };
        let mut value = None;
        for found in places.into_iter().flatten() {
            match found {
                Value::Number(t) => match rules::number_in_range(rule, t) {
                    Some(n) => value = value.or(Some(n)),
                    None => rejected |= bit,
                },
                _ => rejected |= bit,
            }
        }
        if let (Some(n), 0) = (value, rejected & bit) {
            set_number(&mut out.settings, rule, n);
        }
    }

    for (key, bit) in BOOL_KEYS {
        match root.get(key) {
            None => {}
            Some(Value::Bool(b)) => set_bool(&mut out.settings, key, *b),
            Some(_) => rejected |= bit,
        }
    }

    match root.get(GAME_EXES_KEY) {
        None => {}
        Some(Value::Array(items)) if items.len() <= rules::MAX_GAME_EXES => {
            for item in items {
                match item {
                    Value::String(name) if rules::is_allowed_game_exe(name) => out.settings.custom_game_exes.push(name.clone()),
                    _ => rejected |= VF_CONFIG_KEY_CUSTOM_GAME_EXES,
                }
            }
        }
        Some(_) => rejected |= VF_CONFIG_KEY_CUSTOM_GAME_EXES,
    }

    out.rejected_keys = rejected;
    if rejected != 0 {
        out.status |= VF_CONFIG_KEY_REJECTED;
    }
    if old {
        out.status |= VF_CONFIG_OLD_VERSION;
    }
    if version > CURRENT_VERSION {
        out.status |= VF_CONFIG_NEWER_VERSION;
    }
    out
}

/// `VF_CONFIG_KEY_*` bits of the settings that break their own rule.
fn invalid_settings(s: &Settings) -> u32 {
    let mut bits = 0;
    for (rule, bit) in NUMBER_KEYS {
        if !(rule.min..=rule.max).contains(&number_of(s, rule)) {
            bits |= bit;
        }
    }
    if s.custom_game_exes.len() > rules::MAX_GAME_EXES || !s.custom_game_exes.iter().all(|n| rules::is_allowed_game_exe(n)) {
        bits |= VF_CONFIG_KEY_CUSTOM_GAME_EXES;
    }
    bits
}

fn set_member(members: &mut Vec<(String, Value)>, key: &str, value: Value) {
    match members.iter_mut().find(|(k, _)| k == key) {
        Some((_, v)) => *v = value,
        None => members.push((key.to_string(), value)),
    }
}

/// The new text of the file: `original` with the settings replaced, the rest kept, and `version` set to the current one.
pub fn save(original: &[u8], settings: &Settings) -> Result<String, SaveRefused> {
    let loaded = load(original);
    if loaded.status & VF_CONFIG_BLOCKS_SAVE != 0 {
        return Err(SaveRefused::Blocked(loaded.status));
    }
    let invalid = invalid_settings(settings);
    if invalid != 0 {
        return Err(SaveRefused::InvalidSetting(invalid));
    }
    let mut root = match json_text::parse(original) {
        Ok(Value::Object(members)) => members,
        _ => Vec::new(), // only an empty file gets here: load() already refused anything else
    };

    let version = Value::Number(CURRENT_VERSION.to_string());
    match root.iter_mut().find(|(k, _)| k == VERSION.key) {
        Some((_, v)) => *v = version,
        None => root.insert(0, (VERSION.key.to_string(), version)),
    }

    if loaded.status & VF_CONFIG_OLD_VERSION != 0 {
        // Moved into `memory` below: the current format reads the memory keys only there.
        root.retain(|(k, _)| !NUMBER_KEYS.iter().any(|(rule, _)| rule.in_memory && rule.key == k));
    }
    let mut memory = match root.iter().find(|(k, _)| k == MEMORY_KEY) {
        Some((_, Value::Object(m))) => m.clone(),
        _ => Vec::new(), // absent; load() already refused a memory that is not an object
    };
    for (rule, _) in NUMBER_KEYS {
        let value = Value::Number(number_of(settings, rule).to_string());
        set_member(if rule.in_memory { &mut memory } else { &mut root }, rule.key, value);
    }
    set_member(&mut root, MEMORY_KEY, Value::Object(memory));
    for (key, _) in BOOL_KEYS {
        set_member(&mut root, key, Value::Bool(bool_of(settings, key)));
    }

    let names = settings.custom_game_exes.iter().cloned().map(Value::String).collect();
    set_member(&mut root, GAME_EXES_KEY, Value::Array(names));
    Ok(json_text::write_pretty(&Value::Object(root)))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn defaults() -> Settings {
        Settings::default()
    }

    #[test]
    fn a_current_file_is_read_whole() {
        let l = load(
            br#"{"version": 1, "poll_interval_ms": 2000, "timer_resolution_ns100": 10000, "timer_forced_manual": true,
                 "restrict_game_cpu_sets": true,
                 "memory": {"min_free_ram_mb": 800, "min_standby_mb": 900, "cooldown_seconds": 30,
                            "max_game_disk_read_mb_s": 40},
                 "custom_game_exes": ["A.exe", "b.EXE"]}"#,
        );
        assert_eq!(l.status, 0);
        assert_eq!(l.rejected_keys, 0);
        assert_eq!(
            l.settings,
            Settings {
                poll_interval_ms: 2000,
                timer_resolution_ns100: 10000,
                timer_forced_manual: true,
                restrict_game_cpu_sets: true,
                min_free_ram_mb: 800,
                min_standby_mb: 900,
                cooldown_seconds: 30,
                max_game_disk_read_mb_s: 40,
                custom_game_exes: vec!["A.exe".into(), "b.EXE".into()],
            }
        );
    }

    #[test]
    fn a_corrupt_file_gives_factory_values_and_blocks_saving() {
        for bad in [&b"{"[..], b"[]", b"12", b"\"x\"", b"{\"a\":1,}", b"\xff"] {
            let l = load(bad);
            assert_eq!(l.settings, defaults());
            assert_eq!(l.status & VF_CONFIG_CORRUPT, VF_CONFIG_CORRUPT);
            assert_eq!(save(bad, &defaults()), Err(SaveRefused::Blocked(l.status)));
        }
    }

    #[test]
    fn an_empty_file_gives_factory_values_and_can_be_saved() {
        for empty in [&b""[..], b"  \r\n", b"\xEF\xBB\xBF"] {
            let l = load(empty);
            assert_eq!(l.settings, defaults());
            assert_eq!(l.status, VF_CONFIG_EMPTY);
            let text = save(empty, &defaults()).unwrap();
            assert_eq!(load(text.as_bytes()).status, 0);
        }
    }

    #[test]
    fn one_bad_value_rejects_only_its_key_and_blocks_saving() {
        let original = br#"{"version": 1, "poll_interval_ms": "fast", "memory": {"cooldown_seconds": 0, "min_free_ram_mb": 900}}"#;
        let l = load(original);
        assert_eq!(l.settings.poll_interval_ms, 1500);
        assert_eq!(l.settings.cooldown_seconds, 90);
        assert_eq!(l.settings.min_free_ram_mb, 900);
        assert_eq!(l.status, VF_CONFIG_KEY_REJECTED);
        assert_eq!(l.rejected_keys, VF_CONFIG_KEY_POLL_INTERVAL_MS | VF_CONFIG_KEY_COOLDOWN_SECONDS);
        assert_eq!(save(original, &l.settings), Err(SaveRefused::Blocked(VF_CONFIG_KEY_REJECTED)));
    }

    #[test]
    fn a_bad_game_name_drops_only_that_name() {
        let l = load(br#"{"version": 1, "custom_game_exes": ["explorer.exe", "MyGame.exe", 3]}"#);
        assert_eq!(l.settings.custom_game_exes, vec!["MyGame.exe".to_string()]);
        assert_eq!(l.rejected_keys, VF_CONFIG_KEY_CUSTOM_GAME_EXES);
    }

    #[test]
    fn too_many_game_names_reject_the_whole_list() {
        let names: Vec<String> = (0..=rules::MAX_GAME_EXES).map(|i| format!("\"g{i}.exe\"")).collect();
        let text = format!("{{\"version\": 1, \"custom_game_exes\": [{}]}}", names.join(","));
        let l = load(text.as_bytes());
        assert!(l.settings.custom_game_exes.is_empty());
        assert_eq!(l.rejected_keys, VF_CONFIG_KEY_CUSTOM_GAME_EXES);
    }

    #[test]
    fn memory_that_is_not_an_object_is_rejected() {
        let l = load(br#"{"version": 1, "memory": 5}"#);
        assert_eq!(l.rejected_keys, VF_CONFIG_KEY_MEMORY);
        assert_eq!(l.settings, defaults());
    }

    #[test]
    fn a_bad_version_is_rejected() {
        for v in ["\"1\"", "-1", "1.0", "null"] {
            let l = load(format!("{{\"version\": {v}}}").as_bytes());
            assert_eq!(l.rejected_keys, VF_CONFIG_KEY_VERSION, "{v}");
        }
    }

    #[test]
    fn a_newer_version_is_read_but_never_overwritten() {
        let original = br#"{"version": 2, "poll_interval_ms": 2000, "new_thing": {"x": 1}}"#;
        let l = load(original);
        assert_eq!(l.status, VF_CONFIG_NEWER_VERSION);
        assert_eq!(l.settings.poll_interval_ms, 2000);
        assert_eq!(save(original, &l.settings), Err(SaveRefused::Blocked(VF_CONFIG_NEWER_VERSION)));
    }

    #[test]
    fn an_old_file_is_read_and_saving_moves_it_to_the_current_version() {
        let original = br#"{"poll_interval_ms": 2000, "cooldown_seconds": 45, "memory": {"min_standby_mb": 700}}"#;
        let l = load(original);
        assert_eq!(l.status, VF_CONFIG_OLD_VERSION);
        assert_eq!(l.settings.poll_interval_ms, 2000);
        assert_eq!(l.settings.cooldown_seconds, 45, "the old reader found memory keys at the top level too");
        assert_eq!(l.settings.min_standby_mb, 700);
        let text = save(original, &l.settings).unwrap();
        let v = json_text::parse(text.as_bytes()).unwrap();
        assert_eq!(v.get("version"), Some(&Value::Number("1".into())));
        assert_eq!(v.get("cooldown_seconds"), None, "moved into memory");
        assert_eq!(v.get("memory").and_then(|m| m.get("cooldown_seconds")), Some(&Value::Number("45".into())));
        let again = load(text.as_bytes());
        assert_eq!(again.status, 0);
        assert_eq!(again.settings, l.settings);
    }

    #[test]
    fn in_an_old_file_the_memory_object_wins_over_the_top_level() {
        let l = load(br#"{"cooldown_seconds": 45, "memory": {"cooldown_seconds": 60}}"#);
        assert_eq!(l.settings.cooldown_seconds, 60);
    }

    #[test]
    fn in_an_old_file_a_bad_top_level_memory_key_is_rejected_even_if_memory_has_it() {
        let original = br#"{"cooldown_seconds": "x", "memory": {"cooldown_seconds": 60}}"#;
        let l = load(original);
        assert_eq!(l.rejected_keys, VF_CONFIG_KEY_COOLDOWN_SECONDS);
        assert_eq!(l.settings.cooldown_seconds, 90);
        assert_eq!(save(original, &l.settings), Err(SaveRefused::Blocked(VF_CONFIG_KEY_REJECTED | VF_CONFIG_OLD_VERSION)));
    }

    #[test]
    fn in_a_current_file_top_level_memory_keys_are_unknown_keys() {
        let original = br#"{"version": 1, "cooldown_seconds": 45}"#;
        let l = load(original);
        assert_eq!(l.settings.cooldown_seconds, 90);
        let v = json_text::parse(save(original, &l.settings).unwrap().as_bytes()).unwrap();
        assert_eq!(v.get("cooldown_seconds"), Some(&Value::Number("45".into())));
    }

    #[test]
    fn saving_keeps_unknown_keys_at_every_level_and_their_order() {
        let original = br#"{"my_note": "keep me", "version": 1, "poll_interval_ms": 2000,
            "memory": {"extra": [1.50, {"deep": true}], "cooldown_seconds": 30}, "zz": 1e5}"#;
        let mut s = load(original).settings;
        s.poll_interval_ms = 3000;
        s.cooldown_seconds = 120;
        let text = save(original, &s).unwrap();
        let v = json_text::parse(text.as_bytes()).unwrap();
        let Value::Object(members) = &v else { panic!("not an object") };
        let keys: Vec<&str> = members.iter().map(|(k, _)| k.as_str()).collect();
        assert_eq!(&keys[..5], &["my_note", "version", "poll_interval_ms", "memory", "zz"]);
        assert_eq!(v.get("my_note"), Some(&Value::String("keep me".into())));
        assert_eq!(v.get("zz"), Some(&Value::Number("1e5".into())));
        let memory = v.get("memory").unwrap();
        let extra = json_text::parse(br#"[1.50, {"deep": true}]"#).unwrap();
        assert_eq!(memory.get("extra"), Some(&extra));
        assert_eq!(memory.get("cooldown_seconds"), Some(&Value::Number("120".into())));
        assert_eq!(load(text.as_bytes()).settings, s);
    }

    #[test]
    fn saving_refuses_a_setting_that_breaks_its_rule() {
        let mut s = defaults();
        s.cooldown_seconds = 0;
        assert_eq!(save(b"", &s), Err(SaveRefused::InvalidSetting(VF_CONFIG_KEY_COOLDOWN_SECONDS)));
        let mut s = defaults();
        s.custom_game_exes = vec!["explorer.exe".into()];
        assert_eq!(save(b"", &s), Err(SaveRefused::InvalidSetting(VF_CONFIG_KEY_CUSTOM_GAME_EXES)));
    }

    /// Reads `tests/vectors/config_rules.tsv`: one file per row, holding only that key.
    #[test]
    fn values_match_the_shared_vectors() {
        const VECTORS: &str = include_str!("../../../../tests/vectors/config_rules.tsv");
        let mut checked = 0;
        for line in VECTORS.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let cells: Vec<&str> = line.split('\t').collect();
            assert_eq!(cells.len(), 4, "malformed row: {line}");
            let (id, key, value, expected) = (cells[0], cells[1], cells[2], cells[3]);
            let l = load(vector_file(key, value).as_bytes());
            assert_eq!(l.status & VF_CONFIG_CORRUPT, 0, "{id}: the vector file itself is not JSON");
            let accepted = l.rejected_keys == 0;
            assert_eq!(accepted, expected == "accepted", "{id}: {key} = {value}");
            checked += 1;
        }
        assert!(checked >= 69, "expected at least 69 vectors, found {checked}");
    }

    fn vector_file(key: &str, value: &str) -> String {
        match key {
            "game_exe" => format!("{{\"version\": 1, \"custom_game_exes\": [{value}]}}"),
            "min_free_ram_mb" | "min_standby_mb" | "cooldown_seconds" | "max_game_disk_read_mb_s" => {
                format!("{{\"version\": 1, \"memory\": {{\"{key}\": {value}}}}}")
            }
            _ => format!("{{\"version\": 1, \"{key}\": {value}}}"),
        }
    }

    mod properties {
        use super::super::*;
        use proptest::prelude::*;

        proptest! {
            #[test]
            fn load_never_panics(bytes in prop::collection::vec(any::<u8>(), 0..512)) {
                let _ = load(&bytes);
            }

            #[test]
            fn what_load_reads_from_a_saved_file_is_what_was_saved(
                poll in 250u32..=60_000, cooldown in 10u32..=3600, forced in any::<bool>(),
                disk in 1u64..=100_000, names in prop::collection::vec("[A-Za-z0-9 _-]{1,20}", 0..5),
                note in any::<String>(),
            ) {
                let names: Vec<String> = names.into_iter().map(|n| format!("x{}.exe", n.trim())).collect();
                let s = Settings { poll_interval_ms: poll, cooldown_seconds: cooldown, timer_forced_manual: forced,
                    max_game_disk_read_mb_s: disk, custom_game_exes: names, ..Settings::default() };
                let original = json_text::write_pretty(&Value::Object(vec![("note".into(), Value::String(note.clone()))]));
                let text = save(original.as_bytes(), &s).unwrap();
                let l = load(text.as_bytes());
                prop_assert_eq!(l.status, 0);
                prop_assert_eq!(&l.settings, &s);
                let written = json_text::parse(text.as_bytes()).unwrap();
                prop_assert_eq!(written.get("note"), Some(&Value::String(note)));
            }
        }
    }
}
