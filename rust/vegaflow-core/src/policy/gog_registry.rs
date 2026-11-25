//! GOG Galaxy installs, from `HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<gameID>` (values `gameID`, `gameName`, `path`,
//! `exe`). C++ reads the registry (`windows/game_launchers.cpp`); this file only decides what the values mean.
//! Vectors: `tests/vectors/gog_games.tsv`.

use super::install_dir::{normalize_install_dir, stays_inside};
use super::sandbox_core::has_anticheat_signature;

/// Why a registry entry gives no game folder. The numbers are the codes of `vf_rust_gog_install_dir`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Rejection {
    /// `gameID` is not 1 to 20 digits, or does not match the key name.
    BadGameId = 1,
    /// `path` is not a plain game-sized folder (see `install_dir`).
    BadInstallDir = 4,
    /// `exe` lies outside `path`.
    ExeOutsideFolder = 5,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GogGame {
    pub game_id: String,
    /// Backslashes, no trailing separator.
    pub install_dir: String,
    /// The folder or the exe carries an anti-cheat signature: VegaFlow never modifies this game.
    pub anticheat: bool,
}

/// `key_name` is the registry subkey; `game_id`, `path` and `exe` are the values read from it (empty if missing).
pub fn parse_entry(key_name: &str, game_id: &str, path: &str, exe: &str) -> Result<GogGame, Rejection> {
    let digits = |s: &str| !s.is_empty() && s.len() <= 20 && s.bytes().all(|b| b.is_ascii_digit());
    if !digits(game_id) || key_name != game_id {
        return Err(Rejection::BadGameId);
    }
    let install_dir = normalize_install_dir(path).ok_or(Rejection::BadInstallDir)?;
    // `exe` is optional (some entries are add-ons); when present it must be inside the folder.
    if !exe.is_empty() && !stays_inside(&install_dir, exe) {
        return Err(Rejection::ExeOutsideFolder);
    }
    let anticheat = has_anticheat_signature(install_dir.as_bytes()) || has_anticheat_signature(exe.as_bytes());
    Ok(GogGame { game_id: game_id.to_string(), install_dir, anticheat })
}

#[cfg(test)]
mod tests {
    use super::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/gog_games.tsv");

    #[test]
    fn every_vector_row_matches() {
        let mut rows = 0;
        for line in VECTORS.lines().filter(|l| !l.is_empty() && !l.starts_with('#')) {
            let f: Vec<&str> = line.split('\t').collect();
            assert_eq!(f.len(), 7, "{line}");
            let got = parse_entry(f[0], f[1], f[2], f[3]);
            match f[4] {
                "ok" => {
                    let g = got.unwrap_or_else(|e| panic!("{line}: {e:?}"));
                    assert_eq!(g.install_dir, f[5], "{line}");
                    assert_eq!(g.anticheat, f[6] == "1", "{line}");
                }
                "bad_id" => assert_eq!(got, Err(Rejection::BadGameId), "{line}"),
                "bad_dir" => assert_eq!(got, Err(Rejection::BadInstallDir), "{line}"),
                "exe_outside" => assert_eq!(got, Err(Rejection::ExeOutsideFolder), "{line}"),
                x => panic!("unknown expectation {x}"),
            }
            rows += 1;
        }
        assert!(rows >= 10, "vectors went missing");
    }
}
