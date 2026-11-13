//! Universal hardware & GPU device profile policy (phase 3).
//!
//! Pure decision over probed adapters, CPU family/model, RAM, power status, and OS architecture.
//! Both Rust tests and C++ unit tests verify this rule against `tests/vectors/device_profiles.tsv`.

use crate::policy::apu_tier::{self, VfTierInput, TIER_DISCRETE};

pub const VF_GPU_CONN_UNKNOWN: u8 = 0;
pub const VF_GPU_CONN_INTEGRATED: u8 = 1;
pub const VF_GPU_CONN_DISCRETE: u8 = 2;
pub const VF_GPU_CONN_EXTERNAL: u8 = 3;

pub const VF_DISK_UNKNOWN: u8 = 0;
pub const VF_DISK_HDD: u8 = 1;
pub const VF_DISK_SSD: u8 = 2;
pub const VF_DISK_NVME: u8 = 3;

pub const VF_ARCH_UNKNOWN: u8 = 0;
pub const VF_ARCH_X64: u8 = 1;
pub const VF_ARCH_ARM64_EMULATED: u8 = 2;
pub const VF_ARCH_ARM64_NATIVE: u8 = 3;
pub const VF_ARCH_X86: u8 = 4;

pub const VF_GPU_PREF_DEFAULT: u8 = 0;
pub const VF_GPU_PREF_POWER_SAVING: u8 = 1;
pub const VF_GPU_PREF_HIGH_PERF: u8 = 2;

const VENDOR_AMD: u32 = 0x1002;
const VENDOR_INTEL: u32 = 0x8086;
const VENDOR_NVIDIA: u32 = 0x10DE;

