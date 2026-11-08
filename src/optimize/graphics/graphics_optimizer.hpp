#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

struct TimerResolutionState {
    ULONG minimum_ns100 = 0; // e.g. 156250 (15.625 ms)
    ULONG maximum_ns100 = 0; // e.g. 5000 (0.5 ms)
    ULONG current_ns100 = 0;
    bool active_high_res = false;
};

struct ShaderCacheReport {
    std::wstring path;
    uint64_t total_size_bytes = 0;
    uint32_t file_count = 0;
    bool exists = false;
};

struct SystemGraphicsTweaksState {
    bool mpo_disabled = false; // HKLM\SOFTWARE\Microsoft\Windows\Dwm -> OverlayTestMode == 5
    uint32_t dwm_overlay_test_mode = 0;
    bool amd_shader_cache_forced = false; // ShaderCache == 32 00 in Class\{4d36e968-e325-11ce-bfc1-08002be10318}
    bool memory_compression_enabled = false; // "MemCompression" kernel process active
    bool mmcss_gaming_optimized = false; // Tasks\Games -> GPU Priority == 8 && SystemResponsiveness <= 10
    bool amd_dx11_uma_boost = false;     // SurfaceFormatReplacements == 0x3100
    bool gamedvr_disabled = false;       // GameDVR_Enabled == 0 && AppCaptureEnabled == 0
    bool apu_power_optimized = false;    // APU power plan (Efficient Boost + PCIe ASPM Off)
    uint32_t win32_priority_separation = 2; // HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl -> Win32PrioritySeparation
    bool kernel_paging_shield_active = false; // HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management -> DisablePagingExecutive == 1
    uint32_t amd_tessellation_max = 0;       // Detected tessellation limit (e.g. 16, 8, 64)
    std::string mmcss_scheduling_category = "";
    std::string mmcss_sfio_priority = "";
    uint32_t mmcss_clock_rate = 10000;
    uint32_t amd_vram_reported_mb = 0;
    bool amd_vram_override_active = false;
};

enum class ApuPowerMode {
    Balanced = 0,         // Default: standard aggressive Boost (4.0 GHz)
    GpuPrioritySustained = 1 // Efficient boost (3.2-3.4 GHz ceiling / Mode 4) to yield ~15W continuously to the Vega 6 at 1500 MHz
};

class GraphicsOptimizer {
public:
    static TimerResolutionState QueryTimerResolution();
    static bool EnableHighPrecisionTimer(ULONG desired_ns100 = 5000);
    static bool SetHighResolutionTimer(ULONG desired_ns100 = 5000) { return EnableHighPrecisionTimer(desired_ns100); }
    static bool RestoreDefaultTimer();

    // MPO state and inspection, and system tweaks
    static SystemGraphicsTweaksState QuerySystemGraphicsTweaks();
    static bool SetMpoDisabled(bool disable, bool dry_run = false);
    static bool SetAmdShaderCacheForced(bool force_on, bool dry_run = false);
    static bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false);
    static bool SetMmcssGamingProfileOptimized(bool optimize, bool dry_run = false);
    static bool SetAmdDx11UmaDriverBoost(bool enable, bool dry_run = false);
    static bool ApplyAmdDx11DriverBoost(bool enable, bool dry_run = false);
    static bool SetAmdTessellationCap(uint32_t max_tess = 16, bool dry_run = false);
    static bool SetDynamicPrioritySeparation(uint32_t value, bool dry_run = false);
    static bool SetKernelMemoryPagingShield(bool enable, bool dry_run = false);
    static bool QueryKernelMemoryPagingShield();
    static uint32_t QueryAmdTessellationCap();

    // UMA VRAM Budget Override (Windows & DirectX & AMD Driver)
    static bool ApplyAmdVramReportingOverride(uint32_t target_mb = 4096, bool dry_run = false);
    static bool RestoreAmdVramReporting(bool dry_run = false);
    static bool QueryAmdVramReporting(uint32_t& out_driver_mb, uint64_t& out_dxgi_bytes);

    // MMCSS Audio & Gaming Priority Shield (Round 2 - Risk 2)
    static bool ApplyMmcssTaskTweaks(bool dryRun = false);
    static bool ApplyMmcssTweaks(bool dryRun = false) { return ApplyMmcssTaskTweaks(dryRun); }
    static bool RestoreMmcssTaskTweaks(bool dryRun = false);
    static bool QueryMmcssTaskTweaks(std::string& schedulingCategory, std::string& sfioPriority, uint32_t& clockRate);

    // Disables GameDVR / AppCapture in the background (saves UMA bandwidth in all DX11/DX12 games)
    // and forces zero-latency Fullscreen Optimizations (FSEBehaviorMode = 2)
    static bool SetGameDvrAndFseOptimized(bool optimize, bool dry_run = false);

    // Changes to the GameDVR and AMD driver registry first save the original value; disabling the
    // optimization returns it (or deletes the value if it did not exist). This function restores all pending backups
    // and returns true if all of them could be restored.
    static bool RestoreAllRegistryBackups();

    // APU thermal and power control (GPU-Priority Sustained vs Balanced mode):
    // GpuPrioritySustained saves the power plan values before applying and Balanced returns them;
    // if there are no saved values (another process applied it), Balanced writes the Windows default values.
    static bool SetApuPowerMode(ApuPowerMode mode, bool dry_run = false);

    // Whether this process left GpuPrioritySustained mode applied and it still needs to be restored.
    static bool IsApuPowerModeApplied();

    // Optimizes the APU laptop power profile (Ryzen 4000/5000/6000/7000 + Radeon iGPU):
    // - Kept for compatibility; invokes SetApuPowerMode(GpuPrioritySustained / Balanced)
    static bool SetApuLaptopPowerProfile(bool optimize, bool dry_run = false);

    // Writes to HKCU\Software\Microsoft\DirectX\UserGpuPreferences:
    // [exe_full_path] = "SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;"
    static bool ApplyDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run = false);
    static bool ApplyDxgiFlipModelUpgrade(
        const std::wstring& exe_full_path,
        bool dry_run,
        std::wstring* out_effective_preference);
    static bool ApplyDxgiDirectFlipUpgrade(
        const std::wstring& exe_full_path,
        bool dry_run = false,
        std::wstring* out_effective_preference = nullptr);
    static bool HasDxgiFlipModelUpgrade(const std::wstring& exe_full_path);
    static bool HasDirectFlipUpgrade(const std::wstring& exe_full_path);
    static bool QueryUserGpuPreferences(const std::wstring& exe_full_path, std::wstring& out_prefs);
    static bool RemoveDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run = false);

    // Inspects the shader cache folders (D3DSCache, AMD\DxCache, NVIDIA\DXCache)
    static std::vector<ShaderCacheReport> InspectShaderCaches();
};

} // namespace vegaflow
