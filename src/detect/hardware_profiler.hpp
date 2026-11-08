#pragma once

#include "windows/nt_defs.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

enum class GpuVendor {
    Unknown,
    AMD,
    Intel,
    NVIDIA
};

enum class MemoryArchitecture {
    Unknown,
    UMA_Integrated,
    Dedicated_VRAM
};

enum class ApuTier : uint8_t {
    Tier1_EntryUMA = 1,        // <= 8-9GB RAM or <= 4 physical cores (Vega 3/6/8 entry, Athlon, Intel UHD)
    Tier2_MidVegaUMA = 2,      // 12-20GB RAM GCN5/Vega or Zen2/Zen3 6C-8C (Ryzen 5 4600H, 5500U, 5600H)
    Tier3_AdvancedUMA = 3,     // RDNA2/RDNA3/RDNA3.5 (680M, 780M, 890M), Intel Xe/Arc, or UMA >= 22GB RAM
    Tier4_DiscreteOrCustom = 4 // Dedicated GPU (> 2GB VRAM) or unknown non-UMA architecture
};

enum class GpuArchitectureGen : uint8_t {
    Unknown = 0,
    AMD_GCN5_Vega = 1,
    AMD_RDNA2 = 2,
    AMD_RDNA3_Plus = 3,
    Intel_Gen9_11_UHD = 4,
    Intel_Xe_Arc = 5,
    Discrete_GPU = 6
};

struct ApuOptimizationPolicy {
    ApuTier tier = ApuTier::Tier2_MidVegaUMA;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::AMD_GCN5_Vega;
    std::string tier_label;
    std::string guard_mode_name;

    // UMA Memory Guard (DX11 & DX12)
    uint32_t os_reserve_floor_mb = 4096;
    uint32_t safe_vram_pool_mb = 4096;
    uint32_t safe_shared_pool_mb = 8192;
    double runtime_pressure_threshold = 0.85;

    // DirectX 11 Specialization
    uint32_t dxvk_compiler_threads = 6;
    uint32_t amd_tessellation_cap = 16;
    uint32_t max_anisotropy = 4;
    bool recommend_dxvk_gplasync = true;

    // DirectX 12 Specialization
    bool disable_dx12_async_compute = true;
    uint32_t ue_max_csm_resolution = 1024;
    uint32_t ue_max_shadow_cascades = 2;
    bool disable_volumetric_fog = true;

    // CPU STAPM & Power Guard
    uint32_t cpu_stapm_cap_mhz = 3400; // 0 = Uncapped (Do-No-Harm Guard for Tier 3/4)
    bool apply_uma_vram_reg_override = true;
};

struct GpuProfile {
    std::wstring description;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    GpuVendor vendor = GpuVendor::Unknown;
    MemoryArchitecture mem_arch = MemoryArchitecture::Unknown;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::Unknown;
    uint64_t dedicated_vram_bytes = 0;
    uint64_t shared_system_bytes = 0;
};

struct CpuCoreSetInfo {
    uint32_t cpu_set_id = 0;
    uint32_t core_index = 0;
    uint32_t logical_processor_index = 0;
    uint32_t last_level_cache_index = 0; // CCX/CCD on AMD
    uint8_t efficiency_class = 0;        // P-Core vs E-Core on Intel
};

struct CpuTopologyProfile {
    std::string brand_string;
    uint32_t physical_cores = 0;
    uint32_t logical_processors = 0;
    bool has_hybrid_cores = false;
    bool has_multiple_ccx = false;
    std::vector<CpuCoreSetInfo> all_cpu_sets;
    std::vector<ULONG> game_optimal_cpu_set_ids;
    std::vector<ULONG> background_cpu_set_ids;
};

struct SystemHardwareProfile {
    uint64_t total_physical_ram_bytes = 0;
    GpuProfile primary_gpu;
    CpuTopologyProfile cpu_topology;
    ApuTier apu_tier = ApuTier::Tier2_MidVegaUMA;
};

class HardwareProfiler {
public:
    static SystemHardwareProfile DetectHardware();
    static const char* VendorToString(GpuVendor v);
    static const char* MemArchToString(MemoryArchitecture m);
    static ApuOptimizationPolicy BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0);
    static const char* ApuTierToString(ApuTier tier);
    static const char* ArchGenToString(GpuArchitectureGen gen);
    static void PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology);
};

} // namespace vegaflow
