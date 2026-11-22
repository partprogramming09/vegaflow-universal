#pragma once
// System and graphics optimization families backed by RegistryPort and PowerPort (no <windows.h>).

#include "model/hardware.hpp"
#include "ports/power.hpp"
#include "ports/registry.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

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

// AMD display driver & UMA VRAM reporting tweaks (instantiated only for AMD GPUs).
class AmdDriverTweaks {
public:
    // `device_id` (PCI, 0 = every AMD adapter) limits every write to that adapter; `allow_vram_override` is false off an iGPU.
    explicit AmdDriverTweaks(RegistryPort& reg, uint32_t device_id = 0, bool allow_vram_override = true)
        : reg_(reg), device_id_(device_id), allow_vram_override_(allow_vram_override) {}

    bool SetShaderCacheForced(bool force_on, const ApuOptimizationPolicy& policy, bool dry_run = false);
    bool SetDx11UmaDriverBoost(bool enable, const ApuOptimizationPolicy& policy, bool dry_run = false);
    bool SetTessellationCap(uint32_t max_tess = 16, bool dry_run = false);
    uint32_t QueryTessellationCap();

    bool ApplyVramReportingOverride(uint32_t target_mb = 4096, bool dry_run = false);
    bool RestoreVramReporting(bool dry_run = false);
    bool QueryVramReporting(uint32_t& out_driver_mb, uint64_t& out_dxgi_bytes);

    void QueryAdapterState(bool& out_shader_cache_forced, bool& out_dx11_uma_boost, uint32_t& out_tess_max);

private:
    RegistryPort& reg_;
    uint32_t device_id_;
    bool allow_vram_override_;
};

// Windows NT kernel scheduler & memory management tweaks.
class KernelSchedulerTweaks {
public:
    explicit KernelSchedulerTweaks(RegistryPort& reg) : reg_(reg) {}

    bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false);
    bool RevertMemoryCompressionChange(bool dry_run = false);
    bool IsMemoryCompressionEnabled();
    // user_off: the user's own "off" in Ajustes writes the factory value after taking VegaFlow's copy; every give-back path leaves it false.
    bool SetDynamicPrioritySeparation(uint32_t value, bool dry_run = false, bool user_off = false);
    uint32_t QueryDynamicPrioritySeparation();
    bool SetKernelMemoryPagingShield(bool enable, bool dry_run = false, bool user_off = false);
    bool QueryKernelMemoryPagingShield();

private:
    RegistryPort& reg_;
};

// MMCSS Multimedia SystemProfile & Tasks\Games priority tweaks (without deprecated GPU Priority).
class MmcssTweaks {
public:
    explicit MmcssTweaks(RegistryPort& reg) : reg_(reg) {}

    bool SetGamingProfileOptimized(bool optimize, bool dry_run = false, bool user_off = false);
    bool ApplyTaskTweaks(bool dry_run = false);
    bool RestoreTaskTweaks(bool dry_run = false);
    bool QueryTaskTweaks(std::string& scheduling_category, std::string& sfio_priority, uint32_t& clock_rate);
    bool QueryGamingProfileOptimized();

private:
    RegistryPort& reg_;
};

// Display, DWM MPO (undocumented, off by default), GameDVR/FSE, and shader cache folder inspection.
class DisplayTweaks {
public:
    explicit DisplayTweaks(RegistryPort& reg) : reg_(reg) {}

    bool SetMpoDisabled(bool disable, bool dry_run = false);
    void QueryMpoState(bool& out_disabled, uint32_t& out_overlay_test_mode);
    bool SetGameDvrCaptureDisabled(bool disable, bool dry_run = false, bool user_off = false);
    // Shutdown and "Devolver todo": puts back the user's GameDVR values only if VegaFlow saved a copy (it turned it off).
    bool RestoreGameDvrCapture(bool dry_run = false);
    bool QueryGameDvrDisabled();
    // Compatibility option for old DX9/DX10 games; never applied by the daemon on its own (E1.4).
    bool SetFullscreenOptimizationsDisabled(bool disable, bool dry_run = false);
    bool QueryFullscreenOptimizationsDisabled();
    std::vector<ShaderCacheReport> InspectShaderCaches();

private:
    RegistryPort& reg_;
};

