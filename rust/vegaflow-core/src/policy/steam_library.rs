//! Reads Steam's `libraryfolders.vdf` and returns the library folders it lists, in file order.
//! Only absolute drive paths (`x:\...`) in valid UTF-8 are returned; anything malformed is skipped, never guessed.

/// More libraries than this in one file is not a real Steam install; the rest are ignored.
pub const MAX_LIBRARIES: usize = 64;
const MAX_DEPTH: usize = 16;
const MAX_PATH_BYTES: usize = 4096;

#[derive(Debug, PartialEq)]
enum Token {
    Text(Vec<u8>),
    Open,
    Close,
}

/// Splits the file into quoted or bare strings and braces. Stops at the first unterminated string.
fn tokenize(src: &[u8]) -> Vec<Token> {
    let mut tokens = Vec::new();
    let mut i = 0;
    while i < src.len() {
        let c = src[i];
        if c.is_ascii_whitespace() {
            i += 1;
        } else if c == b'/' && src.get(i + 1) == Some(&b'/') {
            while i < src.len() && src[i] != b'\n' {
                i += 1;
            }
        } else if c == b'{' {
            tokens.push(Token::Open);
            i += 1;
        } else if c == b'}' {
            tokens.push(Token::Close);
            i += 1;
        } else if c == b'"' {
            let mut text = Vec::new();
            i += 1;
            loop {
                match src.get(i) {
                    None => return tokens,
                    Some(b'"') => break,
                    Some(b'\\') => {
                        match src.get(i + 1) {
                            Some(b'\\') => text.push(b'\\'),
                            Some(b'"') => text.push(b'"'),
                            Some(b'n') => text.push(b'\n'),
                            Some(b't') => text.push(b'\t'),
                            Some(&other) => text.extend_from_slice(&[b'\\', other]),
                            None => return tokens,
                        }
                        i += 2;
                    }
                    Some(&b) => {
                        text.push(b);
                        i += 1;
                    }
                }
            }
            tokens.push(Token::Text(text));
            i += 1;
        } else {
            let start = i;
            while i < src.len() && !src[i].is_ascii_whitespace() && !matches!(src[i], b'{' | b'}' | b'"') {
                i += 1;
            }
            tokens.push(Token::Text(src[start..i].to_vec()));
        }
    }
    tokens
}

enum Value {
    Text(Vec<u8>),
    Object(Vec<(Vec<u8>, Value)>),
}

/// Parses `key value` pairs until a closing brace or the end; `None` if the structure is broken or too deep.
fn parse_pairs(tokens: &[Token], pos: &mut usize, depth: usize) -> Option<Vec<(Vec<u8>, Value)>> {
    if depth > MAX_DEPTH {
        return None;
    }
    let mut pairs = Vec::new();
    while let Some(token) = tokens.get(*pos) {
        let key = match token {
            Token::Close => return Some(pairs),
            Token::Open => return None,
            Token::Text(k) => k.clone(),
        };
        *pos += 1;
        let value = match tokens.get(*pos)? {
            Token::Text(v) => {
                *pos += 1;
                Value::Text(v.clone())
            }
            Token::Open => {
                *pos += 1;
                let inner = parse_pairs(tokens, pos, depth + 1)?;
                if tokens.get(*pos) != Some(&Token::Close) {
                    return None;
                }
                *pos += 1;
                Value::Object(inner)
            }
            Token::Close => return None,
        };
        pairs.push((key, value));
    }
    Some(pairs)
}

fn is_drive_path(path: &[u8]) -> bool {
    path.len() >= 3
        && path.len() <= MAX_PATH_BYTES
        && path[0].is_ascii_alphabetic()
        && path[1] == b':'
        && matches!(path[2], b'\\' | b'/')
        && !path.contains(&0)
        && std::str::from_utf8(path).is_ok()
}