const ARCH_UNKNOWN: u8 = 0;
const ARCH_AMD_GCN5_VEGA: u8 = 1;
const ARCH_AMD_RDNA2: u8 = 2;
const ARCH_AMD_RDNA3_PLUS: u8 = 3;
const ARCH_INTEL_UHD: u8 = 4;
const ARCH_INTEL_XE_ARC: u8 = 5;
const ARCH_DISCRETE: u8 = 6;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VfGpuAdapterInput {
    pub vendor_id: u32,
    pub device_id: u32,
    pub dedicated_vram_mb: u64,
    pub shared_sys_mb: u64,
    pub dxcore_available: bool,
    pub dxcore_integrated: bool,
    pub dxcore_detachable: bool,
    pub removal_detachable: bool,
    pub name_ptr: *const u8,
    pub name_len: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct VfDeviceProfileInput {
    pub total_ram_mb: u64,
    pub physical_cores: u32,
    pub cpu_family: u32,
    pub cpu_model: u32,
    pub user_gpu_pref: u8,
    pub system_arch: u8,
    pub power_known: bool,
    pub has_battery: bool,
    pub ac_online: bool,
    pub battery_saver: bool,
    pub dxcore_available: bool,
    pub ram_probe_ok: bool,
    pub experimental_egpu_streaming: bool,
    pub override_arch_gen: u8, // 0 = resolve automatically; 1..6 = explicit test override
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VfDeviceProfileDecision {
    pub selected_gpu_index: u32,
    pub gpu_probe_ok: bool,
    pub gpu_connection: u8,
    pub gpu_arch_gen: u8,
    pub apu_tier: u8,
    pub apply_visual_cuts: bool,
    pub apply_uma_memory_guard: bool,
    pub apply_uma_vram_reg_override: bool,
    pub allow_ram_purge: bool,
    pub force_high_perf_power_plan: bool,
    pub block_driver_tweaks: bool,
    pub apply_egpu_streaming_tweaks: bool,
    pub safe_vram_pool_mb: u32,
    pub amd_tessellation_cap: u32,
    pub dxvk_compiler_threads: u32,
    pub disable_dx12_async_compute: bool,
}

pub struct ParsedGpuAdapter<'a> {
    pub vendor_id: u32,
    pub device_id: u32,
    pub dxcore_available: bool,
    pub dxcore_integrated: bool,
    pub dxcore_detachable: bool,
    pub removal_detachable: bool,
    pub name: &'a str,
}

fn classify_connection(adapter: &ParsedGpuAdapter<'_>, global_dxcore: bool) -> u8 {
    let dx_ok = adapter.dxcore_available && global_dxcore;
    if adapter.dxcore_detachable || (adapter.removal_detachable && !(dx_ok && adapter.dxcore_integrated)) {
        VF_GPU_CONN_EXTERNAL
    } else if dx_ok {
        if adapter.dxcore_integrated {
            VF_GPU_CONN_INTEGRATED
        } else {
            VF_GPU_CONN_DISCRETE
        }
    } else {
        VF_GPU_CONN_UNKNOWN
    }
}

fn resolve_amd_integrated_arch(device_id: u32, lower_name: &str, cpu_family: u32, cpu_model: u32) -> u8 {
    // Exact PCI Device IDs take priority over driver marketing strings.
    match device_id {
        // RDNA2 iGPUs: Raphael (7000), Granite Ridge (9000), Steam Deck (Van Gogh / Sephiroth), Rembrandt (680M/660M), Mendocino (610M)
        0x164E | 0x13C0 | 0x163F | 0x1435 | 0x1681 | 0x164D | 0x1506 => return ARCH_AMD_RDNA2,
        // RDNA3 / RDNA3.5 iGPUs: Phoenix / Hawk Point (780M/760M/740M/Z1), Strix Point / Halo (880M/890M), Krackan Point
        0x15BF | 0x15C8 | 0x1900 | 0x150E | 0x1586 | 0x1114 => return ARCH_AMD_RDNA3_PLUS,
        // GCN5 Vega iGPUs: Raven, Picasso, Renoir (4600H), Cezanne, Lucienne, Barcelo
        0x15DD | 0x15D8 | 0x1636 | 0x1638 | 0x164C | 0x15E7 => return ARCH_AMD_GCN5_VEGA,
        _ => {}
    }

    if lower_name.contains("780m")
        || lower_name.contains("760m")
        || lower_name.contains("740m")
        || lower_name.contains("880m")
        || lower_name.contains("890m")
        || lower_name.contains("860m")
        || lower_name.contains("840m")
    {
        return ARCH_AMD_RDNA3_PLUS;
    }

    if lower_name.contains("680m")
        || lower_name.contains("660m")
        || lower_name.contains("610m")
        || lower_name.contains("custom gpu 0405")
        || lower_name.contains("custom gpu 0932")
    {
        return ARCH_AMD_RDNA2;
    }

    if lower_name.contains("vega") {
        return ARCH_AMD_GCN5_VEGA;
    }

    // Narrow CPUID fallback for mobile families when driver/device ID was not in the table.
    // Family 0x1A is NOT defaulted to RDNA3 because Granite Ridge desktop carries RDNA2.
    if cpu_family == 0x19 && (0x70..=0x7F).contains(&cpu_model) {
        return ARCH_AMD_RDNA3_PLUS;
    }
    if cpu_family == 0x19 && (0x40..=0x4F).contains(&cpu_model) {
        return ARCH_AMD_RDNA2;
    }

    ARCH_UNKNOWN
}

fn resolve_intel_integrated_arch(device_id: u32, lower_name: &str) -> u8 {
    // Gen12+ Xe-LP / Xe-LPG / Xe2 / Xe3 (includes 12th/13th/14th Gen UHD 710/730/770, Iris Xe, Core Ultra Arc)
    if (0x9A40..=0x9A7F).contains(&device_id)
        || (0x4C80..=0x4C9F).contains(&device_id)
        || (0x4620..=0x46DF).contains(&device_id)
        || (0xA720..=0xA7BF).contains(&device_id)
        || (0x7D40..=0x7DFF).contains(&device_id)
        || (0x6400..=0x64FF).contains(&device_id)
        || (0xB080..=0xB0AF).contains(&device_id)
    {
        return ARCH_INTEL_XE_ARC;
    }

    // Gen9 - Gen11 Intel UHD / HD Graphics (Skylake through Ice Lake / Comet Lake)
    if (0x1900..=0x193F).contains(&device_id)
        || (0x5900..=0x593F).contains(&device_id)
        || (0x3E90..=0x3E9F).contains(&device_id)
        || (0x9B00..=0x9BFF).contains(&device_id)
        || (0x8A50..=0x8A7F).contains(&device_id)
        || device_id == 0x3184
        || device_id == 0x3185
    {
        return ARCH_INTEL_UHD;
    }

    if lower_name.contains("arc")
        || lower_name.contains("iris")
        || lower_name.contains("xe")
        || lower_name.contains("uhd graphics 770")
        || lower_name.contains("uhd graphics 730")
        || lower_name.contains("uhd graphics 710")
    {
        return ARCH_INTEL_XE_ARC;
    }

    if lower_name.contains("uhd") || lower_name.contains("hd graphics") {
        return ARCH_INTEL_UHD;
    }

    ARCH_UNKNOWN
}

fn select_adapter(adapters: &[ParsedGpuAdapter<'_>], input: &VfDeviceProfileInput) -> Option<usize> {
    if adapters.is_empty() {
        return None;
    }

    if input.user_gpu_pref == VF_GPU_PREF_POWER_SAVING {
        if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_INTEGRATED) {
            return Some(i);
        }
    }

    if input.user_gpu_pref == VF_GPU_PREF_HIGH_PERF {
        if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_EXTERNAL) {
            return Some(i);
        }
        if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_DISCRETE) {
            return Some(i);
        }
    }

    // Default preference when multiple adapters exist (e.g. hybrid laptop or docked eGPU):
    // prefer External -> Discrete -> Integrated -> first adapter.
    if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_EXTERNAL) {
        return Some(i);
    }
    if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_DISCRETE) {
        return Some(i);
    }
    if let Some(i) = adapters.iter().position(|a| classify_connection(a, input.dxcore_available) == VF_GPU_CONN_INTEGRATED) {
        return Some(i);
    }
    Some(0)
}

