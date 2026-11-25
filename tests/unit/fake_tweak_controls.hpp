#pragma once

// In-memory TweakControls: the "system" is a SystemGraphicsTweaksState that the switches change, and every write is logged
// with its dry_run flag, so SettingsActions can be tested on any host without a registry or power plan.

#include <string>
#include <vector>

#include "ports/tweak_controls.hpp"

namespace vftest {

class FakeTweakControls final : public vegaflow::TweakControls {
public:
    vegaflow::SystemGraphicsTweaksState state{};
    bool amd = true;
    bool power = true;
    bool writes_succeed = true;
    size_t backups_reverted = 3;
    size_t backups_total = 5;
    std::vector<std::string> calls; // "Name(args)" in call order
    bool any_dry_run = false;       // true if a write was ever asked as a dry run

    vegaflow::SystemGraphicsTweaksState QuerySystemGraphicsTweaks() override { return state; }
    bool HasAmdDriver() const override { return amd; }
    bool HasPowerMode() const override { return power; }

    bool SetMpoDisabled(bool disable, bool dry_run) override { return Write("Mpo", disable, dry_run, [&] { state.mpo_disabled = disable; }); }
    bool SetGameDvrCaptureDisabled(bool disable, bool dry_run, bool user_off) override {
        return Write("GameDvr", disable, dry_run, [&] { state.gamedvr_disabled = disable; }, user_off);
    }
    bool SetFullscreenOptimizationsDisabled(bool disable, bool dry_run) override { return Write("Fse", disable, dry_run, [&] { state.fse_disabled = disable; }); }
    bool SetMemoryCompressionEnabled(bool enable, bool dry_run) override { return Write("MemCompression", enable, dry_run, [&] { state.memory_compression_enabled = enable; }); }
    bool RevertMemoryCompressionChange(bool dry_run) override { return Write("RevertMemCompression", true, dry_run, [] {}); }
    bool SetDynamicPrioritySeparation(uint32_t value, bool dry_run, bool user_off) override {
        return Write("Priority" + std::to_string(value), true, dry_run, [&] { state.win32_priority_separation = value; }, user_off);
    }
    bool SetKernelMemoryPagingShield(bool enable, bool dry_run, bool user_off) override {
        return Write("PagingShield", enable, dry_run, [&] { state.kernel_paging_shield_active = enable; }, user_off);
    }
    bool SetGamingProfileOptimized(bool optimize, bool dry_run, bool user_off) override {
        return Write("Mmcss", optimize, dry_run, [&] { state.mmcss_gaming_optimized = optimize; }, user_off);
    }
    bool SetShaderCacheForced(bool force_on, const vegaflow::ApuOptimizationPolicy&, bool dry_run) override {
        return Write("ShaderCache", force_on, dry_run, [&] { state.amd_shader_cache_forced = force_on; });
    }
    bool ApplyVramReportingOverride(uint32_t target_mb, bool dry_run) override {
        return Write("VramOverride" + std::to_string(target_mb), true, dry_run, [&] { state.amd_vram_override_active = true; });
    }
    bool RestoreVramReporting(bool dry_run) override { return Write("VramRestore", true, dry_run, [&] { state.amd_vram_override_active = false; }); }
    bool SetApuPowerMode(vegaflow::ApuPowerMode mode, const vegaflow::ApuOptimizationPolicy&, bool dry_run) override {
        return Write("PowerMode", mode == vegaflow::ApuPowerMode::GpuPrioritySustained, dry_run,
                     [&] { state.apu_power_optimized = mode == vegaflow::ApuPowerMode::GpuPrioritySustained; });
    }
    bool SetApuLaptopPowerProfile(bool optimize, const vegaflow::ApuOptimizationPolicy&, bool dry_run) override {
        return Write("LaptopProfile", optimize, dry_run, [] {});
    }
    bool RestoreAllRegistryBackups(size_t* out_reverted, size_t* out_total) override {
        calls.push_back("RestoreAllBackups");
        if (out_reverted) *out_reverted = backups_reverted;
        if (out_total) *out_total = backups_total;
        return true;
    }

private:
    template <typename Apply>
    bool Write(const std::string& name, bool arg, bool dry_run, Apply apply, bool user_off = false) {
        calls.push_back(name + "(" + (arg ? "1" : "0") + (user_off ? ",user_off" : "") + ")");
        if (dry_run) any_dry_run = true;
        if (!writes_succeed) return false;
        if (!dry_run) apply();
        return true;
    }
};

} // namespace vftest
