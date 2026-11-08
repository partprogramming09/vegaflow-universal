#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

// ============================================================================
// SystemInfo Subsystem Data Models
// ============================================================================

struct CpuInfo {
    std::string brand_string;
    uint32_t physical_cores = 0;
    uint32_t logical_processors = 0;
    std::string microarchitecture; // e.g. "Zen 2 Renoir 7nm"
    uint64_t l3_cache_bytes = 0;   // e.g. 8388608 (8 MB)
    double l3_cache_mb = 0.0;      // 8.0
    double boost_target_ghz = 3.4; // 3.4 GHz APU target
    std::string stapm_target;      // e.g. "3.4 GHz (APU Sustained / STAPM 35W-45W)"
    std::vector<uint32_t> active_game_cpu_sets;
    std::vector<uint32_t> active_background_cpu_sets;
};

struct GpuBudgetInfo {
    uint64_t local_budget_bytes = 0;
    uint64_t local_usage_bytes = 0;
    uint64_t non_local_budget_bytes = 0;
    uint64_t non_local_usage_bytes = 0;
    bool valid = false;
};

struct GpuInfo {
    std::string adapter_name;          // e.g. "AMD Radeon(TM) Graphics (Vega 6 / GCN 5.0)"
    std::string architecture;          // "UMA (Unified Memory Architecture)"
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    uint64_t bios_dedicated_vram_bytes = 0; // 512 MB
    double bios_dedicated_vram_mb = 0.0;
    uint64_t dynamic_shared_vram_bytes = 0; // Shared UMA (~7.7 GB)
    double dynamic_shared_vram_gb = 0.0;
    GpuBudgetInfo dxgi_budget;
};

struct RamInfo {
    uint64_t total_physical_bytes = 0;
    double total_physical_mb = 0.0;
    double total_physical_gb = 0.0;
    uint64_t free_real_bytes = 0;      // Free + Zero
    double free_real_mb = 0.0;
    uint64_t in_use_bytes = 0;         // Total - (Free + Standby)
    double in_use_mb = 0.0;
    uint64_t standby_cache_bytes = 0;  // Standby list
    double standby_cache_mb = 0.0;
    uint64_t modified_bytes = 0;       // Modified pages
    double modified_mb = 0.0;
    std::string channel_topology;      // "Dual-Channel (128-bit bus bandwidth)"
    uint32_t populated_dimm_slots = 0;
    uint32_t memory_bus_width_bits = 128;
};

struct OsKernelInfo {
    std::string os_name;            // "Windows 11"
    std::string display_version;    // "23H2" / "24H2" / "26H2"
    uint32_t build_number = 0;      // 22631, 26300, etc.
    uint32_t ubr = 0;               // Update Build Revision
    std::string full_version_string; // "Windows 11 Build 22631"
    double timer_resolution_ms = 0.50; // Current timer resolution in ms
    uint32_t timer_resolution_ns100 = 5000;
    bool is_high_resolution_timer = false;
    bool mmcss_gaming_optimized = false;
    uint32_t mmcss_gpu_priority = 8;
    bool memory_compression_enabled = false;
};

struct ApuGuardInfo {
    std::string tier_name;
    std::string guard_mode;
    std::string arch_generation;
    uint32_t safe_vram_pool_mb = 4096;
    uint32_t os_reserve_floor_mb = 4096;
    uint32_t dxvk_compiler_threads = 6;
    uint32_t amd_tessellation_cap = 16;
    bool dx12_async_compute_allowed = false;
    uint32_t cpu_stapm_cap_mhz = 3400;
};

struct SystemInfoSnapshot {
    CpuInfo cpu;
    GpuInfo gpu;
    RamInfo ram;
    OsKernelInfo os_kernel;
    ApuGuardInfo apu_guard;
};

// ============================================================================
// SystemInfo Orchestrator & Serializer
// ============================================================================

class SystemInfo {
public:
    SystemInfo();
    explicit SystemInfo(const SystemInfoSnapshot& snapshot);

    // Gathers real-time hardware, UMA architecture, RAM topology, and OS specs
    static SystemInfoSnapshot Gather();

    const SystemInfoSnapshot& Snapshot() const { return snapshot_; }

    // Export cleanly to standard JSON
    std::string ToJson() const;
    static std::string ToJson(const SystemInfoSnapshot& snapshot);

private:
    SystemInfoSnapshot snapshot_;

    static CpuInfo GatherCpu();
    static GpuInfo GatherGpu();
    static RamInfo GatherRam();
    static OsKernelInfo GatherOsKernel();
    static ApuGuardInfo GatherApuGuard();
};

} // namespace vegaflow