pub fn evaluate_profile(adapters: &[ParsedGpuAdapter<'_>], input: &VfDeviceProfileInput) -> VfDeviceProfileDecision {
    let force_high_perf_power_plan = input.power_known && input.ac_online && !input.battery_saver;
    let allow_ram_purge = input.ram_probe_ok;
    let cores = if input.physical_cores > 0 { input.physical_cores } else { 4 };
    let ram_mb = if input.ram_probe_ok { input.total_ram_mb } else { 0 };

    let Some(sel_idx) = select_adapter(adapters, input) else {
        // Unreadable GPU: prudent discrete-style pass-through, block driver tweaks.
        return VfDeviceProfileDecision {
            selected_gpu_index: u32::MAX,
            gpu_probe_ok: false,
            gpu_connection: VF_GPU_CONN_UNKNOWN,
            gpu_arch_gen: ARCH_DISCRETE,
            apu_tier: TIER_DISCRETE,
            apply_visual_cuts: false,
            apply_uma_memory_guard: false,
            apply_uma_vram_reg_override: false,
            allow_ram_purge,
            force_high_perf_power_plan,
            block_driver_tweaks: true,
            apply_egpu_streaming_tweaks: false,
            safe_vram_pool_mb: 4096,
            amd_tessellation_cap: 64,
            dxvk_compiler_threads: cores.saturating_sub(2).clamp(4, 8),
            disable_dx12_async_compute: false,
        };
    };

    let adapter = &adapters[sel_idx];
    let conn = classify_connection(adapter, input.dxcore_available);

    let arch_gen = if input.override_arch_gen != 0 {
        input.override_arch_gen
    } else if conn != VF_GPU_CONN_INTEGRATED {
        ARCH_DISCRETE
    } else {
        let lower_name = adapter.name.to_ascii_lowercase();
        match adapter.vendor_id {
            VENDOR_AMD => resolve_amd_integrated_arch(adapter.device_id, &lower_name, input.cpu_family, input.cpu_model),
            VENDOR_INTEL => resolve_intel_integrated_arch(adapter.device_id, &lower_name),
            _ => ARCH_UNKNOWN,
        }
    };

    let apu_tier = apu_tier::classify(&VfTierInput {
        total_ram_mb: ram_mb,
        physical_cores: input.physical_cores,
        arch_gen,
        dedicated_vram: conn != VF_GPU_CONN_INTEGRATED,
        is_nvidia: adapter.vendor_id == VENDOR_NVIDIA,
    });

    let block_driver_tweaks = input.system_arch == VF_ARCH_ARM64_EMULATED
        || input.system_arch == VF_ARCH_ARM64_NATIVE
        || conn == VF_GPU_CONN_UNKNOWN;

    let apply_visual_cuts = conn == VF_GPU_CONN_INTEGRATED;
    let apply_uma_memory_guard = conn == VF_GPU_CONN_INTEGRATED && input.ram_probe_ok;
    let apply_uma_vram_reg_override =
        conn == VF_GPU_CONN_INTEGRATED && adapter.vendor_id == VENDOR_AMD && !block_driver_tweaks;
    let apply_egpu_streaming_tweaks = conn == VF_GPU_CONN_EXTERNAL && input.experimental_egpu_streaming;

    let (safe_vram_pool_mb, amd_tessellation_cap, dxvk_compiler_threads, disable_dx12_async_compute) = match apu_tier {
        1 => (
            (((ram_mb * 22) / 100) as u32).clamp(1536, 2048),
            8,
            cores.saturating_sub(1).clamp(1, 3),
            true,
        ),
        2 => (4096, 16, cores.clamp(4, 6), true),
        3 => (
            (((ram_mb * 30) / 100) as u32).clamp(4096, 8192),
            32,
            cores.saturating_sub(2).clamp(4, 8),
            false,
        ),
        _ => (
            4096,
            64,
            cores.saturating_sub(2).clamp(4, 8),
            false,
        ),
    };

    VfDeviceProfileDecision {
        selected_gpu_index: sel_idx as u32,
        gpu_probe_ok: true,
        gpu_connection: conn,
        gpu_arch_gen: arch_gen,
        apu_tier,
        apply_visual_cuts,
        apply_uma_memory_guard,
        apply_uma_vram_reg_override,
        allow_ram_purge,
        force_high_perf_power_plan,
        block_driver_tweaks,
        apply_egpu_streaming_tweaks,
        safe_vram_pool_mb,
        amd_tessellation_cap,
        dxvk_compiler_threads,
        disable_dx12_async_compute,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::vf_rust_evaluate_device_profile;
    use proptest::prelude::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/device_profiles.tsv");

    fn parse_hex_u32(s: &str) -> u32 {
        let trimmed = s.trim();
        if let Some(hex) = trimmed.strip_prefix("0x").or_else(|| trimmed.strip_prefix("0X")) {
            u32::from_str_radix(hex, 16).unwrap()
        } else {
            trimmed.parse().unwrap()
        }
    }

    fn parse_conn(s: &str) -> u8 {
        match s {
            "unknown" => VF_GPU_CONN_UNKNOWN,
            "integrated" => VF_GPU_CONN_INTEGRATED,
            "discrete" => VF_GPU_CONN_DISCRETE,
            "external" => VF_GPU_CONN_EXTERNAL,
            other => panic!("unexpected connection string: {other}"),
        }
    }

    #[test]
    fn shared_device_profile_vectors_match() {
        let mut checked = 0;
        for line in VECTORS.lines().map(str::trim_end) {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let col: Vec<&str> = line.split('\t').collect();
            assert_eq!(col.len(), 22, "bad vector row ({}): {line}", col.len());

            let mut owned_names: Vec<String> = Vec::new();
            let mut raw_specs: Vec<(u32, u32, u64, bool, bool, bool, bool)> = Vec::new();
            if col[1] != "-" {
                for gpu_str in col[1].split(';') {
                    let p: Vec<&str> = gpu_str.splitn(8, ':').collect();
                    assert_eq!(p.len(), 8, "bad gpu spec in {}: {gpu_str}", col[0]);
                    raw_specs.push((
                        parse_hex_u32(p[0]),
                        parse_hex_u32(p[1]),
                        p[2].parse().unwrap(),
                        p[3] == "1",
                        p[4] == "1",
                        p[5] == "1",
                        p[6] == "1",
                    ));
                    owned_names.push(p[7].to_string());
                }
            }

            let ffi_adapters: Vec<VfGpuAdapterInput> = raw_specs
                .iter()
                .zip(owned_names.iter())
                .map(|(sp, name)| VfGpuAdapterInput {
                    vendor_id: sp.0,
                    device_id: sp.1,
                    dedicated_vram_mb: sp.2,
                    shared_sys_mb: 8192,
                    dxcore_available: sp.3,
                    dxcore_integrated: sp.4,
                    dxcore_detachable: sp.5,
                    removal_detachable: sp.6,
                    name_ptr: name.as_ptr(),
                    name_len: name.len(),
                })
                .collect();

            let (power_known, ac_online, battery_saver) = match col[9] {
                "ac" => (true, true, false),
                "battery" => (true, false, false),
                "saver" => (true, true, true),
                "unknown" => (false, false, false),
                other => panic!("bad power state: {other}"),
            };

            let sys_arch = match col[8] {
                "x64" => VF_ARCH_X64,
                "arm64_emu" => VF_ARCH_ARM64_EMULATED,
                other => panic!("bad system arch: {other}"),
            };

            let input = VfDeviceProfileInput {
                total_ram_mb: col[3].parse().unwrap(),
                physical_cores: col[5].parse().unwrap(),
                cpu_family: parse_hex_u32(col[6]),
                cpu_model: parse_hex_u32(col[7]),
                user_gpu_pref: col[2].parse().unwrap(),
                system_arch: sys_arch,
                power_known,
                has_battery: col[10] == "1",
                ac_online,
                battery_saver,
                dxcore_available: col[11] == "1",
                ram_probe_ok: col[4] == "1",
                experimental_egpu_streaming: false,
                override_arch_gen: 0,
            };

            let mut out: VfDeviceProfileDecision = unsafe { core::mem::zeroed() };
            let ptr = if ffi_adapters.is_empty() { core::ptr::null() } else { ffi_adapters.as_ptr() };
            let ok = vf_rust_evaluate_device_profile(ptr, ffi_adapters.len(), &input, &mut out);
            assert!(ok, "ffi call failed for {}", col[0]);

            let exp_idx: i32 = col[12].parse().unwrap();
            let actual_idx: i32 = if out.selected_gpu_index == u32::MAX { -1 } else { out.selected_gpu_index as i32 };
            assert_eq!(actual_idx, exp_idx, "selected_gpu_index mismatch on {}", col[0]);
            assert_eq!(out.gpu_connection, parse_conn(col[13]), "connection mismatch on {}", col[0]);
            assert_eq!(out.gpu_arch_gen, col[14].parse::<u8>().unwrap(), "arch_gen mismatch on {}", col[0]);
            assert_eq!(out.apu_tier, col[15].parse::<u8>().unwrap(), "tier mismatch on {}", col[0]);
            assert_eq!(out.apply_visual_cuts, col[16] == "1", "visual_cuts mismatch on {}", col[0]);
            assert_eq!(out.apply_uma_memory_guard, col[17] == "1", "uma_guard mismatch on {}", col[0]);
            assert_eq!(out.apply_uma_vram_reg_override, col[18] == "1", "vram_reg mismatch on {}", col[0]);
            assert_eq!(out.allow_ram_purge, col[19] == "1", "ram_purge mismatch on {}", col[0]);
            assert_eq!(out.force_high_perf_power_plan, col[20] == "1", "power_plan mismatch on {}", col[0]);
            assert_eq!(out.block_driver_tweaks, col[21] == "1", "block_drivers mismatch on {}", col[0]);

            checked += 1;
        }
        assert!(checked >= 25, "expected >= 25 device profile vectors, got {checked}");
    }

    proptest! {
        #[test]
        fn non_integrated_never_applies_visual_cuts_or_uma_guard(
            vendor_id in any::<u32>(),
            device_id in any::<u32>(),
            vram_mb in 0u64..65536,
            ram_mb in 4096u64..131072,
            cores in 1u32..128,
            dxcore_avail in any::<bool>(),
            detachable in any::<bool>(),
            removal_det in any::<bool>(),
        ) {
            let adapters = [ParsedGpuAdapter {
                vendor_id,
                device_id,
                dxcore_available: dxcore_avail,
                dxcore_integrated: false,
                dxcore_detachable: detachable,
                removal_detachable: removal_det,
                name: "Test Adapter",
            }];
            let input = VfDeviceProfileInput {
                total_ram_mb: ram_mb,
                physical_cores: cores,
                cpu_family: 0x19,
                cpu_model: 0x61,
                user_gpu_pref: 0,
                system_arch: VF_ARCH_X64,
                power_known: true,
                has_battery: false,
                ac_online: true,
                battery_saver: false,
                dxcore_available: dxcore_avail,
                ram_probe_ok: vram_mb > 0,
                experimental_egpu_streaming: false,
                override_arch_gen: 0,
            };
            let d = evaluate_profile(&adapters, &input);
            prop_assert!(!d.apply_visual_cuts);
            prop_assert!(!d.apply_uma_memory_guard);
            prop_assert!(!d.apply_uma_vram_reg_override);
            prop_assert_eq!(d.apu_tier, TIER_DISCRETE);
        }
    }
}
