//! Game launchers other than Steam: Epic manifests and GOG registry entries. C++ reads the files and the registry and
//! hands the bytes over; the decision is in `policy/epic_manifest.rs` and `policy/gog_registry.rs`.

use super::{bytes_or_empty, ffi_guard};
use crate::policy::{epic_manifest, gog_registry};
use std::ffi::c_void;

/// The folder was accepted and no anti-cheat showed in the manifest or entry.
pub const VF_LAUNCHER_GAME: u32 = 0;
/// Codes 1 to 5 refuse the entry: see `epic_manifest::Rejection` and `gog_registry::Rejection`.
/// The folder was accepted but the manifest or entry names an anti-cheat: VegaFlow must never modify it.
pub const VF_LAUNCHER_GAME_ANTICHEAT: u32 = 10;
/// Bad pointers or a panic: treat as refused.
pub const VF_LAUNCHER_ERROR: u32 = 255;

/// Called once with the install folder (UTF-8, backslashes, not NUL-terminated, valid only during the call).
pub type VfLauncherDirFn = Option<unsafe extern "C" fn(ctx: *mut c_void, utf8_path: *const u8, len: usize)>;

fn report(dir: &str, anticheat: bool, on_dir: VfLauncherDirFn, ctx: *mut c_void) -> u32 {
    if let Some(callback) = on_dir {
        unsafe { callback(ctx, dir.as_ptr(), dir.len()) };
    }
    if anticheat { VF_LAUNCHER_GAME_ANTICHEAT } else { VF_LAUNCHER_GAME }
}

/// See `epic_manifest::parse_manifest`. Calls `on_dir` only for an accepted game (status 0 or 10).
#[no_mangle]
pub extern "C" fn vf_rust_epic_manifest_install_dir(json: *const u8, len: usize, on_dir: VfLauncherDirFn, ctx: *mut c_void) -> u32 {
    ffi_guard(VF_LAUNCHER_ERROR, || {
        let Some(bytes) = (unsafe { bytes_or_empty(json, len) }) else { return VF_LAUNCHER_ERROR };
        match epic_manifest::parse_manifest(bytes) {
            Ok(game) => report(&game.install_dir, game.anticheat, on_dir, ctx),
            Err(rejection) => rejection as u32,
        }
    })
}

/// See `gog_registry::parse_entry`; each text is UTF-8 (the registry's UTF-16 converted by C++). Calls `on_dir` only
/// for an accepted game (status 0 or 10).
#[no_mangle]
pub extern "C" fn vf_rust_gog_install_dir(
    key: *const u8, key_len: usize,
    game_id: *const u8, game_id_len: usize,
    path: *const u8, path_len: usize,
    exe: *const u8, exe_len: usize,
    on_dir: VfLauncherDirFn, ctx: *mut c_void,
) -> u32 {
    ffi_guard(VF_LAUNCHER_ERROR, || {
        let text = |p, n| unsafe { bytes_or_empty(p, n) }.and_then(|b| std::str::from_utf8(b).ok());
        let (Some(key), Some(game_id), Some(path), Some(exe)) =
            (text(key, key_len), text(game_id, game_id_len), text(path, path_len), text(exe, exe_len))
        else {
            return VF_LAUNCHER_ERROR;
        };
        match gog_registry::parse_entry(key, game_id, path, exe) {
            Ok(game) => report(&game.install_dir, game.anticheat, on_dir, ctx),
            Err(rejection) => rejection as u32,
        }
    })
}
