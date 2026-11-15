// Unit tests: GraphicsOptimizer family gating, ven_1002 guard, PowerMode policy gating,
// and MMCSS GPU Priority removal + legacy backup cleanup via FakeRegistryPort / FakePowerPort.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "fake_registry_power.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

void Test57() {
    const std::wstring kDisplay0000 =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000";
    const std::wstring kDisplay0001 =
        L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0001";
    const std::wstring kMmcssGames =
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";

    // 1. Intel & NVIDIA hardware profiles: AmdDriver() is nullptr and zero AMD registry keys are touched.
    auto check_non_amd = [&](vegaflow::GpuVendor vendor) {
        FakeRegistryPort reg;
        FakePowerPort power;
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"MatchingDeviceId",
                      vegaflow::RegValue::MakeString(L"pci\\ven_8086&dev_9a49"));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000 + L"\\UMD", L"ShaderCache",
                      vegaflow::RegValue::MakeBinary({ 0x31, 0x00 }));

        vegaflow::SystemHardwareProfile hw{};
        hw.primary_gpu.vendor = vendor;
        vegaflow::ApuOptimizationPolicy policy{};
        policy.block_driver_tweaks = false;
        policy.force_high_perf_power_plan = false;

        vegaflow::GraphicsOptimizer gfx(hw, policy, reg, &power);
        const bool no_amd = (gfx.AmdDriver() == nullptr);
        const bool no_power = (gfx.PowerMode() == nullptr);
        const auto state = gfx.QuerySystemGraphicsTweaks();
        const bool mpo_off_default = !state.mpo_disabled;
        const bool no_amd_reads = !reg.TouchedSubkeyContaining(L"4d36e968-e325-11ce-bfc1-08002be10318") &&
                                  reg.writes.empty() && power.apply_count == 0;
        return no_amd && no_power && mpo_off_default && no_amd_reads;
    };
    const bool intel_ok = check_non_amd(vegaflow::GpuVendor::Intel);
    const bool nvidia_ok = check_non_amd(vegaflow::GpuVendor::NVIDIA);

    // 2. AMD profile with block_driver_tweaks=true also suppresses AmdDriver().
    bool blocked_amd_ok = false;
    {
        FakeRegistryPort reg;
        vegaflow::SystemHardwareProfile hw{};
        hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        vegaflow::ApuOptimizationPolicy policy{};
        policy.block_driver_tweaks = true;
        vegaflow::GraphicsOptimizer gfx(hw, policy, reg, nullptr);
        blocked_amd_ok = (gfx.AmdDriver() == nullptr) && (gfx.PowerMode() == nullptr);
    }

    // 3. AMD profile with block_driver_tweaks=false: AmdDriver() exists and ven_1002 guard skips non-AMD adapters.
    bool amd_ven1002_ok = false;
    {
        FakeRegistryPort reg;
        FakePowerPort power;
        // 0000 is Intel (should NOT be touched even though it has UMD/ShaderCache)
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000, L"MatchingDeviceId",
                      vegaflow::RegValue::MakeString(L"pci\\ven_8086&dev_46a6"));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0000 + L"\\UMD", L"ShaderCache",
                      vegaflow::RegValue::MakeBinary({ 0x31, 0x00 }));
        // 0001 is AMD (ven_1002)
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0001, L"MatchingDeviceId",
                      vegaflow::RegValue::MakeString(L"pci\\ven_1002&dev_1636"));
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kDisplay0001 + L"\\UMD", L"ShaderCache",
                      vegaflow::RegValue::MakeBinary({ 0x31, 0x00 }));

        vegaflow::SystemHardwareProfile hw{};
        hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        vegaflow::ApuOptimizationPolicy policy{};
        policy.block_driver_tweaks = false;
        policy.force_high_perf_power_plan = true;
        policy.amd_tessellation_cap = 16;

        vegaflow::GraphicsOptimizer gfx(hw, policy, reg, &power);
        if (gfx.AmdDriver() && gfx.PowerMode()) {
            const bool sc_ok = gfx.AmdDriver()->SetShaderCacheForced(true, policy, false);
            const bool boost_ok = gfx.AmdDriver()->SetDx11UmaDriverBoost(true, policy, false);

            bool touched_intel_0000 = false;
            bool touched_amd_0001 = false;
            for (const auto& w : reg.writes) {
                const std::wstring sk = FakeRegistryPort::Lower(w.subkey);
                if (sk.find(L"\\0000") != std::wstring::npos) touched_intel_0000 = true;
                if (sk.find(L"\\0001") != std::wstring::npos) touched_amd_0001 = true;
            }
            amd_ven1002_ok = sc_ok && boost_ok && !touched_intel_0000 && touched_amd_0001;
        }
    }

    // 4. PowerModeTweaks saves and restores previous AC/DC values per instance.
    bool power_roundtrip_ok = false;
    {
        FakeRegistryPort reg;
        FakePowerPort power;
        power.current = { { 1, 2, 50, 3800, 3800, 100, 45 }, { 1, 2, 50, 3800, 3800, 100, 45 } };
        const auto original = power.current;

        vegaflow::SystemHardwareProfile hw{};
        hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
        vegaflow::ApuOptimizationPolicy policy{};
        policy.force_high_perf_power_plan = true;
        policy.cpu_stapm_cap_mhz = 3300;

        vegaflow::GraphicsOptimizer gfx(hw, policy, reg, &power);
        if (gfx.PowerMode()) {
            const bool set_gpu = gfx.PowerMode()->SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, policy, false);
            const bool applied = gfx.PowerMode()->IsApuPowerModeApplied() && power.current.ac[0] == 0 && power.current.ac[6] == 25;
            const bool set_bal = gfx.PowerMode()->SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, policy, false);
            const bool restored = !gfx.PowerMode()->IsApuPowerModeApplied() &&
                                  power.current.ac == original.ac && power.current.dc == original.dc;
            power_roundtrip_ok = set_gpu && applied && set_bal && restored;
        }
    }

    // 5. MMCSS never touches GPU Priority AND legacy GPU Priority backup is cleaned up on restore.
    bool mmcss_no_gpu_prio_ok = false;
    bool legacy_cleanup_ok = false;
    {
        FakeRegistryPort reg;
        vegaflow::SystemHardwareProfile hw{};
        hw.primary_gpu.vendor = vegaflow::GpuVendor::Intel;
        vegaflow::ApuOptimizationPolicy policy{};
        vegaflow::GraphicsOptimizer gfx(hw, policy, reg, nullptr);

        const bool opt_ok = gfx.Mmcss().SetGamingProfileOptimized(true, false);
        const bool task_ok = gfx.Mmcss().ApplyTaskTweaks(false);
        std::string cat, sfio;
        uint32_t clk = 0;
        const bool query_ok = gfx.Mmcss().QueryTaskTweaks(cat, sfio, clk);
        const bool rest_ok = gfx.Mmcss().RestoreTaskTweaks(false);
        mmcss_no_gpu_prio_ok = opt_ok && task_ok && query_ok && rest_ok && !reg.TouchedName(L"gpu priority");

        // Legacy backup cleanup via RestoreTaskTweaks AND via RestoreAllRegistryBackups
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority",
                      vegaflow::RegValue::MakeDword(8));
        reg.SeedLegacyBackup(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority",
                             vegaflow::RegValue::MakeDword(2));
        gfx.Mmcss().SetGamingProfileOptimized(false, false);
        vegaflow::RegValue v1{};
        const bool restored_via_mmcss = !reg.HasBackup(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority") &&
                                        reg.ReadValue(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority", v1) &&
                                        v1.dword == 2;

        // Also verify deletion when original had no GPU Priority value
        reg.SeedValue(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority",
                      vegaflow::RegValue::MakeDword(8));
        reg.SeedLegacyBackup(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority", std::nullopt);
        const bool restore_all_ok = gfx.RestoreAllRegistryBackups();
        const bool deleted_via_all = restore_all_ok &&
                                     !reg.HasBackup(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority") &&
                                     !reg.HasValue(vegaflow::RegHive::LocalMachine, kMmcssGames, L"GPU Priority");

        legacy_cleanup_ok = restored_via_mmcss && deleted_via_all;
    }

    const bool ok = intel_ok && nvidia_ok && blocked_amd_ok && amd_ven1002_ok &&
                    power_roundtrip_ok && mmcss_no_gpu_prio_ok && legacy_cleanup_ok;
    const std::string details =
        "Intel gate: " + std::string(intel_ok ? "PASS" : "FAIL") +
        ", NVIDIA gate: " + std::string(nvidia_ok ? "PASS" : "FAIL") +
        ", Blocked AMD: " + std::string(blocked_amd_ok ? "PASS" : "FAIL") +
        ", AMD ven_1002: " + std::string(amd_ven1002_ok ? "PASS" : "FAIL") +
        ", PowerMode roundtrip: " + std::string(power_roundtrip_ok ? "PASS" : "FAIL") +
        ", MMCSS no GPU Priority: " + std::string(mmcss_no_gpu_prio_ok ? "PASS" : "FAIL") +
        ", Legacy cleanup: " + std::string(legacy_cleanup_ok ? "PASS" : "FAIL");
    RecordResult(ok, "Test 57: GraphicsOptimizer family gating, ven_1002 guard & MMCSS GPU Priority cleanup", details);
}

} // namespace vftest
