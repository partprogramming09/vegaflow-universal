//! Install folders reported by a launcher (Epic manifest, GOG registry): the same strict reading for both.
//! A launcher file is data a non-admin program may have written, so a folder is accepted only if it is a plain drive
//! path to a game-sized folder, never `.`/`..`, a device or UNC path, or a broad system folder.

/// Longest folder accepted, in bytes; a longer one is not a real install.
pub const MAX_INSTALL_DIR_BYTES: usize = 1024;

/// Folders that are never a game folder, as the only folder after the drive.
const BROAD_TOP_FOLDERS: &[&str] = &[
    "windows", "program files", "program files (x86)", "programdata", "users", "documents and settings",
    "system volume information", "$recycle.bin", "recovery", "boot",
];

fn bad_component_char(c: char) -> bool {
    matches!(c, ':' | '*' | '?' | '"' | '<' | '>' | '|') || c.is_control()
}

/// `C:/Games/X/` or `C:\Games\X` -> `C:\Games\X`; `None` for anything that is not a plain local drive folder.
pub fn normalize_install_dir(raw: &str) -> Option<String> {
    let path = raw.trim();
    if path.is_empty() || path.len() > MAX_INSTALL_DIR_BYTES {
        return None;
    }
    let b = path.as_bytes();
    if b.len() < 4 || !b[0].is_ascii_alphabetic() || b[1] != b':' || !matches!(b[2], b'\\' | b'/') {
        return None;
    }
    let rest = path[3..].replace('/', "\\");
    let rest = rest.trim_end_matches('\\');
    let parts: Vec<&str> = rest.split('\\').collect();
    for part in &parts {
        if part.is_empty() || *part == "." || *part == ".." || part.ends_with('.') || part.ends_with(' ') || part.chars().any(bad_component_char) {
            return None;
        }
    }
    let first = parts[0].to_ascii_lowercase();
    let broad = match first.as_str() {
        "windows" | "programdata" => true,
        "users" | "documents and settings" => parts.len() <= 2, // `Users` or a profile folder itself
        other => parts.len() == 1 && BROAD_TOP_FOLDERS.contains(&other),
    };
    if broad {
        return None;
    }
    Some(format!("{}:\\{}", &path[..1], parts.join("\\")))
}

/// `file` (a path or a bare name) stays inside `dir`: relative without `..`, or absolute under `dir`, folder by folder.
pub fn stays_inside(dir: &str, file: &str) -> bool {
    let file = file.replace('/', "\\");
    if file.is_empty() || file.contains('\0') {
        return false;
    }
    let is_abs = file.as_bytes().get(1) == Some(&b':') || file.starts_with('\\');
    let rel: Vec<&str> = if is_abs {
        let dir_parts: Vec<&str> = dir.split('\\').collect();
        let file_parts: Vec<&str> = file.split('\\').collect();
        if file_parts.len() <= dir_parts.len() || !dir_parts.iter().zip(&file_parts).all(|(a, b)| a.eq_ignore_ascii_case(b)) {
            return false;
        }
        file_parts[dir_parts.len()..].to_vec()
    } else {
        file.split('\\').collect()
    };
    rel.iter().all(|p| !p.is_empty() && *p != "." && *p != ".." && !p.chars().any(bad_component_char))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn forward_slashes_and_trailing_separators_become_one_form() {
        assert_eq!(normalize_install_dir("C:/Program Files/Epic Games/Sample/").as_deref(), Some("C:\\Program Files\\Epic Games\\Sample"));
        assert_eq!(normalize_install_dir("d:\\Games\\X").as_deref(), Some("d:\\Games\\X"));
        assert_eq!(normalize_install_dir("D:/Fortnite").as_deref(), Some("D:\\Fortnite"));
    }

    #[test]
    fn dot_dot_and_dot_are_refused() {
        for p in ["C:/Games/../Windows", "C:\\Games\\..\\..", "C:/Games/./X", "C:/Games/X/..", "C:/Games/X./Y", "C:/Games/X /Y"] {
            assert_eq!(normalize_install_dir(p), None, "{p}");
        }
    }

    #[test]
    fn not_a_plain_drive_path_is_refused() {
        for p in ["", "C:", "C:\\", "C:/", "\\\\server\\share\\g", "\\\\?\\C:\\Games\\X", "Games\\X", "/usr/games", "C:Games", "C:\\Games\\X:stream", "C:\\Games\\a|b", "C:\\Games\\\\X", "1:\\Games"] {
            assert_eq!(normalize_install_dir(p), None, "{p:?}");
        }
    }

    #[test]
    fn broad_system_folders_are_refused_but_a_game_inside_is_not() {
        for p in ["C:/Windows", "C:/Windows/System32/X", "C:/Users", "C:/Users/me", "C:/ProgramData/Epic", "C:/Program Files", "C:/Program Files (x86)", "C:/PROGRAM FILES", "C:/$Recycle.Bin"] {
            assert_eq!(normalize_install_dir(p), None, "{p}");
        }
        assert!(normalize_install_dir("C:/Program Files/Epic Games/X").is_some());
        assert!(normalize_install_dir("C:/Users/me/Games/X").is_some());
    }

    #[test]
    fn too_long_is_refused() {
        assert!(normalize_install_dir(&format!("C:\\{}", "a".repeat(MAX_INSTALL_DIR_BYTES))).is_none());
    }

    #[test]
    fn a_file_must_stay_inside_the_folder() {
        let dir = "C:\\GOG Games\\X";
        assert!(stays_inside(dir, "X.exe"));
        assert!(stays_inside(dir, "bin/X.exe"));
        assert!(stays_inside(dir, "c:\\gog games\\x\\bin\\X.exe"));
        assert!(!stays_inside(dir, "..\\Y\\Y.exe"));
        assert!(!stays_inside(dir, "bin\\..\\..\\Y.exe"));
        assert!(!stays_inside(dir, "C:\\GOG Games\\X"));
        assert!(!stays_inside(dir, "C:\\GOG Games\\XY\\a.exe"));
        assert!(!stays_inside(dir, "C:\\Windows\\notepad.exe"));
        assert!(!stays_inside(dir, ""));
    }
}
