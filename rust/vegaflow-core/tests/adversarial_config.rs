//! Adversarial tests for config.json: json_text, config_rules, config_file, folder_trust and their C functions.
//! A test named `bug_*` reproduces a contract break and fails until the production code is fixed.

use std::os::raw::c_void;

use proptest::prelude::*;
use vegaflow_core::ffi::{
    vf_rust_config_load, vf_rust_config_save, vf_rust_only_admins_can_write, VfAceEntry, VfConfigLoad, VfConfigSettings,
    VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_CONFIG_SAVE_OK, VF_SID_ADMINISTRATORS, VF_SID_OTHER, VF_SID_SYSTEM,
    VF_SID_TRUSTED_INSTALLER,
};
use vegaflow_core::policy::config_file::*;
use vegaflow_core::policy::config_rules::{is_allowed_game_exe, MAX_GAME_EXES};
use vegaflow_core::policy::folder_trust::{only_admins_can_write, Ace, SidKind, WRITE_RIGHTS};
use vegaflow_core::policy::json_text::{parse, write_pretty, ParseError, Value, MAX_DEPTH};

// --- Helpers ---

fn num(s: &str) -> Value {
    Value::Number(s.to_string())
}

fn st(s: &str) -> Value {
    Value::String(s.to_string())
}

/// Small deterministic generator driven by a proptest seed, for formatting choices.
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        // xorshift64*
        let mut x = self.0 | 1;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
}

/// Any value `parse` can return. Numbers come from the full grammar, strings from any char.
fn any_value(depth: u32) -> impl Strategy<Value = Value> {
    let number = (any::<bool>(), "(0|[1-9][0-9]{0,25})", proptest::option::of("[0-9]{1,6}"), proptest::option::of(
        ("[eE]", "[+-]?", "[0-9]{1,4}"),
    ))
        .prop_map(|(neg, int, frac, exp)| {
            let mut s = String::new();
            if neg {
                s.push('-');
            }
            s.push_str(&int);
            if let Some(f) = frac {
                s.push('.');
                s.push_str(&f);
            }
            if let Some((e, sign, d)) = exp {
                s.push_str(&e);
                s.push_str(&sign);
                s.push_str(&d);
            }
            Value::Number(s)
        });
    let leaf = prop_oneof![
        Just(Value::Null),
        any::<bool>().prop_map(Value::Bool),
        number,
        any::<String>().prop_map(Value::String),
        "[\\x00-\\x1f\"\\\\\u{7f}\u{2028}\u{feff}a]{0,8}".prop_map(Value::String),
    ];
    leaf.prop_recursive(depth, 48, 5, |inner| {
        prop_oneof![
            prop::collection::vec(inner.clone(), 0..5).prop_map(Value::Array),
            prop::collection::btree_map(any::<String>(), inner, 0..5).prop_map(|m| Value::Object(m.into_iter().collect())),
        ]
    })
}

/// Writes `v` as valid RFC 8259 text with random whitespace, random escapes and random hex case.
fn write_random(out: &mut String, v: &Value, r: &mut Rng) {
    fn ws(out: &mut String, r: &mut Rng) {
        for _ in 0..r.below(3) {
            out.push([' ', '\t', '\n', '\r'][r.below(4) as usize]);
        }
    }
    fn hex(out: &mut String, unit: u32, r: &mut Rng) {
        let s = format!("{unit:04x}");
        out.push_str("\\u");
        out.push_str(&if r.below(2) == 0 { s } else { s.to_uppercase() });
    }
    fn string(out: &mut String, s: &str, r: &mut Rng) {
        out.push('"');
        for c in s.chars() {
            let must_escape = c == '"' || c == '\\' || (c as u32) < 0x20;
            match r.below(4) {
                0 => {
                    let mut buf = [0u16; 2];
                    for unit in c.encode_utf16(&mut buf) {
                        hex(out, *unit as u32, r);
                    }
                }
                1 if matches!(c, '"' | '\\' | '/' | '\u{8}' | '\u{c}' | '\n' | '\r' | '\t') => {
                    out.push_str(match c {
                        '"' => "\\\"",
                        '\\' => "\\\\",
                        '/' => "\\/",
                        '\u{8}' => "\\b",
                        '\u{c}' => "\\f",
                        '\n' => "\\n",
                        '\r' => "\\r",
                        _ => "\\t",
                    });
                }
                _ if must_escape => hex(out, c as u32, r),
                _ => out.push(c),
            }
        }
        out.push('"');
    }
    match v {
        Value::Null => out.push_str("null"),
        Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        Value::Number(n) => out.push_str(n),
        Value::String(s) => string(out, s, r),
        Value::Array(items) => {
            out.push('[');
            ws(out, r);
            for (i, item) in items.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                    ws(out, r);
                }
                write_random(out, item, r);
                ws(out, r);
            }
            out.push(']');
        }
        Value::Object(members) => {
            out.push('{');
            ws(out, r);
            for (i, (k, val)) in members.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                    ws(out, r);
                }
                string(out, k, r);
                ws(out, r);
                out.push(':');
                ws(out, r);
                write_random(out, val, r);
                ws(out, r);
            }
            out.push('}');
        }
    }
}

/// Reference recognizer for the RFC 8259 `number` production.
fn is_rfc_number(s: &str) -> bool {
    let b = s.as_bytes();
    let mut i = 0;
    if b.get(i) == Some(&b'-') {
        i += 1;
    }
    match b.get(i) {
        Some(b'0') => i += 1,
        Some(b'1'..=b'9') => {
            while matches!(b.get(i), Some(b'0'..=b'9')) {
                i += 1;
            }
        }
        _ => return false,
    }
    if b.get(i) == Some(&b'.') {
        i += 1;
        let start = i;
        while matches!(b.get(i), Some(b'0'..=b'9')) {
            i += 1;
        }
        if i == start {
            return false;
        }
    }
    if matches!(b.get(i), Some(b'e' | b'E')) {
        i += 1;
        if matches!(b.get(i), Some(b'+' | b'-')) {
            i += 1;
        }
        let start = i;
        while matches!(b.get(i), Some(b'0'..=b'9')) {
            i += 1;
        }
        if i == start {
            return false;
        }
    }
    i == b.len()
}

