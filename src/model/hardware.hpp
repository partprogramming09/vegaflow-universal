#pragma once

// Hardware types shared by detect/, optimize/, session/ and ui/ (no logic here).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // ULONG in CpuTopologyProfile (to be replaced by a neutral type in its own phase)

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

enum class GpuConnection : uint8_t {
    Unknown = 0,
    Integrated = 1,
    Discrete = 2,
    External = 3
};

enum class GameDiskType : uint8_t {
    Unknown = 0,
    HDD = 1,
    SSD = 2,
    NVMe = 3
};

enum class SystemArch : uint8_t {
    Unknown = 0,
    X64 = 1,
    ARM64_Emulated = 2,
    ARM64_Native = 3,
    X86 = 4
};

enum class ApuTier : uint8_t {
    Tier1_EntryUMA = 1,        // <= 8-9GB RAM or <= 4 physical cores (Vega 3/6/8 entry, Athlon, Intel UHD)
    Tier2_MidVegaUMA = 2,      // 12-20GB RAM GCN5/Vega or Zen2/Zen3 6C-8C (Ryzen 5 4600H, 5500U, 5600H)
    Tier3_AdvancedUMA = 3,     // RDNA2/RDNA3/RDNA3.5 (680M, 780M, 890M), Intel Xe/Arc, or UMA >= 22GB RAM
    Tier4_DiscreteOrCustom = 4 // Dedicated GPU, External eGPU, or Unknown non-UMA connection
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

struct PowerSupplyStatus {
    bool known = false;
    bool has_battery = false;
    bool ac_online = false;
    bool battery_saver = false;
    uint8_t battery_percent = 255;
};

struct DisplayMonitorInfo {
    std::string device_name;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t refresh_hz = 0;
    bool primary = false;
};

struct WindowsVersionInfo {
    bool known = false;
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t build = 0;
    uint32_t ubr = 0;
    bool dxcore_available = false;
    std::string product_name;
    std::string display_version;
};

struct ApuOptimizationPolicy {
    ApuTier tier = ApuTier::Tier2_MidVegaUMA;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::AMD_GCN5_Vega;
    GpuConnection gpu_connection = GpuConnection::Integrated;
    std::string tier_label;
    std::string guard_mode_name;

    // Universal capability gates decided by policy/device_profile.rs
    bool apply_visual_cuts = true;
    bool apply_uma_memory_guard = true;
    bool allow_ram_purge = true;
    bool force_high_perf_power_plan = true;
    bool block_driver_tweaks = false;
    bool experimental_egpu_streaming = false;
    bool apply_egpu_streaming_tweaks = false;

    // UMA Memory Guard (DX11 & DX12)
    uint32_t os_reserve_floor_mb = 4096;
    uint32_t safe_vram_pool_mb = 4096;
    uint32_t safe_shared_pool_mb = 8192;
    double runtime_pressure_threshold = 0.0; // Resolved via vf_rust_vram_pressure_threshold()

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
    uint64_t luid = 0;
    GpuVendor vendor = GpuVendor::Unknown;
    MemoryArchitecture mem_arch = MemoryArchitecture::Unknown;
    GpuConnection connection = GpuConnection::Unknown;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::Unknown;
    uint64_t dedicated_vram_bytes = 0;
    uint64_t shared_system_bytes = 0;
    bool dxcore_available = false;
    bool dxcore_integrated = false;
    bool dxcore_detachable = false;
    bool removal_policy_detachable = false;
    std::string driver_version;
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
    std::vector<GpuProfile> all_gpus;
    CpuTopologyProfile cpu_topology;
    ApuTier apu_tier = ApuTier::Tier2_MidVegaUMA;
    uint32_t cpu_family = 0;
    uint32_t cpu_model = 0;
    uint8_t user_gpu_pref = 0;
    bool gpu_probe_ok = true;
    bool ram_probe_ok = true;
    bool cpu_probe_ok = true;
    bool dxcore_available = false;
    bool experimental_egpu_streaming = false;
    SystemArch system_arch = SystemArch::X64;
    PowerSupplyStatus power_status{};
    WindowsVersionInfo windows_version{};
    std::vector<DisplayMonitorInfo> displays;
    GameDiskType last_game_disk = GameDiskType::Unknown;
    uint64_t adapter_signature = 0;
};

} // namespace vegaflow
