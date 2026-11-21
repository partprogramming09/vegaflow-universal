//! Hardware: APU tier, memory purge, CPU topology and the GPU / device profile.

use std::os::raw::c_char;

use super::{bytes_or_empty, ffi_guard};
use crate::policy::apu_tier::{self, VfTierInput, TIER_ENTRY, TIER_MID};
use crate::policy::cpu_governor::{self, VfCpuCoreEntry, VfTopologyPartitionOut};
use crate::policy::device_profile::{
    self, ParsedGpuAdapter, VfDeviceProfileDecision, VfDeviceProfileInput, VfGpuAdapterInput,
};
use crate::policy::memory_purge::{self, VfMemoryPurgeDecision, VfMemoryPurgeInput, DEFAULT_VRAM_PRESSURE_THRESHOLD};

/// Returns 1 (entry), 2 (mid), 3 (advanced) or 4 (discrete or custom). A null input yields the mid tier.
#[no_mangle]
pub extern "C" fn vf_rust_classify_apu_tier(input: *const VfTierInput) -> u8 {
    ffi_guard(TIER_ENTRY, || {
        if input.is_null() {
            return TIER_MID;
        }
        apu_tier::classify(unsafe { &*input })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_vram_pressure_threshold() -> f64 {
    ffi_guard(DEFAULT_VRAM_PRESSURE_THRESHOLD, || DEFAULT_VRAM_PRESSURE_THRESHOLD)
}

#[no_mangle]
pub extern "C" fn vf_rust_evaluate_memory_purge(input: *const VfMemoryPurgeInput) -> VfMemoryPurgeDecision {
    ffi_guard(unsafe { core::mem::zeroed() }, || {
        if input.is_null() {
            return VfMemoryPurgeDecision {
                should_purge_standby: false,
                should_trim_background: false,
                skipped_due_to_cooldown: false,
                skipped_due_to_disk_io: false,
                cooldown_remaining_seconds: 0,
                guard_triggered: false,
                dxgi_over_ceiling: false,
                free_ram_critical: false,
                dxgi_budget_utilization: 0.0,
                available_os_headroom_mb: 0,
                total_interventions: 0,
            };
        }
        memory_purge::evaluate_memory_purge(unsafe { &*input })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_is_factory_memory_protected_exe(utf8_name: *const c_char, len: usize) -> bool {
    ffi_guard(true, || {
        if utf8_name.is_null() || len == 0 {
            return true;
        }
        let bytes = unsafe { core::slice::from_raw_parts(utf8_name as *const u8, len) };
        match std::str::from_utf8(bytes) {
            Ok(s) => memory_purge::is_factory_memory_protected_exe(s),
            Err(_) => true,
        }
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_partition_cpu_topology(
    entries: *const VfCpuCoreEntry,
    count: u32,
    out: *mut VfTopologyPartitionOut,
) -> bool {
    ffi_guard(false, || {
        if entries.is_null() || out.is_null() || count == 0 || count > 2048 {
            return false;
        }
        let entries_slice = unsafe { core::slice::from_raw_parts(entries, count as usize) };
        cpu_governor::partition(entries_slice, unsafe { &mut *out })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_partition_cpu_topology_group(
    entries: *const VfCpuCoreEntry,
    count: u32,
    target_group: u16,
    out: *mut VfTopologyPartitionOut,
) -> bool {
    ffi_guard(false, || {
        if entries.is_null() || out.is_null() || count == 0 || count > 2048 {
            return false;
        }
        let entries_slice = unsafe { core::slice::from_raw_parts(entries, count as usize) };
        cpu_governor::partition_for_group(entries_slice, target_group, unsafe { &mut *out })
    })
}

#[no_mangle]
pub extern "C" fn vf_rust_eval_bg_confinement(logical_cores_used: f64, currently_confined: bool) -> bool {
    ffi_guard(false, || cpu_governor::eval_bg_confinement(logical_cores_used, currently_confined))
}

/// Evaluates the universal GPU / hardware device profile decision.
#[no_mangle]
pub extern "C" fn vf_rust_evaluate_device_profile(
    adapters: *const VfGpuAdapterInput,
    adapter_count: usize,
    input: *const VfDeviceProfileInput,
    out: *mut VfDeviceProfileDecision,
) -> bool {
    ffi_guard(false, || {
        if input.is_null() || out.is_null() || (adapters.is_null() && adapter_count != 0) {
            return false;
        }
        let raw_slice: &[VfGpuAdapterInput] = if adapter_count == 0 {
            &[]
        } else {
            unsafe { core::slice::from_raw_parts(adapters, adapter_count) }
        };
        let mut parsed: Vec<ParsedGpuAdapter<'_>> = Vec::with_capacity(raw_slice.len());
        for a in raw_slice {
            let name = match unsafe { bytes_or_empty(a.name_ptr, a.name_len) } {
                Some(b) => core::str::from_utf8(b).unwrap_or(""),
                None => "",
            };
            parsed.push(ParsedGpuAdapter {
                vendor_id: a.vendor_id,
                device_id: a.device_id,
                dxcore_available: a.dxcore_available,
                dxcore_integrated: a.dxcore_integrated,
                dxcore_detachable: a.dxcore_detachable,
                removal_detachable: a.removal_detachable,
                name,
            });
        }
        let decision = device_profile::evaluate_profile(&parsed, unsafe { &*input });
        unsafe { *out = decision };
        true
    })
}