fn valid_settings() -> impl Strategy<Value = Settings> {
    (
        250u32..=60_000,
        5000u32..=156_250,
        any::<bool>(),
        any::<bool>(),
        128u64..=1_048_576,
        128u64..=1_048_576,
        10u32..=3600,
        1u64..=100_000,
        prop::collection::vec("[A-Za-z0-9_ñé-]{1,12}", 0..4),
    )
        .prop_map(|(poll, timer, forced, restrict, free, standby, cool, disk, names)| Settings {
            poll_interval_ms: poll,
            timer_resolution_ns100: timer,
            timer_forced_manual: forced,
            restrict_game_cpu_sets: restrict,
            min_free_ram_mb: free,
            min_standby_mb: standby,
            cooldown_seconds: cool,
            max_game_disk_read_mb_s: disk,
            custom_game_exes: names.into_iter().map(|n| format!("g{n}.exe")).collect(),
        })
}

const MEMORY_KNOWN: &[&str] = &["min_free_ram_mb", "min_standby_mb", "cooldown_seconds", "max_game_disk_read_mb_s"];

fn members(v: &Value) -> &[(String, Value)] {
    match v {
        Value::Object(m) => m,
        _ => panic!("not an object: {v:?}"),
    }
}

/// `sub` appears in `all` in the same relative order.
fn is_subsequence(sub: &[&str], all: &[&str]) -> bool {
    let mut it = all.iter();
    sub.iter().all(|s| it.any(|a| a == s))
}

// --- 1. json_text ---

#[test]
fn json_refuses_more_text_outside_rfc_8259() {
    let bad: &[&[u8]] = &[
        // whitespace RFC 8259 does not list
        b"\x0c{}",
        b"\x0b[]",
        "\u{a0}{}".as_bytes(),
        "{}\u{3000}".as_bytes(),
        b"{}\x00",
        b"\x00{}",
        // a BOM anywhere but the very start
        " \u{feff}{}".as_bytes(),
        "{}\u{feff}".as_bytes(),
        "[\u{feff}1]".as_bytes(),
        // literals and numbers
        b"True",
        b"NULL",
        b"nul",
        b"0x10",
        b"1_0",
        b"--1",
        b"-01",
        b"00",
        b"1.e5",
        b"1e5.5",
        b"1ee5",
        b"1e+-5",
        b".",
        b"-",
        b"- 1",
        // escapes
        b"\"\\U0041\"",
        b"\"\\u00G0\"",
        b"\"\\u 041\"",
        b"\"\\ud83d\\u0041\"",
        b"\"\\ud83d\\ud83d\"",
        b"\"\\ud83d\\\"",
        b"\"\\ud83dx\"",
        b"\"\\",
        b"\"\\u",
        b"\"\\ud83d\\u",
        // raw control characters in strings
        b"\"\t\"",
        b"\"\x1f\"",
        // invalid UTF-8 forms
        b"\"\xC0\x80\"",         // overlong NUL
        b"\"\xE0\x80\xAF\"",     // overlong '/'
        b"\"\xED\xA0\x80\"",     // CESU-8 lone surrogate
        b"\"\xF4\x90\x80\x80\"", // above U+10FFFF
        b"\"\x80\"",
        // structure
        b"[1 2]",
        b"{\"a\":1 \"b\":2}",
        b"{,}",
        b"[,]",
        b"[,1]",
        b"{\"a\"}",
        b"{\"a\":}",
        b"[\"a\":1]",
        b"{1:2}",
        b"{\"a\"::1}",
        b"{\"a\":1}]",
        b"[1]]",
        // repeated keys, also when spelled with escapes
        b"{\"a\":1,\"\\u0061\":2}",
        "{\"\\u00e9\":1,\"é\":2}".as_bytes(),
        b"{\"a\":{\"b\":1,\"b\":1}}",
        b"[{\"x\":1,\"x\":1}]",
    ];
    for b in bad {
        assert_eq!(parse(b), Err(ParseError), "accepted {:?}", String::from_utf8_lossy(b));
    }
}

#[test]
fn json_accepts_rfc_8259_corner_cases() {
    assert_eq!(parse(b"\"\x7f\"").unwrap(), st("\u{7f}"), "DEL needs no escape");
    assert_eq!(parse(b"\"\\u0000\"").unwrap(), st("\0"));
    assert_eq!(parse(b"\"\\uD83D\\uDE00\"").unwrap(), st("😀"), "upper-case hex");
    assert_eq!(parse("\"\u{2028}\u{feff}\"".as_bytes()).unwrap(), st("\u{2028}\u{feff}"));
    assert_eq!(parse(b"-0.0E-0").unwrap(), num("-0.0E-0"));
    assert_eq!(parse(b"\xEF\xBB\xBF[]").unwrap(), Value::Array(vec![]));
    assert_eq!(parse(b"\xEF\xBB\xBF7").unwrap(), num("7"));
    assert_eq!(
        parse(b"{\"a\\u0000\":1,\"a\":2}").unwrap(),
        Value::Object(vec![("a\0".into(), num("1")), ("a".into(), num("2"))]),
        "different keys after decoding are not repeated"
    );
    assert_eq!(parse(b"{\"\":1}").unwrap(), Value::Object(vec![("".into(), num("1"))]));
    assert_eq!(parse(b" \t\r\n null \t\r\n").unwrap(), Value::Null);
}

fn nested_objects(n: usize) -> String {
    format!("{}1{}", "{\"a\":".repeat(n), "}".repeat(n))
}

#[test]
fn json_depth_limit_counts_objects_and_mixed_containers() {
    assert!(parse(nested_objects(MAX_DEPTH).as_bytes()).is_ok());
    assert_eq!(parse(nested_objects(MAX_DEPTH + 1).as_bytes()), Err(ParseError));
    let half = MAX_DEPTH / 2;
    let mixed_ok = format!("{}1{}", "[{\"a\":".repeat(half), "}]".repeat(half));
    assert!(parse(mixed_ok.as_bytes()).is_ok());
    let mixed_deep = format!("{}1{}", "[{\"a\":".repeat(half + 1), "}]".repeat(half + 1));
    assert_eq!(parse(mixed_deep.as_bytes()), Err(ParseError));
    // A depth-1 sibling after a deep branch does not reset the count wrongly.
    let deep_then_sibling = format!("[{}, {}]", nested_objects(MAX_DEPTH - 1), nested_objects(MAX_DEPTH));
    assert_eq!(parse(deep_then_sibling.as_bytes()), Err(ParseError));
}

