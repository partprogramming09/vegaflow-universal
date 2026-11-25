#pragma once
// The system switches the "Ajustes" screen turns on and off, as one interface: SettingsActions decides what each switch
// means and what the status bar says; the optimizer behind this port does the registry and power-plan writes.
// (F4.3: session/ no longer includes optimize/; the optimizer implements this and tests give a fake.)

#include <cstddef>
#include <cstdint>

#include "model/hardware.hpp"
#include "model/tweaks.hpp"

namespace vegaflow {

class TweakControls {
public:
    virtual ~TweakControls() = default;

    // What the system holds right now (the switches' current position).
    virtual SystemGraphicsTweaksState QuerySystemGraphicsTweaks() = 0;

    // The AMD driver and the power-mode families exist only for some GPUs and policies.
    virtual bool HasAmdDriver() const = 0;
    virtual bool HasPowerMode() const = 0;

    // Display and DWM.
    virtual bool SetMpoDisabled(bool disable, bool dry_run) = 0;
    virtual bool SetGameDvrCaptureDisabled(bool disable, bool dry_run, bool user_off) = 0;
    virtual bool SetFullscreenOptimizationsDisabled(bool disable, bool dry_run) = 0;

    // Kernel scheduler and memory.
    virtual bool SetMemoryCompressionEnabled(bool enable, bool dry_run) = 0;
    virtual bool RevertMemoryCompressionChange(bool dry_run) = 0;
    virtual bool SetDynamicPrioritySeparation(uint32_t value, bool dry_run, bool user_off) = 0;
    virtual bool SetKernelMemoryPagingShield(bool enable, bool dry_run, bool user_off) = 0;

    // MMCSS.
    virtual bool SetGamingProfileOptimized(bool optimize, bool dry_run, bool user_off) = 0;

    // AMD driver (only when HasAmdDriver()).
    virtual bool SetShaderCacheForced(bool force_on, const ApuOptimizationPolicy& policy, bool dry_run) = 0;
    virtual bool ApplyVramReportingOverride(uint32_t target_mb, bool dry_run) = 0;
    virtual bool RestoreVramReporting(bool dry_run) = 0;

    // Power plan (only when HasPowerMode()).
    virtual bool SetApuPowerMode(ApuPowerMode mode, const ApuOptimizationPolicy& policy, bool dry_run) = 0;
    virtual bool SetApuLaptopPowerProfile(bool optimize, const ApuOptimizationPolicy& policy, bool dry_run) = 0;

    // "Volver todo al valor de Windows": puts back every registry value VegaFlow saved a copy of.
    virtual bool RestoreAllRegistryBackups(size_t* out_reverted, size_t* out_total) = 0;
};

} // namespace vegaflow
