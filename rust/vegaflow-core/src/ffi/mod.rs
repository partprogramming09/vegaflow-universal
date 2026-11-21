//! Every function exported to C++ (`vf_rust_*`), split by domain. Each one checks its pointers, catches panics with
//! `ffi_guard` and calls the rule in `policy/`. The C declarations are in `src/bridge/rust_bridge.h`.

mod config;
mod folder_trust;
mod hardware;
mod ini;
mod layout;
mod paths;
mod process;

pub use config::*;
pub use folder_trust::*;
pub use hardware::*;
pub use ini::*;
pub use layout::*;
pub use paths::*;
pub use process::*;

// E2.5
mod doctor;
pub use doctor::*;

/// Runs `f` for an `extern "C"` function and returns `fallback` if it panics. A panic must never cross into C++ (that is
/// undefined behaviour) and must not take the daemon down; every fallback is the cautious answer for its function
/// (deny, assume anti-cheat present, do not confine, do not purge).
pub(crate) fn ffi_guard<T>(fallback: T, f: impl FnOnce() -> T) -> T {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)).unwrap_or(fallback)
}

unsafe fn slice_or_empty<'a>(ptr: *const u16, len: usize) -> &'a [u16] {
    if ptr.is_null() || len == 0 {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

unsafe fn bytes_or_empty<'a>(ptr: *const u8, len: usize) -> Option<&'a [u8]> {
    if len == 0 {
        Some(&[])
    } else if ptr.is_null() {
        None
    } else {
        Some(core::slice::from_raw_parts(ptr, len))
    }
}