#[test]
fn json_huge_nesting_neither_panics_nor_overflows_a_small_stack() {
    // 256 KiB is a quarter of the default Windows main-thread stack.
    let handle = std::thread::Builder::new()
        .stack_size(256 * 1024)
        .spawn(|| {
            for text in [
                "{\"a\":".repeat(1_000_000),
                "[{\"a\":".repeat(500_000),
                "[".repeat(1_000_000) + &"]".repeat(1_000_000),
                nested_objects(MAX_DEPTH),
            ] {
                let _ = parse(text.as_bytes());
                if let Ok(v) = parse(text.as_bytes()) {
                    let _ = write_pretty(&v);
                }
            }
        })
        .unwrap();
    assert!(handle.join().is_ok());
}

#[test]
fn json_many_small_values_parse() {
    let items = vec!["0"; 200_000].join(",");
    let v = parse(format!("[{items}]").as_bytes()).unwrap();
    assert!(matches!(v, Value::Array(ref a) if a.len() == 200_000));
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 512, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5eed_0001), ..ProptestConfig::default() })]

    /// Any valid serialization (random whitespace, escapes and hex case) reads back as the same value.
    #[test]
    fn json_every_valid_spelling_reads_back(v in any_value(5), seed in any::<u64>(), bom in any::<bool>()) {
        let mut text = String::new();
        if bom { text.push('\u{feff}'); }
        let mut r = Rng(seed);
        write_random(&mut text, &v, &mut r);
        prop_assert_eq!(parse(text.as_bytes()), Ok(v));
    }

    /// `parse(write_pretty(v)) == v` and writing is stable.
    #[test]
    fn json_pretty_round_trip_and_idempotence(v in any_value(5)) {
        let text = write_pretty(&v);
        let back = parse(text.as_bytes());
        prop_assert_eq!(back.as_ref(), Ok(&v));
        prop_assert_eq!(write_pretty(&back.unwrap()), text);
    }

    /// Every proper prefix of a container document is incomplete and must be refused.
    #[test]
    fn json_every_proper_prefix_is_refused(items in prop::collection::vec(any_value(3), 0..4), seed in any::<u64>(), obj in any::<bool>()) {
        let v = if obj {
            Value::Object(items.into_iter().enumerate().map(|(i, x)| (format!("k{i}"), x)).collect())
        } else {
            Value::Array(items)
        };
        let mut text = String::new();
        write_random(&mut text, &v, &mut Rng(seed));
        for (i, _) in text.char_indices().skip(1) {
            prop_assert_eq!(parse(text[..i].as_bytes()), Err(ParseError), "prefix {:?}", &text[..i]);
        }
    }

    /// Number acceptance matches the RFC grammar exactly, and the text is kept as written.
    #[test]
    fn json_numbers_follow_the_grammar(s in "[-+.eE0-9]{1,10}") {
        let got = parse(s.as_bytes());
        if is_rfc_number(&s) {
            prop_assert_eq!(got, Ok(Value::Number(s.clone())));
        } else {
            prop_assert_eq!(got, Err(ParseError));
        }
    }

    /// Mutations of valid documents never panic, and anything accepted round-trips.
    #[test]
    fn json_mutated_documents_never_panic(v in any_value(4), edits in prop::collection::vec((any::<usize>(), any::<u8>(), 0u8..3), 1..6)) {
        let mut bytes = write_pretty(&v).into_bytes();
        for (pos, b, kind) in edits {
            if bytes.is_empty() { bytes.push(b); continue; }
            let p = pos % bytes.len();
            match kind {
                0 => bytes[p] = b,
                1 => bytes.insert(p, b),
                _ => { bytes.remove(p); }
            }
        }
        if let Ok(parsed) = parse(&bytes) {
            prop_assert_eq!(parse(write_pretty(&parsed).as_bytes()), Ok(parsed));
        }
    }
}

// --- 2. config_rules / config_file ---

/// Windows device names: `CON.exe` opens the console instead of a file on Windows 10 and earlier, so Windows does
/// not read these names as written (contract of `is_allowed_game_exe`).
#[test]
fn bug_game_name_accepts_windows_device_names() {
    let accepted: Vec<&str> = ["CON.exe", "nul.exe", "PRN.EXE", "aux.exe", "COM1.exe", "lpt9.exe", "COM\u{b9}.exe"]
        .into_iter()
        .filter(|n| is_allowed_game_exe(n))
        .collect();
    assert!(accepted.is_empty(), "device names accepted as game names: {accepted:?}");
}

/// `wmpnetwk.exe` (Windows Media Player network sharing service) is a Windows process the C++ detector already
/// excludes (`src/detect/process_detector.cpp`), but the Rust list lets it be saved as a game.
#[test]
fn bug_game_name_accepts_a_windows_process_the_cpp_detector_excludes() {
    assert!(!is_allowed_game_exe("wmpnetwk.exe"));
    assert!(!is_allowed_game_exe("WMPNetwk.exe"));
}

#[test]
fn game_name_more_bad_spellings_are_refused() {
    for bad in [
        "game.exe\u{0}",
        "game\u{7}.exe",
        "..\\game.exe",
        "\\\\?\\c:\\g.exe",
        "g.exe::$DATA",
        "game.exe..",
        "game.exe. ",
        "game.ex",
        "game.exe\n",
        "game.exe/",
        "g>.exe",
    ] {
        assert!(!is_allowed_game_exe(bad), "accepted {bad:?}");
    }
    assert!(is_allowed_game_exe(&format!("{}.exe", "😀".repeat(125))), "250 + 4 UTF-16 units");
    assert!(!is_allowed_game_exe(&format!("{}a.exe", "😀".repeat(126))), "256 UTF-16 units");
}

