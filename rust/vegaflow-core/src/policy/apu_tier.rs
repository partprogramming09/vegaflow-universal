//! APU tier classification: a pure decision over a few hardware facts.
//!
//! The thresholds and their order mirror the C++ policy this replaces; the shared vector file
//! `tests/vectors/apu_tier.tsv` is checked by both this crate and the C++ suite (Test 32).

#[repr(C)]
pub struct VfTierInput {
    pub total_ram_mb: u64,
    pub physical_cores: u32,
    /// GpuArchitectureGen as a number (0 unknown, 1 GCN5/Vega, 2 RDNA2, 3 RDNA3+, 4 Intel UHD, 5 Intel Xe/Arc, 6 discrete).
    pub arch_gen: u8,
    pub dedicated_vram: bool,
    pub is_nvidia: bool,
}

pub(crate) const TIER_ENTRY: u8 = 1;
pub(crate) const TIER_MID: u8 = 2;
pub(crate) const TIER_ADVANCED: u8 = 3;
pub(crate) const TIER_DISCRETE: u8 = 4;

const ARCH_RDNA2: u8 = 2;
const ARCH_RDNA3_PLUS: u8 = 3;
const ARCH_INTEL_UHD: u8 = 4;
const ARCH_INTEL_XE_ARC: u8 = 5;
const ARCH_DISCRETE: u8 = 6;

/// Entry tier at or below this much RAM (9 GB).
const ENTRY_MAX_RAM_MB: u64 = 9216;
/// Advanced tier from this much RAM on (22 GB), even on an older GPU.
const ADVANCED_MIN_RAM_MB: u64 = 22528;
/// Entry tier at or below this many physical cores.
const ENTRY_MAX_CORES: u32 = 4;
/// Used when the core count is unknown (0).
const DEFAULT_CORES: u32 = 4;

/// Returns 1 (entry), 2 (mid), 3 (advanced) or 4 (discrete or custom).
/// The checks run in this order on purpose: discrete first, then entry, then advanced.
pub fn classify(input: &VfTierInput) -> u8 {
    classify_with_rule(input).0
}

/// The first rule of `classify` that matched (F2.7: `--doctor` says why a tier was chosen).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum TierRule {
    Discrete,
    EntryRam,
    EntryCores,
    EntryIntelUhd,
    AdvancedGpu,
    AdvancedRam,
    Mid,
}

/// Same decision as `classify`, with the rule that made it.
pub(crate) fn classify_with_rule(input: &VfTierInput) -> (u8, TierRule) {
    let cores = if input.physical_cores > 0 { input.physical_cores } else { DEFAULT_CORES };

    if input.dedicated_vram || input.arch_gen == ARCH_DISCRETE || input.is_nvidia {
        (TIER_DISCRETE, TierRule::Discrete)
    } else if input.total_ram_mb <= ENTRY_MAX_RAM_MB {
        (TIER_ENTRY, TierRule::EntryRam)
    } else if cores <= ENTRY_MAX_CORES {
        (TIER_ENTRY, TierRule::EntryCores)
    } else if input.arch_gen == ARCH_INTEL_UHD {
        (TIER_ENTRY, TierRule::EntryIntelUhd)
    } else if matches!(input.arch_gen, ARCH_RDNA2 | ARCH_RDNA3_PLUS | ARCH_INTEL_XE_ARC) {
        (TIER_ADVANCED, TierRule::AdvancedGpu)
    } else if input.total_ram_mb >= ADVANCED_MIN_RAM_MB {
        (TIER_ADVANCED, TierRule::AdvancedRam)
    } else {
        (TIER_MID, TierRule::Mid)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::*;

    const VECTORS: &str = include_str!("../../../../tests/vectors/apu_tier.tsv");

    #[test]
    fn classification_matches_the_shared_vectors() {
        let mut checked = 0;
        for line in VECTORS.lines().map(str::trim_end) {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            let col: Vec<&str> = line.split('\t').collect();
            assert_eq!(col.len(), 7, "bad vector row: {line}");
            let input = VfTierInput {
                dedicated_vram: col[1] == "dedicated",
                is_nvidia: col[2] == "nvidia",
                arch_gen: col[3].parse().unwrap(),
                total_ram_mb: col[4].parse().unwrap(),
                physical_cores: col[5].parse().unwrap(),
            };
            let expected: u8 = col[6].parse().unwrap();
            assert_eq!(vf_rust_classify_apu_tier(&input), expected, "vector {}", col[0]);
            checked += 1;
        }
        assert!(checked >= 19, "expected at least 19 vectors, found {checked}");
    }

    #[test]
    fn contract_numbers_match_the_cpp_enums() {
        // GpuArchitectureGen and ApuTier (src/model/hardware.hpp) are static_asserted in hardware_profiler.cpp against the same numbers.
        assert_eq!([ARCH_RDNA2, ARCH_RDNA3_PLUS, ARCH_INTEL_UHD, ARCH_INTEL_XE_ARC, ARCH_DISCRETE], [2, 3, 4, 5, 6]);
        assert_eq!([TIER_ENTRY, TIER_MID, TIER_ADVANCED, TIER_DISCRETE], [1, 2, 3, 4]);
    }

    #[test]
    fn null_input_falls_back_to_the_mid_tier() {
        assert_eq!(vf_rust_classify_apu_tier(core::ptr::null()), 2);
    }
}