// Per-game DXGI UserGpuPreferences and AppCompatFlags\Layers tweaks.
class DxgiFlipTweaks {
public:
    explicit DxgiFlipTweaks(RegistryPort& reg) : reg_(reg) {}

    bool ApplyFlipModelUpgrade(
        const std::wstring& exe_full_path,
        bool dry_run = false,
        std::wstring* out_effective_preference = nullptr);
    bool QueryUserGpuPreferences(const std::wstring& exe_full_path, std::wstring& out_prefs);
    // Takes the GPU and HDR part out of values that still hold the exact 0.1.x signature; returns how many it changed.
    size_t MigrateLegacyPreferences(bool dry_run = false);
    bool HasFlipModelUpgrade(const std::wstring& exe_full_path);

private:
    RegistryPort& reg_;
};

// Active Windows power plan tweaks (PCIe ASPM & CPU boost/EPP).
class PowerModeTweaks {
public:
    explicit PowerModeTweaks(PowerPort& power) : power_(power) {}

    bool SetApuPowerMode(ApuPowerMode mode, const ApuOptimizationPolicy& policy, bool dry_run = false);
    bool IsApuPowerModeApplied() const;
    bool SetApuLaptopPowerProfile(bool optimize, const ApuOptimizationPolicy& policy, bool dry_run = false);
    // Shutdown: back to Balanced only if this object applied the profile; false when there was nothing to undo.
    bool RestoreIfApplied(const ApuOptimizationPolicy& policy, bool dry_run = false);
    bool QueryApuPowerOptimized();

private:
    PowerPort& power_;
    mutable std::mutex mutex_;
    bool applied_ = false;
    bool saved_ = false;
    ApuPowerPlanValues prev_values_{};
    std::string saved_scheme_;  // GUID of the plan prev_values_ came from: the original goes back to that plan
};

// Coordinator that decides vendor-gated and policy-gated tweak families in a single place.
class GraphicsOptimizer {
public:
    GraphicsOptimizer(
        const SystemHardwareProfile& hw,
        const ApuOptimizationPolicy& policy,
        RegistryPort& registry,
        PowerPort* power = nullptr);

    AmdDriverTweaks* AmdDriver() { return amd_driver_ ? &*amd_driver_ : nullptr; }
    const AmdDriverTweaks* AmdDriver() const { return amd_driver_ ? &*amd_driver_ : nullptr; }

    KernelSchedulerTweaks& KernelScheduler() { return kernel_; }
    const KernelSchedulerTweaks& KernelScheduler() const { return kernel_; }

    MmcssTweaks& Mmcss() { return mmcss_; }
    const MmcssTweaks& Mmcss() const { return mmcss_; }

    DisplayTweaks& Display() { return display_; }
    const DisplayTweaks& Display() const { return display_; }

    DxgiFlipTweaks& DxgiFlip() { return dxgi_; }
    const DxgiFlipTweaks& DxgiFlip() const { return dxgi_; }

    PowerModeTweaks* PowerMode() { return power_mode_ ? &*power_mode_ : nullptr; }
    const PowerModeTweaks* PowerMode() const { return power_mode_ ? &*power_mode_ : nullptr; }

    SystemGraphicsTweaksState QuerySystemGraphicsTweaks();
    bool RestoreAllRegistryBackups(size_t* out_reverted = nullptr, size_t* out_total = nullptr);

    struct RebindResult {
        bool rebuilt = false;            // the target GPU or its policy changed (with dry_run: would change)
        bool uma_restore_failed = false; // the old iGPU still reports the overridden VRAM size
    };
    // Aims the tweaks at a new primary GPU: puts back the old iGPU's VRAM override first; the power state is kept.
    RebindResult Rebind(const SystemHardwareProfile& hw, const ApuOptimizationPolicy& policy, bool dry_run = false);

private:
    RegistryPort& registry_;
    PowerPort* power_ = nullptr;
    GpuProfile gpu_;
    ApuOptimizationPolicy policy_;
    std::optional<AmdDriverTweaks> amd_driver_;
    KernelSchedulerTweaks kernel_;
    MmcssTweaks mmcss_;
    DisplayTweaks display_;
    DxgiFlipTweaks dxgi_;
    std::optional<PowerModeTweaks> power_mode_;
};

} // namespace vegaflow