#[test]
fn every_rule_key_alone_rejects_only_its_own_bit_and_blocks_saving() {
    let base = r#"{"version": 1, "poll_interval_ms": 2000, "timer_resolution_ns100": 10000, "timer_forced_manual": true,
        "restrict_game_cpu_sets": true, "memory": {"min_free_ram_mb": 800, "min_standby_mb": 900,
        "cooldown_seconds": 30, "max_game_disk_read_mb_s": 40}, "custom_game_exes": ["A.exe"]}"#;
    let good = load(base.as_bytes());
    assert_eq!(good.status, 0);
    let cases: &[(&str, &str, u32)] = &[
        ("\"poll_interval_ms\": 2000", "\"poll_interval_ms\": {}", VF_CONFIG_KEY_POLL_INTERVAL_MS),
        ("\"timer_resolution_ns100\": 10000", "\"timer_resolution_ns100\": [10000]", VF_CONFIG_KEY_TIMER_RESOLUTION_NS100),
        ("\"timer_forced_manual\": true", "\"timer_forced_manual\": null", VF_CONFIG_KEY_TIMER_FORCED_MANUAL),
        ("\"restrict_game_cpu_sets\": true", "\"restrict_game_cpu_sets\": \"no\"", VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS),
        ("\"min_free_ram_mb\": 800", "\"min_free_ram_mb\": 8e2", VF_CONFIG_KEY_MIN_FREE_RAM_MB),
        ("\"min_standby_mb\": 900", "\"min_standby_mb\": -0", VF_CONFIG_KEY_MIN_STANDBY_MB),
        ("\"cooldown_seconds\": 30", "\"cooldown_seconds\": 30.0", VF_CONFIG_KEY_COOLDOWN_SECONDS),
        ("\"max_game_disk_read_mb_s\": 40", "\"max_game_disk_read_mb_s\": 18446744073709551617", VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S),
        ("[\"A.exe\"]", "[\"A.exe\", \"dwm.EXE\"]", VF_CONFIG_KEY_CUSTOM_GAME_EXES),
        ("[\"A.exe\"]", "{\"A.exe\": 1}", VF_CONFIG_KEY_CUSTOM_GAME_EXES),
        ("\"version\": 1", "\"version\": true", VF_CONFIG_KEY_VERSION),
    ];
    for (from, to, bit) in cases {
        let text = base.replacen(from, to, 1);
        assert_ne!(text, base, "{from}");
        let l = load(text.as_bytes());
        assert_eq!(l.status, VF_CONFIG_KEY_REJECTED, "{to}");
        assert_eq!(l.rejected_keys, *bit, "{to}");
        let mut expected = good.settings.clone();
        // The rejected key takes its factory value; the others keep theirs.
        let d = Settings::default();
        match *bit {
            VF_CONFIG_KEY_POLL_INTERVAL_MS => expected.poll_interval_ms = d.poll_interval_ms,
            VF_CONFIG_KEY_TIMER_RESOLUTION_NS100 => expected.timer_resolution_ns100 = d.timer_resolution_ns100,
            VF_CONFIG_KEY_TIMER_FORCED_MANUAL => expected.timer_forced_manual = d.timer_forced_manual,
            VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS => expected.restrict_game_cpu_sets = d.restrict_game_cpu_sets,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB => expected.min_free_ram_mb = d.min_free_ram_mb,
            VF_CONFIG_KEY_MIN_STANDBY_MB => expected.min_standby_mb = d.min_standby_mb,
            VF_CONFIG_KEY_COOLDOWN_SECONDS => expected.cooldown_seconds = d.cooldown_seconds,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S => expected.max_game_disk_read_mb_s = d.max_game_disk_read_mb_s,
            VF_CONFIG_KEY_CUSTOM_GAME_EXES if to.starts_with('{') => expected.custom_game_exes.clear(),
            _ => {}
        }
        assert_eq!(l.settings, expected, "{to}");
        assert_eq!(save(text.as_bytes(), &good.settings), Err(SaveRefused::Blocked(VF_CONFIG_KEY_REJECTED)), "{to}");
    }
}

