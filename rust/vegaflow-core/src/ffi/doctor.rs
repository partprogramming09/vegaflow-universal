//! `--doctor`: the rules of `policy/doctor.rs` for C++. Plain arrays instead of structs, so nothing here needs a layout
//! check in `ffi/layout.rs`.

use super::{bytes_or_empty, ffi_guard};
use crate::policy::doctor::{self, CacheDir, CacheKind};
use crate::policy::json_text;

/// 0 unknown (the probe failed), 1 ok, 2 low, 3 critical. See `doctor::disk_advice`.
#[no_mangle]
pub extern "C" fn vf_rust_doctor_disk_advice(free_bytes: u64, total_bytes: u64) -> u8 {
    ffi_guard(0, || doctor::disk_advice(free_bytes, total_bytes) as u8)
}

fn to_advice(disk: u8) -> doctor::DiskAdvice {
    match disk {
        1 => doctor::DiskAdvice::Ok,
        2 => doctor::DiskAdvice::Low,
        3 => doctor::DiskAdvice::Critical,
        _ => doctor::DiskAdvice::Unknown,
    }
}

/// One entry per cache folder: `kind_exists[i]` is 0 AMD, 1 NVIDIA or 2 generic, plus 0x80 when the folder exists;
/// `bytes[i]` is its size. Writes up to `cap` pairs (severity 0 info / 1 warn, code 1..4) into `out` and returns how many
/// findings there are in total, or 0 for bad pointers.
#[no_mangle]
pub extern "C" fn vf_rust_doctor_cache_findings(
    kind_exists: *const u8,
    bytes: *const u64,
    count: usize,
    disk: u8,
    out: *mut u8,
    cap: usize,
) -> usize {
    ffi_guard(0, || {
        if count > 0 && (kind_exists.is_null() || bytes.is_null()) {
            return 0;
        }
        let dirs: Vec<CacheDir> = (0..count)
            .map(|i| {
                let k = unsafe { *kind_exists.add(i) };
                let kind = match k & 0x7F {
                    0 => CacheKind::Amd,
                    1 => CacheKind::Nvidia,
                    _ => CacheKind::Generic,
                };
                CacheDir { kind, exists: k & 0x80 != 0, bytes: unsafe { *bytes.add(i) } }
            })
            .collect();
        let findings = doctor::shader_cache_advice(&dirs, to_advice(disk));
        if !out.is_null() {
            for (i, f) in findings.iter().take(cap / 2).enumerate() {
                unsafe {
                    *out.add(2 * i) = f.severity as u8;
                    *out.add(2 * i + 1) = f.code as u8;
                }
            }
        }
        findings.len()
    })
}

/// `names_nul`: the running process names or paths in UTF-8, each ended by a NUL. Writes up to `cap` indexes of the OEM
/// tools found (each once) into `out` and returns how many there are in total.
#[no_mangle]
pub extern "C" fn vf_rust_doctor_oem_conflicts(names_nul: *const u8, len: usize, out: *mut u32, cap: usize) -> usize {
    ffi_guard(0, || {
        let Some(buf) = (unsafe { bytes_or_empty(names_nul, len) }) else { return 0 };
        let names: Vec<&str> = buf.split(|&b| b == 0).filter_map(|s| std::str::from_utf8(s).ok()).filter(|s| !s.is_empty()).collect();
        let hits = doctor::oem_conflicts(&names);
        if !out.is_null() {
            for (i, h) in hits.iter().take(cap).enumerate() {
                unsafe { *out.add(i) = *h as u32 };
            }
        }
        hits.len()
    })
}

/// Writes "product\tconflicts" (UTF-8, no NUL) of OEM tool `index` into `buf` and returns its full length; 0 when there is
/// no such tool. Writes nothing past `cap`.
#[no_mangle]
pub extern "C" fn vf_rust_doctor_oem_info(index: u32, buf: *mut u8, cap: usize) -> usize {
    ffi_guard(0, || {
        let Some(t) = doctor::OEM_TOOLS.get(index as usize) else { return 0 };
        let text = format!("{}\t{}", t.product, t.conflicts);
        if !buf.is_null() {
            let n = text.len().min(cap);
            unsafe { core::ptr::copy_nonoverlapping(text.as_ptr(), buf, n) };
        }
        text.len()
    })
}

/// True when the bytes are one well-formed JSON value (the strict parser of `config.json`). For the tests of `--doctor --json`.
#[no_mangle]
pub extern "C" fn vf_rust_doctor_json_is_valid(bytes: *const u8, len: usize) -> bool {
    ffi_guard(false, || match unsafe { bytes_or_empty(bytes, len) } {
        Some(b) => json_text::parse(b).is_ok(),
        None => false,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn disk_and_oem_cross_the_boundary() {
        assert_eq!(vf_rust_doctor_disk_advice(4 << 30, 500 << 30), 3);
        let names = b"C:\\x\\ArmouryCrate.exe\0notepad.exe\0ARMOURYCRATE.EXE\0";
        let mut out = [0u32; 4];
        assert_eq!(vf_rust_doctor_oem_conflicts(names.as_ptr(), names.len(), out.as_mut_ptr(), 4), 1);
        let mut buf = [0u8; 64];
        let n = vf_rust_doctor_oem_info(out[0], buf.as_mut_ptr(), 64);
        assert!(std::str::from_utf8(&buf[..n]).unwrap().starts_with("ASUS Armoury Crate\t"));
        assert_eq!(vf_rust_doctor_oem_info(9999, buf.as_mut_ptr(), 64), 0);
    }

    #[test]
    fn cache_findings_and_json_check() {
        let kinds = [0x80u8];
        let bytes = [12u64 << 30];
        let mut out = [0u8; 8];
        assert_eq!(vf_rust_doctor_cache_findings(kinds.as_ptr(), bytes.as_ptr(), 1, 1, out.as_mut_ptr(), 8), 1);
        assert_eq!(&out[..2], &[0, 4]);
        assert_eq!(vf_rust_doctor_cache_findings(core::ptr::null(), bytes.as_ptr(), 1, 1, out.as_mut_ptr(), 8), 0);
        let good = b"{\"a\":\"C:\\\\x\\\"\"}";
        let bad = b"{\"a\":\"C:\\x\"}"; // a lone backslash escape is not JSON
        assert!(vf_rust_doctor_json_is_valid(good.as_ptr(), good.len()));
        assert!(!vf_rust_doctor_json_is_valid(bad.as_ptr(), bad.len()));
    }
}
