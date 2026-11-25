#pragma once
// Plain data of the graphics and system tweaks: what is applied now, and the power mode (optimize/GraphicsOptimizer reads and writes it).

#include <cstdint>
#include <string>

namespace vegaflow {

struct SystemGraphicsTweaksState {
    bool mpo_disabled = false; // HKLM\SOFTWARE\Microsoft\Windows\Dwm -> OverlayTestMode == 5 (undocumented; off by default)
    uint32_t dwm_overlay_test_mode = 0;
    bool amd_shader_cache_forced = false; // ShaderCache == 32 00 in Class\{4d36e968-e325-11ce-bfc1-08002be10318}
    bool memory_compression_enabled = false; // "MemCompression" kernel process active
    bool mmcss_gaming_optimized = false; // SystemProfile\SystemResponsiveness <= 10 && Tasks\Games\Priority == 6 && Scheduling Category == "High"
    bool amd_dx11_uma_boost = false;     // SurfaceFormatReplacements == 0x3100
    bool gamedvr_disabled = false;       // GameDVR_Enabled == 0
    bool fse_disabled = false;           // GameDVR_FSEBehaviorMode == 2
    bool apu_power_optimized = false;    // APU power plan (PCIe ASPM Off)
    uint32_t win32_priority_separation = 2; // HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl -> Win32PrioritySeparation
    bool kernel_paging_shield_active = false; // HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management -> DisablePagingExecutive == 1
    uint32_t amd_tessellation_max = 0;       // Detected tessellation limit (e.g. 16, 8, 64)
    std::string mmcss_scheduling_category;
    std::string mmcss_sfio_priority;
    uint32_t mmcss_clock_rate = 10000;
    uint32_t amd_vram_reported_mb = 0;
    bool amd_vram_override_active = false;
};

enum class ApuPowerMode {
    Balanced = 0,            // Default: standard aggressive Boost (4.0 GHz)
    GpuPrioritySustained = 1 // Efficient boost (3.2-3.4 GHz ceiling / Mode 4) to yield ~15W continuously to the iGPU
};

} // namespace vegaflow