#[test]
fn version_edges() {
    let status = |v: &str| load(format!("{{\"version\": {v}}}").as_bytes());
    assert_eq!(status("0").status, VF_CONFIG_OLD_VERSION);
    assert_eq!(status("1").status, 0);
    assert_eq!(status("18446744073709551615").status, VF_CONFIG_NEWER_VERSION);
    let over = status("18446744073709551616");
    assert_eq!((over.status, over.rejected_keys), (VF_CONFIG_KEY_REJECTED, VF_CONFIG_KEY_VERSION));
    for bad in ["1e0", "-0", "0.0", "[1]", "{}"] {
        assert_eq!(status(bad).rejected_keys, VF_CONFIG_KEY_VERSION, "{bad}");
        assert!(save(format!("{{\"version\": {bad}}}").as_bytes(), &Settings::default()).is_err(), "{bad}");
    }
    assert_eq!(status("01").status, VF_CONFIG_CORRUPT);
    assert_eq!(
        save(br#"{"version": 18446744073709551615}"#, &Settings::default()),
        Err(SaveRefused::Blocked(VF_CONFIG_NEWER_VERSION))
    );
}

#[test]
fn a_top_level_that_is_not_an_object_is_corrupt_even_when_valid_json() {
    for text in [&b"null"[..], b"true", b"0", b"\"{}\"", b"[{}]", b"\xEF\xBB\xBF[]"] {
        let l = load(text);
        assert_eq!(l.status, VF_CONFIG_CORRUPT, "{:?}", String::from_utf8_lossy(text));
        assert_eq!(l.settings, Settings::default());
        assert_eq!(save(text, &Settings::default()), Err(SaveRefused::Blocked(VF_CONFIG_CORRUPT)));
    }
}

#[test]
fn blank_but_not_rfc_whitespace_is_corrupt_not_empty() {
    for text in [&b"\x0c"[..], b"\x00", "\u{a0}".as_bytes(), b"\xEF\xBB\xBF\xEF\xBB\xBF", b"\xEF\xBB"] {
        assert_eq!(load(text).status, VF_CONFIG_CORRUPT, "{:?}", text);
    }
}

#[test]
fn every_out_of_range_setting_is_refused_with_its_own_bit() {
    let d = Settings::default();
    type Set = fn(&mut Settings);
    let cases: &[(Set, u32)] = &[
        (|s| s.poll_interval_ms = 249, VF_CONFIG_KEY_POLL_INTERVAL_MS),
        (|s| s.poll_interval_ms = u32::MAX, VF_CONFIG_KEY_POLL_INTERVAL_MS),
        (|s| s.timer_resolution_ns100 = 0, VF_CONFIG_KEY_TIMER_RESOLUTION_NS100),
        (|s| s.timer_resolution_ns100 = 156_251, VF_CONFIG_KEY_TIMER_RESOLUTION_NS100),
        (|s| s.min_free_ram_mb = u64::MAX, VF_CONFIG_KEY_MIN_FREE_RAM_MB),
        (|s| s.min_standby_mb = 127, VF_CONFIG_KEY_MIN_STANDBY_MB),
        (|s| s.cooldown_seconds = 3601, VF_CONFIG_KEY_COOLDOWN_SECONDS),
        (|s| s.max_game_disk_read_mb_s = 0, VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S),
        (|s| s.custom_game_exes = vec!["ok.exe".into(), "".into()], VF_CONFIG_KEY_CUSTOM_GAME_EXES),
        (|s| s.custom_game_exes = vec!["LSASS.Exe".into()], VF_CONFIG_KEY_CUSTOM_GAME_EXES),
        (|s| s.custom_game_exes = (0..=MAX_GAME_EXES).map(|i| format!("g{i}.exe")).collect(), VF_CONFIG_KEY_CUSTOM_GAME_EXES),
    ];
    for (i, (set, bit)) in cases.iter().enumerate() {
        let mut s = d.clone();
        set(&mut s);
        for original in [&b""[..], b"{}", br#"{"version": 1}"#] {
            assert_eq!(save(original, &s), Err(SaveRefused::InvalidSetting(*bit)), "case {i}");
        }
    }
    let mut s = d.clone();
    s.custom_game_exes = (0..MAX_GAME_EXES).map(|i| format!("g{i}.exe")).collect();
    let text = save(b"", &s).unwrap();
    assert_eq!(load(text.as_bytes()).settings, s, "exactly MAX_GAME_EXES names save and load back");
}

#[test]
fn saving_twice_gives_the_same_text() {
    let original = br#"{"z": [1.0, {"y": -0}], "poll_interval_ms": 300, "cooldown_seconds": 20}"#;
    let s = load(original).settings;
    let once = save(original, &s).unwrap();
    let twice = save(once.as_bytes(), &s).unwrap();
    assert_eq!(once, twice);
}

#[test]
fn an_old_file_with_bom_and_memory_keys_everywhere_saves_and_reloads() {
    let original = "\u{feff}{\"min_free_ram_mb\": 500, \"memory\": {\"note\": 1e-7, \"min_standby_mb\": 600}, \"cooldown_seconds\": 15, \"x\": {\"cooldown_seconds\": \"keep\"}}";
    let l = load(original.as_bytes());
    assert_eq!(l.status, VF_CONFIG_OLD_VERSION);
    assert_eq!((l.settings.min_free_ram_mb, l.settings.min_standby_mb, l.settings.cooldown_seconds), (500, 600, 15));
    let text = save(original.as_bytes(), &l.settings).unwrap();
    let again = load(text.as_bytes());
    assert_eq!((again.status, &again.settings), (0, &l.settings));
    let v = parse(text.as_bytes()).unwrap();
    assert_eq!(v.get("x"), Some(&Value::Object(vec![("cooldown_seconds".into(), st("keep"))])));
    assert_eq!(v.get("memory").unwrap().get("note"), Some(&num("1e-7")));
}

/// One unknown entry to place in a generated config file.
fn unknown_entries(known: &'static [&'static str]) -> impl Strategy<Value = Vec<(String, Value)>> {
    prop::collection::btree_map(
        any::<String>().prop_filter("not a known key", move |k| !known.contains(&k.as_str())),
        any_value(3),
        0..4,
    )
    .prop_map(|m| m.into_iter().collect())
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 384, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5eed_0002), ..ProptestConfig::default() })]

    /// Unknown keys (top level and inside memory, any value, any position) survive save exactly and in order, and
    /// what save writes loads back to the same settings with status 0. Covers old and current files and a BOM.
    #[test]
    fn save_keeps_every_unknown_key_and_round_trips(
        current in valid_settings(),
        wanted in valid_settings(),
        top_unknown in unknown_entries(&["version", "poll_interval_ms", "timer_resolution_ns100", "timer_forced_manual",
            "restrict_game_cpu_sets", "memory", "custom_game_exes", "min_free_ram_mb", "min_standby_mb",
            "cooldown_seconds", "max_game_disk_read_mb_s"]),
        mem_unknown in unknown_entries(MEMORY_KNOWN),
        old in any::<bool>(),
        with_memory in any::<bool>(),
        bom in any::<bool>(),
        seed in any::<u64>(),
    ) {
        let mut r = Rng(seed);
        // Build the original file: known keys from `current`, unknown keys interleaved at random positions.
        let n = |x: u64| Value::Number(x.to_string());
        let mut memory: Vec<(String, Value)> = vec![
            ("min_free_ram_mb".into(), n(current.min_free_ram_mb)),
            ("min_standby_mb".into(), n(current.min_standby_mb)),
            ("cooldown_seconds".into(), n(current.cooldown_seconds as u64)),
            ("max_game_disk_read_mb_s".into(), n(current.max_game_disk_read_mb_s)),
        ];
        for e in mem_unknown.iter().cloned() {
            let at = r.below(memory.len() as u64 + 1) as usize;
            memory.insert(at, e);
        }
        let mut top: Vec<(String, Value)> = vec![
            ("poll_interval_ms".into(), n(current.poll_interval_ms as u64)),
            ("timer_resolution_ns100".into(), n(current.timer_resolution_ns100 as u64)),
            ("timer_forced_manual".into(), Value::Bool(current.timer_forced_manual)),
            ("restrict_game_cpu_sets".into(), Value::Bool(current.restrict_game_cpu_sets)),
            ("custom_game_exes".into(), Value::Array(current.custom_game_exes.iter().cloned().map(Value::String).collect())),
        ];
        if !old {
            top.insert(0, ("version".into(), n(1)));
        }
        if with_memory || !old {
            top.push(("memory".into(), Value::Object(memory.clone())));
        } else {
            // Old reader: memory keys at the top level.
            for (k, v) in memory.iter().filter(|(k, _)| MEMORY_KNOWN.contains(&k.as_str())) {
                top.push((k.clone(), v.clone()));
            }
        }
        for e in top_unknown.iter().cloned() {
            let at = r.below(top.len() as u64 + 1) as usize;
            top.insert(at, e);
        }
        let top_order: Vec<String> = top.iter().map(|(k, _)| k.clone())
            .filter(|k| top_unknown.iter().any(|(u, _)| u == k)).collect();
        let mem_order: Vec<String> = memory.iter().map(|(k, _)| k.clone())
            .filter(|k| mem_unknown.iter().any(|(u, _)| u == k)).collect();
        let mut text = String::new();
        if bom { text.push('\u{feff}'); }
        write_random(&mut text, &Value::Object(top), &mut r);

        let l = load(text.as_bytes());
        prop_assert_eq!(l.status, if old { VF_CONFIG_OLD_VERSION } else { 0 });
        prop_assert_eq!(&l.settings, &current);

        let saved = save(text.as_bytes(), &wanted).unwrap();
        let again = load(saved.as_bytes());
        prop_assert_eq!(again.status, 0);
        prop_assert_eq!(&again.settings, &wanted);

        let written = parse(saved.as_bytes()).unwrap();
        let keys: Vec<&str> = members(&written).iter().map(|(k, _)| k.as_str()).collect();
        let unknown_keys: Vec<&str> = top_order.iter().map(String::as_str).collect();
        for (k, v) in &top_unknown {
            prop_assert_eq!(written.get(k), Some(v), "top-level unknown key {:?}", k);
        }
        prop_assert!(is_subsequence(&unknown_keys, &keys), "order of top-level unknown keys changed");
        let mem = written.get("memory").unwrap();
        let mem_keys: Vec<&str> = members(mem).iter().map(|(k, _)| k.as_str()).collect();
        let mem_unknown_keys: Vec<&str> = mem_order.iter().map(String::as_str).collect();
        if with_memory || !old {
            for (k, v) in &mem_unknown {
                prop_assert_eq!(mem.get(k), Some(v), "memory unknown key {:?}", k);
            }
            prop_assert!(is_subsequence(&mem_unknown_keys, &mem_keys), "order of memory unknown keys changed");
        }
        // Saving the saved file again changes nothing.
        prop_assert_eq!(save(saved.as_bytes(), &wanted).unwrap(), saved);
    }

    /// For any bytes (mutated valid files): save refuses exactly when load says the file blocks saving, and
    /// otherwise its text loads back to the settings with status 0.
    #[test]
    fn save_refuses_exactly_when_load_blocks(
        base in valid_settings(),
        wanted in valid_settings(),
        edits in prop::collection::vec((any::<usize>(), any::<u8>(), 0u8..3), 0..4),
    ) {
        let mut bytes = save(b"", &base).unwrap().into_bytes();
        for (pos, b, kind) in edits {
            if bytes.is_empty() { bytes.push(b); continue; }
            let p = pos % bytes.len();
            match kind {
                0 => bytes[p] = b,
                1 => bytes.insert(p, b),
                _ => { bytes.remove(p); }
            }
        }
        let l = load(&bytes);
        match save(&bytes, &wanted) {
            Err(SaveRefused::Blocked(s)) => {
                prop_assert_eq!(s, l.status);
                prop_assert!(l.status & VF_CONFIG_BLOCKS_SAVE != 0);
            }
            Err(e) => prop_assert!(false, "unexpected {:?}", e),
            Ok(text) => {
                prop_assert_eq!(l.status & VF_CONFIG_BLOCKS_SAVE, 0);
                let again = load(text.as_bytes());
                prop_assert_eq!(again.status, 0);
                prop_assert_eq!(again.settings, wanted);
            }
        }
        // A blocked status always comes with factory values for the rejected keys only; corrupt means all factory.
        if l.status & VF_CONFIG_CORRUPT != 0 {
            prop_assert_eq!(l.settings, Settings::default());
            prop_assert_eq!(l.rejected_keys, 0);
        }
        prop_assert_eq!(l.status & VF_CONFIG_KEY_REJECTED != 0, l.rejected_keys != 0);
    }

    /// A Windows process name in any ASCII case is refused, alone and inside a list.
    #[test]
    fn windows_processes_refused_in_any_case(mask in any::<u64>(), which in 0usize..6) {
        let base = ["explorer.exe", "csrss.exe", "svchost.exe", "lsass.exe", "dwm.exe", "vegaflow_daemon.exe"][which];
        let name: String = base.chars().enumerate()
            .map(|(i, c)| if mask >> (i % 64) & 1 == 1 { c.to_ascii_uppercase() } else { c })
            .collect();
        prop_assert!(!is_allowed_game_exe(&name), "{}", name);
        let l = load(format!("{{\"version\":1,\"custom_game_exes\":[\"ok.exe\",\"{name}\"]}}").as_bytes());
        prop_assert_eq!(l.settings.custom_game_exes, vec!["ok.exe".to_string()]);
        prop_assert_eq!(l.rejected_keys, VF_CONFIG_KEY_CUSTOM_GAME_EXES);
    }

    /// Any name load accepts is written back by save exactly, and any name save accepts is read back by load.
    #[test]
    fn load_and_save_agree_on_game_names(name in "\\PC{0,20}(\\.[eE][xX][eE])?") {
        let allowed = is_allowed_game_exe(&name);
        let file = write_pretty(&Value::Object(vec![
            ("version".into(), num("1")),
            ("custom_game_exes".into(), Value::Array(vec![Value::String(name.clone())])),
        ]));
        let l = load(file.as_bytes());
        prop_assert_eq!(l.rejected_keys == 0, allowed);
        let s = Settings { custom_game_exes: vec![name.clone()], ..Settings::default() };
        match save(b"", &s) {
            Ok(text) => {
                prop_assert!(allowed);
                prop_assert_eq!(load(text.as_bytes()).settings.custom_game_exes, vec![name]);
            }
            Err(e) => {
                prop_assert!(!allowed);
                prop_assert_eq!(e, SaveRefused::InvalidSetting(VF_CONFIG_KEY_CUSTOM_GAME_EXES));
            }
        }
    }
}

