//! Epic Games Launcher manifests (`ProgramData\Epic\EpicGamesLauncher\Data\Manifests\*.item`, JSON): which game lives
//! in which folder. Pure parsing; C++ lists and reads the files (`windows/game_launchers.cpp`).
//! Vectors: `tests/vectors/epic_manifests.tsv` with the `.item` files in `tests/vectors/epic/`.

use super::install_dir::{normalize_install_dir, stays_inside};
use super::json_text::{self, Value};
use super::sandbox_core::has_anticheat_signature;

/// A manifest bigger than this is not a real one.
pub const MAX_MANIFEST_BYTES: usize = 1 << 20;

/// Why a manifest gives no game folder. The numbers are the codes of `vf_rust_epic_manifest_install_dir`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Rejection {
    /// Not JSON, not an object, too big, or `InstallLocation` missing.
    Broken = 1,
    /// Epic lists tools and engines too: only `AppCategories` containing `games` counts.
    NotAGame = 2,
    /// `bIsIncompleteInstall` is true (or not a boolean): the folder is half written.
    Incomplete = 3,
    /// `InstallLocation` is not a plain game-sized folder (see `install_dir`).
    BadInstallDir = 4,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EpicGame {
    pub app_name: String,
    pub display_name: String,
    /// Backslashes, no trailing separator.
    pub install_dir: String,
    /// `LaunchExecutable` when it stays inside the folder, else empty.
    pub launch_executable: String,
    /// The folder or the launch executable carries an anti-cheat signature: VegaFlow never modifies this game.
    pub anticheat: bool,
}

fn text<'a>(v: &'a Value, key: &str) -> Option<&'a str> {
    match v.get(key) {
        Some(Value::String(s)) => Some(s),
        _ => None,
    }
}

pub fn parse_manifest(bytes: &[u8]) -> Result<EpicGame, Rejection> {
    if bytes.len() > MAX_MANIFEST_BYTES {
        return Err(Rejection::Broken);
    }
    let root = json_text::parse(bytes).map_err(|_| Rejection::Broken)?;
    if !matches!(root, Value::Object(_)) {
        return Err(Rejection::Broken);
    }
    let location = text(&root, "InstallLocation").ok_or(Rejection::Broken)?;
    let is_game = matches!(root.get("AppCategories"), Some(Value::Array(c)) if c.iter().any(|v| matches!(v, Value::String(s) if s.eq_ignore_ascii_case("games"))));
    if !is_game {
        return Err(Rejection::NotAGame);
    }
    if !matches!(root.get("bIsIncompleteInstall"), Some(Value::Bool(false))) {
        return Err(Rejection::Incomplete);
    }
    let install_dir = normalize_install_dir(location).ok_or(Rejection::BadInstallDir)?;
    let launch = text(&root, "LaunchExecutable").unwrap_or("");
    let launch_executable = if stays_inside(&install_dir, launch) { launch.to_string() } else { String::new() };
    let anticheat = has_anticheat_signature(install_dir.as_bytes()) || has_anticheat_signature(launch.as_bytes());
    Ok(EpicGame {
        app_name: text(&root, "AppName").unwrap_or("").to_string(),
        display_name: text(&root, "DisplayName").unwrap_or("").to_string(),
        install_dir,
        launch_executable,
        anticheat,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    const VECTORS: &str = include_str!("../../../../tests/vectors/epic_manifests.tsv");

    fn item(name: &str) -> Vec<u8> {
        let p = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../tests/vectors/epic").join(name);
        std::fs::read(&p).unwrap_or_else(|e| panic!("{}: {e}", p.display()))
    }

    #[test]
    fn every_vector_row_matches() {
        let mut rows = 0;
        for line in VECTORS.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let f: Vec<&str> = line.split('\t').collect();
            assert_eq!(f.len(), 4, "{line}");
            let (file, expect, dir, anticheat) = (f[0], f[1], f[2], f[3]);
            let got = parse_manifest(&item(file));
            match expect {
                "ok" => {
                    let g = got.unwrap_or_else(|e| panic!("{file}: {e:?}"));
                    assert_eq!(g.install_dir, dir, "{file}");
                    assert_eq!(g.anticheat, anticheat == "1", "{file}");
                }
                other => {
                    let want = match other {
                        "broken" => Rejection::Broken,
                        "not_a_game" => Rejection::NotAGame,
                        "incomplete" => Rejection::Incomplete,
                        "bad_dir" => Rejection::BadInstallDir,
                        x => panic!("unknown expectation {x}"),
                    };
                    assert_eq!(got, Err(want), "{file}");
                }
            }
            rows += 1;
        }
        assert!(rows >= 10, "vectors went missing");
    }

    #[test]
    fn the_plain_game_keeps_its_names_and_launch_executable() {
        let g = parse_manifest(&item("sample_game.item")).unwrap();
        assert_eq!(g.app_name, "SampleApp");
        assert_eq!(g.display_name, "Sample Game");
        assert_eq!(g.launch_executable, "SampleGame/Binaries/Win64/SampleGame-Win64-Shipping.exe");
    }

    #[test]
    fn a_launch_executable_that_escapes_the_folder_is_dropped() {
        let g = parse_manifest(&item("launch_escapes.item")).unwrap();
        assert_eq!(g.launch_executable, "");
    }

    #[test]
    fn empty_and_binary_input_is_broken() {
        assert_eq!(parse_manifest(b""), Err(Rejection::Broken));
        assert_eq!(parse_manifest(&[0xff, 0xfe, 0x00]), Err(Rejection::Broken));
        assert_eq!(parse_manifest(b"[]"), Err(Rejection::Broken));
        assert_eq!(parse_manifest(&vec![b' '; MAX_MANIFEST_BYTES + 1]), Err(Rejection::Broken));
    }

    #[test]
    fn a_bom_is_skipped() {
        let mut bytes = vec![0xEF, 0xBB, 0xBF];
        bytes.extend(item("sample_game.item"));
        assert!(parse_manifest(&bytes).is_ok());
    }
}
