//! Adversarial tests for `steam_library::library_paths`: hostile `libraryfolders.vdf` contents.

use proptest::prelude::*;
use vegaflow_core::ffi::vf_rust_steam_library_paths;
use vegaflow_core::policy::steam_library::{library_paths, MAX_LIBRARIES};

fn lib(path_escaped: &str) -> String {
    format!("\"libraryfolders\" {{ \"0\" {{ \"path\" \"{path_escaped}\" }} }}")
}

/// Runs `f` on a thread with a small stack, so deep recursion shows up as a crash instead of passing by luck.
fn on_small_stack<T: Send + 'static>(f: impl FnOnce() -> T + Send + 'static) -> T {
    std::thread::Builder::new()
        .stack_size(128 * 1024)
        .spawn(f)
        .unwrap()
        .join()
        .expect("thread crashed")
}

fn is_drive_path(p: &str) -> bool {
    let b = p.as_bytes();
    b.len() >= 3
        && b.len() <= 4096
        && b[0].is_ascii_alphabetic()
        && b[1] == b':'
        && matches!(b[2], b'\\' | b'/')
        && !b.contains(&0)
}

#[test]
fn path_length_boundary_is_4096_bytes() {
    let ok = format!("C:\\\\{}", "a".repeat(4093));
    let too_long = format!("C:\\\\{}", "a".repeat(4094));
    assert_eq!(library_paths(lib(&ok).as_bytes())[0].len(), 4096);
    assert!(library_paths(lib(&too_long).as_bytes()).is_empty());
}

#[test]
fn cap_holds_across_several_libraryfolders_blocks() {
    let block = |base: usize| {
        let mut s = String::from("\"libraryfolders\" {");
        for n in 0..40 {
            s.push_str(&format!(" \"{n}\" \"C:\\\\L{}\"", base + n));
        }
        s.push('}');
        s
    };
    let vdf = format!("{} {} {}", block(0), block(100), block(200));
    let out = library_paths(vdf.as_bytes());
    assert_eq!(out.len(), MAX_LIBRARIES);
    assert_eq!(out[40], "C:\\L100");
}

#[test]
fn invalid_entries_do_not_count_toward_the_cap() {
    let mut vdf = String::from("\"libraryfolders\" {");
    for n in 0..200 {
        vdf.push_str(&format!(" \"{n}\" \"relative{n}\""));
    }
    vdf.push_str(" \"999\" \"E:\\\\ok\" }");
    assert_eq!(library_paths(vdf.as_bytes()), ["E:\\ok"]);
}

#[test]
fn non_letter_and_non_ascii_drives_are_refused() {
    for p in [
        "1:\\\\a",
        "::\\\\a",
        "\\\\\\\\?\\\\C:\\\\a",
        "\\\\??\\\\C:\\\\a",
        "C:a",
        "C:",
        "é:\\\\a",
        "\u{FF23}:\\\\a",
    ] {
        let vdf = format!("\"libraryfolders\" {{ \"0\" \"{p}\" \"9\" \"D:\\\\ok\" }}");
        assert_eq!(library_paths(vdf.as_bytes()), ["D:\\ok"], "{p:?}");
    }
}

#[test]
fn invalid_utf8_and_nul_are_refused() {
    let mut vdf = b"\"libraryfolders\" { \"0\" \"C:\\\\a\xC0\xAFb\" \"1\" \"C:\\\\a\xED\xA0\x80\" \"2\" \"C:\\\\a\x00\" \"9\" \"D:\\\\ok\" }".to_vec();
    assert_eq!(library_paths(&vdf), ["D:\\ok"]);
    vdf.clear();
    vdf.extend_from_slice(b"libraryfolders { 0 C:\\a\x00b 9 D:\\ok }");
    assert_eq!(library_paths(&vdf), ["D:\\ok"]);
}

#[test]
fn deep_braces_and_deep_keys_do_not_overflow_a_small_stack() {
    let n = 1_000_000;
    on_small_stack(move || {
        assert!(library_paths("{".repeat(n).as_bytes()).is_empty());
        assert!(library_paths("\"k\" {".repeat(n).as_bytes()).is_empty());
        let inside = format!("\"libraryfolders\" {{ {} }}", "\"k\" {".repeat(n));
        assert!(library_paths(inside.as_bytes()).is_empty());
    });
}

#[test]
fn huge_unterminated_string_and_escape_give_nothing() {
    let s = format!(
        "\"libraryfolders\" {{ \"0\" \"C:\\\\{}",
        "a".repeat(8 << 20)
    );
    assert!(library_paths(s.as_bytes()).is_empty());
    assert!(library_paths(b"\"libraryfolders\" { \"0\" \"C:\\").is_empty());
    assert!(library_paths(b"\"libraryfolders\" { \"0\" \"C:\\\\a\\\"").is_empty());
}

#[test]
fn trailing_open_brace_after_the_block_rejects_the_file() {
    assert!(library_paths(format!("{} {{", lib("C:\\\\a")).as_bytes()).is_empty());
}

#[test]
fn ffi_count_equals_number_of_callbacks() {
    unsafe extern "C" fn count(ctx: *mut core::ffi::c_void, _p: *const u8, _len: usize) {
        *(ctx as *mut usize) += 1;
    }
    let mut vdf = String::from("\"libraryfolders\" {");
    for n in 0..100 {
        vdf.push_str(&format!(" \"{n}\" {{ \"path\" \"D:\\\\L{n}\" }}"));
    }
    vdf.push('}');
    let mut calls = 0usize;
    let n = vf_rust_steam_library_paths(
        vdf.as_ptr(),
        vdf.len(),
        Some(count),
        &mut calls as *mut usize as *mut core::ffi::c_void,
    );
    assert_eq!((n, calls), (MAX_LIBRARIES, MAX_LIBRARIES));
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 1000, failure_persistence: None, rng_seed: proptest::test_runner::RngSeed::Fixed(0x5EED), ..ProptestConfig::default() })]

    /// Noise built from the tokens the parser reacts to, including escapes and quotes in odd places.
    #[test]
    fn noise_never_panics_and_output_is_bounded(parts in proptest::collection::vec(prop_oneof![
        Just("{"), Just("}"), Just("\""), Just("\\"), Just("\\\""), Just("\\\\"), Just("libraryfolders"), Just("LibraryFolders"),
        Just("path"), Just("0"), Just("1"), Just("C:\\\\x"), Just("c:/y"), Just(" "), Just("//"), Just("\n"), Just("\0"),
        Just("\u{FEFF}"), Just("\u{FF}")], 0..200)) {
        let vdf: String = parts.concat();
        let out = library_paths(vdf.as_bytes());
        prop_assert!(out.len() <= MAX_LIBRARIES);
        for p in out {
            prop_assert!(is_drive_path(&p), "{p:?}");
        }
    }
}