// --- 3. folder_trust ---

/// Reference model written from the contract, independent of the implementation.
fn trusted_reference(has_dacl: bool, owner: SidKind, aces: &[Ace]) -> bool {
    const RIGHTS: [u32; 11] =
        [0x2, 0x4, 0x10, 0x40, 0x100, 0x1_0000, 0x4_0000, 0x8_0000, 0x200_0000, 0x1000_0000, 0x4000_0000];
    let admin = |k: SidKind| matches!(k, SidKind::System | SidKind::Administrators | SidKind::TrustedInstaller);
    if !has_dacl || !admin(owner) {
        return false;
    }
    !aces.iter().any(|a| a.allows && !a.inherit_only && !admin(a.sid) && RIGHTS.iter().any(|r| a.mask & r != 0))
}

fn any_sid() -> impl Strategy<Value = SidKind> {
    prop_oneof![Just(SidKind::Other), Just(SidKind::System), Just(SidKind::Administrators), Just(SidKind::TrustedInstaller)]
}

fn any_ace() -> impl Strategy<Value = Ace> {
    (prop_oneof![any::<u32>(), (0u32..32).prop_map(|b| 1 << b), Just(0x0012_00A9u32)], any_sid(), any::<bool>(), any::<bool>())
        .prop_map(|(mask, sid, allows, inherit_only)| Ace { mask, sid, allows, inherit_only })
}