/// The library folders listed in `vdf`: `"N" { "path" "..." }` (current format) or `"N" "..."` (before 2021).
pub fn library_paths(vdf: &[u8]) -> Vec<String> {
    let tokens = tokenize(vdf);
    let mut pos = 0;
    let Some(top) = parse_pairs(&tokens, &mut pos, 0) else { return Vec::new() };
    let mut paths = Vec::new();
    for (key, value) in &top {
        let Value::Object(entries) = value else { continue };
        if !key.eq_ignore_ascii_case(b"libraryfolders") {
            continue;
        }
        for (entry_key, entry) in entries {
            if entry_key.is_empty() || !entry_key.iter().all(u8::is_ascii_digit) {
                continue;
            }
            let path = match entry {
                Value::Text(p) => Some(p),
                Value::Object(fields) => fields.iter().find_map(|(k, v)| match v {
                    Value::Text(p) if k.eq_ignore_ascii_case(b"path") => Some(p),
                    _ => None,
                }),
            };
            if let Some(p) = path.filter(|p| is_drive_path(p)) {
                if paths.len() == MAX_LIBRARIES {
                    return paths;
                }
                paths.push(String::from_utf8(p.clone()).unwrap_or_default());
            }
        }
    }
    paths
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;

    const CURRENT_FORMAT: &str = r#"
"libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"label"		""
		"contentid"		"123"
		"apps"
		{
			"228980"		"1234"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
		"apps" { }
	}
}
"#;

    #[test]
    fn reads_the_current_format() {
        assert_eq!(library_paths(CURRENT_FORMAT.as_bytes()), ["C:\\Program Files (x86)\\Steam", "D:\\SteamLibrary"]);
    }

    #[test]
    fn reads_the_format_before_2021() {
        let vdf = "\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\"1\"\n\t\"ContentStatsID\"\t\"-2\"\n\t\"1\"\t\"E:\\\\Games\\\\Steam\"\n}\n";
        assert_eq!(library_paths(vdf.as_bytes()), ["E:\\Games\\Steam"]);
    }

    #[test]
    fn skips_relative_unc_and_device_paths() {
        let vdf = r#""libraryfolders" { "0" { "path" "Steam" } "1" { "path" "\\\\server\\share" } "2" { "path" "\\\\?\\C:\\x" } "3" { "path" "F:\\ok" } }"#;
        assert_eq!(library_paths(vdf.as_bytes()), ["F:\\ok"]);
    }

    #[test]
    fn skips_paths_without_a_drive_colon_or_with_a_nul() {
        let vdf = b"\"libraryfolders\" { \"0\" { \"path\" \"CX\\\\a\" } \"1\" { \"path\" \"C:\\\\a\0b\" } \"2\" { \"path\" \"C:\\\\ok\" } }";
        assert_eq!(library_paths(vdf), ["C:\\ok"]);
    }

    #[test]
    fn ignores_paths_outside_the_libraryfolders_block_and_non_numeric_keys() {
        let vdf = r#""other" { "0" { "path" "C:\\a" } } "libraryfolders" { "label" { "path" "C:\\b" } "path" "C:\\c" "2" { "path" "C:\\d" } }"#;
        assert_eq!(library_paths(vdf.as_bytes()), ["C:\\d"]);
    }

    #[test]
    fn broken_files_give_nothing() {
        for vdf in ["", "\"libraryfolders\" {", "\"libraryfolders\" { \"0\" { \"path\" \"C:\\\\a", "}}}}", "{ \"0\" }"] {
            assert!(library_paths(vdf.as_bytes()).is_empty(), "{vdf:?}");
        }
    }

    #[test]
    fn comments_and_bare_strings_are_tokens_like_steam_reads_them() {
        let vdf = "// written by Steam\nlibraryfolders { 0 { path \"G:\\\\Lib\" } }";
        assert_eq!(library_paths(vdf.as_bytes()), ["G:\\Lib"]);
    }

    #[test]
    fn stops_at_max_libraries() {
        let mut vdf = String::from("\"libraryfolders\" {");
        for n in 0..(MAX_LIBRARIES + 5) {
            vdf.push_str(&format!(" \"{n}\" {{ \"path\" \"C:\\\\L{n}\" }}"));
        }
        vdf.push('}');
        assert_eq!(library_paths(vdf.as_bytes()).len(), MAX_LIBRARIES);
    }

    #[test]
    fn too_deep_nesting_rejects_the_whole_file() {
        let with_nesting = |n: usize| {
            let deep = format!("{}{}", "\"k\" {".repeat(n), "}".repeat(n));
            format!("\"libraryfolders\" {{ \"0\" {{ \"path\" \"C:\\\\a\" }} {deep} }}")
        };
        assert_eq!(library_paths(with_nesting(MAX_DEPTH - 1).as_bytes()), ["C:\\a"]);
        assert!(library_paths(with_nesting(MAX_DEPTH).as_bytes()).is_empty());
    }

    #[test]
    fn exported_function_calls_back_once_per_library() {
        unsafe extern "C" fn collect(ctx: *mut core::ffi::c_void, p: *const u8, len: usize) {
            let out = &mut *(ctx as *mut Vec<String>);
            out.push(String::from_utf8(core::slice::from_raw_parts(p, len).to_vec()).unwrap());
        }
        let mut found: Vec<String> = Vec::new();
        let n = crate::ffi::vf_rust_steam_library_paths(
            CURRENT_FORMAT.as_ptr(), CURRENT_FORMAT.len(), Some(collect), &mut found as *mut _ as *mut core::ffi::c_void);
        assert_eq!(n, 2);
        assert_eq!(found, ["C:\\Program Files (x86)\\Steam", "D:\\SteamLibrary"]);
        assert_eq!(crate::ffi::vf_rust_steam_library_paths(core::ptr::null(), 5, Some(collect), core::ptr::null_mut()), 0);
        assert_eq!(crate::ffi::vf_rust_steam_library_paths(CURRENT_FORMAT.as_ptr(), CURRENT_FORMAT.len(), None, core::ptr::null_mut()), 2);
    }

    fn escape(path: &str) -> String {
        path.replace('\\', "\\\\").replace('"', "\\\"")
    }

    proptest! {
        #[test]
        fn never_panics_and_only_returns_drive_paths(bytes in proptest::collection::vec(any::<u8>(), 0..512)) {
            for p in library_paths(&bytes) {
                prop_assert!(is_drive_path(p.as_bytes()), "{p:?}");
            }
        }

        #[test]
        fn never_panics_on_vdf_shaped_noise(parts in proptest::collection::vec(
            prop_oneof![Just("{"), Just("}"), Just("\""), Just("\\"), Just("libraryfolders"), Just("path"), Just("0"),
                        Just("C:\\x"), Just(" "), Just("//"), Just("\n")], 0..64)) {
            let vdf: String = parts.concat();
            for p in library_paths(vdf.as_bytes()) {
                prop_assert!(is_drive_path(p.as_bytes()));
            }
        }

        #[test]
        fn every_written_library_is_read_back(libs in proptest::collection::vec("[A-Za-z]:\\\\[^\\x00]{0,40}", 0..8)) {
            let mut vdf = String::from("\"libraryfolders\"\n{\n");
            for (n, lib) in libs.iter().enumerate() {
                vdf.push_str(&format!("\t\"{n}\"\n\t{{\n\t\t\"path\"\t\t\"{}\"\n\t}}\n", escape(lib)));
            }
            vdf.push_str("}\n");
            prop_assert_eq!(library_paths(vdf.as_bytes()), libs);
        }
    }
}
