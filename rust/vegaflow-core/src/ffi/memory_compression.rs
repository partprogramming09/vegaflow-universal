//! E2.2: the memory compression advice of `policy/memory_compression.rs` for C++.

use super::ffi_guard;
use crate::policy::memory_compression::{self, Advice};

/// 0 allowed, 1 discouraged, 2 unknown (the probe failed). A panic answers unknown: the cautious "ask first".
#[no_mangle]
pub extern "C" fn vf_rust_memory_compression_off_advice(total_ram_mb: u64, probe_ok: bool) -> u8 {
    ffi_guard(Advice::Unknown as u8, || memory_compression::compression_off_advice(total_ram_mb, probe_ok) as u8)
}