#[test]
fn write_rights_constant_is_exactly_the_contract_set() {
    assert_eq!(WRITE_RIGHTS, 0x2 | 0x4 | 0x10 | 0x40 | 0x100 | 0x1_0000 | 0x4_0000 | 0x8_0000 | 0x200_0000 | 0x1000_0000 | 0x4000_0000);
}

#[test]
fn read_only_rights_for_other_accounts_keep_trust() {
    // READ_CONTROL, SYNCHRONIZE, FILE_READ_DATA/EA/ATTRIBUTES, FILE_EXECUTE, GENERIC_READ/EXECUTE.
    for mask in [0u32, 0x2_0000, 0x10_0000, 0x1, 0x8, 0x80, 0x20, 0x8000_0000, 0x2000_0000, 0x0012_00A9] {
        let aces = [Ace { mask, sid: SidKind::Other, allows: true, inherit_only: false }];
        assert!(only_admins_can_write(true, SidKind::System, &aces), "{mask:#x}");
    }
}

#[test]
fn a_deny_entry_does_not_cancel_a_later_allow_for_another_account() {
    let aces = [
        Ace { mask: 0x001F_01FF, sid: SidKind::Other, allows: false, inherit_only: false },
        Ace { mask: 0x2, sid: SidKind::Other, allows: true, inherit_only: false },
    ];
    assert!(!only_admins_can_write(true, SidKind::Administrators, &aces));
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 2048, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5eed_0003), ..ProptestConfig::default() })]

    #[test]
    fn folder_trust_matches_the_reference_model(has_dacl in any::<bool>(), owner in any_sid(), aces in prop::collection::vec(any_ace(), 0..6)) {
        prop_assert_eq!(only_admins_can_write(has_dacl, owner, &aces), trusted_reference(has_dacl, owner, &aces));
    }

    /// Adding any entry never turns an untrusted folder into a trusted one (monotonic, fail closed).
    #[test]
    fn adding_an_entry_never_adds_trust(has_dacl in any::<bool>(), owner in any_sid(), aces in prop::collection::vec(any_ace(), 0..5), extra in any_ace(), at in any::<usize>()) {
        let before = only_admins_can_write(has_dacl, owner, &aces);
        let mut more = aces.clone();
        let i = at % (more.len() + 1);
        more.insert(i, extra);
        prop_assert!(!only_admins_can_write(has_dacl, owner, &more) || before);
    }
}

// --- 4. FFI ---

unsafe extern "C" fn collect(ctx: *mut c_void, utf8: *const u8, len: usize) {
    let out = &mut *(ctx as *mut Vec<Vec<u8>>);
    out.push(std::slice::from_raw_parts(utf8, len).to_vec());
}

fn ffi_defaults() -> VfConfigSettings {
    let mut out = VfConfigLoad::default();
    assert!(vf_rust_config_load(std::ptr::null(), 0, &mut out, None, std::ptr::null_mut()));
    out.settings
}

fn ffi_save(original: &[u8], s: &VfConfigSettings, names: &[u8]) -> (u32, Vec<Vec<u8>>) {
    let mut texts: Vec<Vec<u8>> = Vec::new();
    let r = vf_rust_config_save(
        original.as_ptr(),
        original.len(),
        s,
        names.as_ptr(),
        names.len(),
        Some(collect),
        &mut texts as *mut _ as *mut c_void,
    );
    (r, texts)
}

#[test]
fn ffi_malformed_name_buffers_fail_closed() {
    let s = ffi_defaults();
    for names in [
        &b"\0"[..],
        b"\0\0",
        b"a.exe\0\0",
        b"\0a.exe\0",
        b"a.exe\0b.exe",
        b"a.exe\0\xC0\x80.exe\0",
        b"a.exe\0lsass.exe\0",
        b"a.exe\0dir\\b.exe\0",
    ] {
        assert_eq!(ffi_save(b"{}", &s, names), (VF_CONFIG_SAVE_INVALID, vec![]), "{:?}", String::from_utf8_lossy(names));
    }
    let too_many: Vec<u8> = (0..=MAX_GAME_EXES).flat_map(|i| format!("g{i}.exe\0").into_bytes()).collect();
    assert_eq!(ffi_save(b"{}", &s, &too_many), (VF_CONFIG_SAVE_INVALID, vec![]));
    let exactly: Vec<u8> = (0..MAX_GAME_EXES).flat_map(|i| format!("g{i}.exe\0").into_bytes()).collect();
    assert_eq!(ffi_save(b"{}", &s, &exactly).0, VF_CONFIG_SAVE_OK);
}

#[test]
fn ffi_zero_length_with_a_dangling_pointer_is_not_read() {
    let dangling = std::ptr::NonNull::<u8>::dangling().as_ptr();
    let mut out = VfConfigLoad::default();
    assert!(vf_rust_config_load(dangling, 0, &mut out, None, std::ptr::null_mut()));
    assert_eq!(out.status, VF_CONFIG_EMPTY);
    let s = ffi_defaults();
    let mut texts: Vec<Vec<u8>> = Vec::new();
    let r = vf_rust_config_save(dangling, 0, &s, dangling, 0, Some(collect), &mut texts as *mut _ as *mut c_void);
    assert_eq!(r, VF_CONFIG_SAVE_OK);
    assert_eq!(texts.len(), 1);
    let dangling_ace = std::ptr::NonNull::<VfAceEntry>::dangling().as_ptr();
    assert!(vf_rust_only_admins_can_write(true, VF_SID_SYSTEM, dangling_ace, 0));
}

#[test]
fn ffi_null_pointers_fail_closed() {
    let s = ffi_defaults();
    let mut texts: Vec<Vec<u8>> = Vec::new();
    let ctx = &mut texts as *mut _ as *mut c_void;
    assert_eq!(vf_rust_config_save(b"{}".as_ptr(), 2, std::ptr::null(), std::ptr::null(), 0, Some(collect), ctx), VF_CONFIG_SAVE_INVALID);
    assert_eq!(vf_rust_config_save(std::ptr::null(), 1, &s, std::ptr::null(), 0, Some(collect), ctx), VF_CONFIG_SAVE_INVALID);
    assert_eq!(vf_rust_config_save(b"{}".as_ptr(), 2, &s, std::ptr::null(), 1, Some(collect), ctx), VF_CONFIG_SAVE_INVALID);
    assert!(texts.is_empty());
    // No callback: still OK, nothing to call.
    assert_eq!(vf_rust_config_save(std::ptr::null(), 0, &s, std::ptr::null(), 0, None, std::ptr::null_mut()), VF_CONFIG_SAVE_OK);
    // Blocked and invalid never call back, whatever the callback.
    assert_eq!(ffi_save(br#"{"version": 9}"#, &s, b""), (VF_CONFIG_SAVE_BLOCKED, vec![]));
    let mut bad = s;
    bad.max_game_disk_read_mb_s = u64::MAX;
    assert_eq!(ffi_save(b"", &bad, b""), (VF_CONFIG_SAVE_INVALID, vec![]));
}

#[test]
fn ffi_load_hands_back_exactly_the_accepted_names_as_utf8() {
    let text = "{\"version\":1,\"custom_game_exes\":[\"juego ñ.exe\",\"explorer.exe\",\"😀.EXE\",\"a\\u0000b.exe\"]}";
    let mut out = VfConfigLoad::default();
    let mut names: Vec<Vec<u8>> = Vec::new();
    assert!(vf_rust_config_load(text.as_ptr(), text.len(), &mut out, Some(collect), &mut names as *mut _ as *mut c_void));
    assert_eq!(names, vec!["juego ñ.exe".as_bytes().to_vec(), "😀.EXE".as_bytes().to_vec()]);
    assert_eq!(out.rejected_keys, VF_CONFIG_KEY_CUSTOM_GAME_EXES);
    assert_eq!(out.status, VF_CONFIG_KEY_REJECTED);
}

#[test]
fn ffi_every_sid_kind_number() {
    let full = 0x001F_01FFu32;
    for k in 0u8..=255 {
        let is_admin = matches!(k, VF_SID_SYSTEM | VF_SID_ADMINISTRATORS | VF_SID_TRUSTED_INSTALLER);
        // As owner.
        assert_eq!(vf_rust_only_admins_can_write(true, k, std::ptr::null(), 0), is_admin, "owner {k}");
        // As an entry with a write right.
        let e = VfAceEntry { mask: full, sid_kind: k, allows: true, inherit_only: false };
        assert_eq!(vf_rust_only_admins_can_write(true, VF_SID_SYSTEM, &e, 1), is_admin, "entry {k}");
        // Never trusted without a DACL.
        assert!(!vf_rust_only_admins_can_write(false, k, &e, 1));
    }
    assert_eq!(VF_SID_OTHER, 0);
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 512, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5eed_0004), ..ProptestConfig::default() })]

    #[test]
    fn ffi_folder_trust_matches_the_reference_model(
        has_dacl in any::<bool>(), owner in any::<u8>(),
        raw in prop::collection::vec((any::<u32>(), 0u8..6, any::<bool>(), any::<bool>()), 0..6),
    ) {
        let kind = |k: u8| match k { 1 => SidKind::System, 2 => SidKind::Administrators, 3 => SidKind::TrustedInstaller, _ => SidKind::Other };
        let entries: Vec<VfAceEntry> = raw.iter().map(|&(mask, sid_kind, allows, inherit_only)| VfAceEntry { mask, sid_kind, allows, inherit_only }).collect();
        let aces: Vec<Ace> = raw.iter().map(|&(mask, k, allows, inherit_only)| Ace { mask, sid: kind(k), allows, inherit_only }).collect();
        prop_assert_eq!(
            vf_rust_only_admins_can_write(has_dacl, owner, entries.as_ptr(), entries.len()),
            trusted_reference(has_dacl, kind(owner), &aces)
        );
    }

    /// Any bytes through the C load never panic and the callback only sees names that pass the rule.
    #[test]
    fn ffi_load_any_bytes(bytes in prop::collection::vec(any::<u8>(), 0..256)) {
        let mut out = VfConfigLoad::default();
        let mut names: Vec<Vec<u8>> = Vec::new();
        prop_assert!(vf_rust_config_load(bytes.as_ptr(), bytes.len(), &mut out, Some(collect), &mut names as *mut _ as *mut c_void));
        for n in names {
            let s = String::from_utf8(n).unwrap();
            prop_assert!(is_allowed_game_exe(&s));
        }
    }

    /// Any name buffer through the C save never panics, and OK only when every NUL-terminated name passes the rule.
    #[test]
    fn ffi_save_any_name_buffer(names in prop::collection::vec(prop_oneof![Just(0u8), Just(b'.'), Just(b'a'), Just(b'e'), Just(b'x'), any::<u8>()], 0..40)) {
        let s = ffi_defaults();
        let (r, texts) = ffi_save(b"", &s, &names);
        let expected_ok = names.is_empty() || (names.last() == Some(&0) && names[..names.len() - 1]
            .split(|&b| b == 0)
            .all(|n| std::str::from_utf8(n).map(is_allowed_game_exe).unwrap_or(false)));
        prop_assert_eq!(r == VF_CONFIG_SAVE_OK, expected_ok, "{:?}", names);
        prop_assert_eq!(texts.len(), usize::from(r == VF_CONFIG_SAVE_OK));
    }
}
