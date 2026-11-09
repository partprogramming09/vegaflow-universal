#include "optimize/graphics/graphics_internal.hpp"
#include "detect/hardware_profiler.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

// The seven power plan settings written by SetApuPowerMode, in this order.
struct ApuPowerSetting {
    const GUID* subgroup;
    const GUID* setting;
};
constexpr size_t kApuPowerSettingCount = 7;
const ApuPowerSetting kApuPowerSettings[kApuPowerSettingCount] = {
    {&GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_PERF_BOOST_MODE},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_CORE_PARKING_MIN_CORES},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX_ALT},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM},
    {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_ENERGY_PERF_PREFERENCE},
};

// APU mode state in this process: whether it is applied and the previous values to restore them.
struct ApuPowerState {
    std::mutex mutex;
    bool applied = false;
    bool saved = false;
    DWORD prev_ac[kApuPowerSettingCount] = {};
    DWORD prev_dc[kApuPowerSettingCount] = {};
};
ApuPowerState g_apu_power;

} // namespace

bool GraphicsOptimizer::SetApuPowerMode(ApuPowerMode mode, bool dry_run) {
    if (dry_run) {
        return true;
    }

    static HMODULE s_hPowrProf = LoadLibraryW(L"powrprof.dll");
    if (!s_hPowrProf) {
        return false;
    }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    static auto pfnGetActiveScheme = reinterpret_cast<pfnPowerGetActiveScheme>(GetProcAddress(s_hPowrProf, "PowerGetActiveScheme"));
    static auto pfnSetActiveScheme = reinterpret_cast<pfnPowerSetActiveScheme>(GetProcAddress(s_hPowrProf, "PowerSetActiveScheme"));
    static auto pfnWriteACValueIndex = reinterpret_cast<pfnPowerWriteACValueIndex>(GetProcAddress(s_hPowrProf, "PowerWriteACValueIndex"));
    static auto pfnWriteDCValueIndex = reinterpret_cast<pfnPowerWriteDCValueIndex>(GetProcAddress(s_hPowrProf, "PowerWriteDCValueIndex"));
    static auto pfnReadACValueIndex = reinterpret_cast<pfnPowerReadACValueIndex>(GetProcAddress(s_hPowrProf, "PowerReadACValueIndex"));
    static auto pfnReadDCValueIndex = reinterpret_cast<pfnPowerReadDCValueIndex>(GetProcAddress(s_hPowrProf, "PowerReadDCValueIndex"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (!pfnGetActiveScheme || !pfnSetActiveScheme || !pfnWriteACValueIndex) {
        return false;
    }

    GUID* activePolicy = nullptr;
    if (pfnGetActiveScheme(nullptr, &activePolicy) != ERROR_SUCCESS || !activePolicy) {
        return false;
    }

    DWORD boost_val = 2;
    DWORD cpu_max_freq = 0;
    DWORD aspm_val = 1;
    DWORD core_parking_val = 50;
    DWORD cpu_throttle_max = 100;
    DWORD epp_val = 50;

    if (mode == ApuPowerMode::GpuPrioritySustained) {
        const auto policy = HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware());
        boost_val = 4;        // Efficient Aggressive (lowers CPU voltage and avoids unnecessary heat spikes)
        cpu_max_freq = policy.cpu_stapm_cap_mhz;  // 0 = 100% unlocked (Smart Uncapped Boost)
        epp_val = 25;         // Efficient high performance SMU, prioritizing immediate response in game threads and GPU at 100%
        aspm_val = 0;         // PCIe ASPM disabled
        core_parking_val = 50; // Factory default: 100 lowers D3D11 drawstress FPS by 21% (126.2 to 99.4, docs/mediciones/2026-11-ab-daemon.md)
        cpu_throttle_max = 100;
    } else { // ApuPowerMode::Balanced
        boost_val = 2;        // Default: standard aggressive Boost (4.0 GHz)
        cpu_max_freq = 0;     // Unlocked
        epp_val = 50;         // standard Windows balanced EPP
        aspm_val = 1;         // PCIe ASPM enabled
        core_parking_val = 50;
        cpu_throttle_max = 100;
    }

    // Values to write, in the order of kApuPowerSettings.
    const DWORD target[kApuPowerSettingCount] = {aspm_val, boost_val, core_parking_val, cpu_max_freq, cpu_max_freq, cpu_throttle_max, epp_val};
    DWORD ac_values[kApuPowerSettingCount];
    DWORD dc_values[kApuPowerSettingCount];
    std::copy(std::begin(target), std::end(target), std::begin(ac_values));
    std::copy(std::begin(target), std::end(target), std::begin(dc_values));

    std::lock_guard<std::mutex> state_lock(g_apu_power.mutex);
    if (mode == ApuPowerMode::GpuPrioritySustained) {
        if (!g_apu_power.saved && pfnReadACValueIndex && pfnReadDCValueIndex) {
            DWORD ac[kApuPowerSettingCount];
            DWORD dc[kApuPowerSettingCount];
            bool read_ok = true;
            bool already_applied = true;
            for (size_t i = 0; i < kApuPowerSettingCount && read_ok; ++i) {
                const ApuPowerSetting& s = kApuPowerSettings[i];
                if (pfnReadACValueIndex(nullptr, activePolicy, s.subgroup, s.setting, &ac[i]) != ERROR_SUCCESS ||
                    pfnReadDCValueIndex(nullptr, activePolicy, s.subgroup, s.setting, &dc[i]) != ERROR_SUCCESS) {
                    read_ok = false;
                } else if (ac[i] != target[i] || dc[i] != target[i]) {
                    already_applied = false;
                }
            }
            // If the profile was already applied (previous shutdown without restoring) those values are not a valid previous state.
            if (read_ok && !already_applied) {
                std::copy(std::begin(ac), std::end(ac), std::begin(g_apu_power.prev_ac));
                std::copy(std::begin(dc), std::end(dc), std::begin(g_apu_power.prev_dc));
                g_apu_power.saved = true;
            }
        }
    } else if (g_apu_power.saved) {
        std::copy(std::begin(g_apu_power.prev_ac), std::end(g_apu_power.prev_ac), std::begin(ac_values));
        std::copy(std::begin(g_apu_power.prev_dc), std::end(g_apu_power.prev_dc), std::begin(dc_values));
        g_apu_power.saved = false;
    }

    for (size_t i = 0; i < kApuPowerSettingCount; ++i) {
        const ApuPowerSetting& s = kApuPowerSettings[i];
        pfnWriteACValueIndex(nullptr, activePolicy, s.subgroup, s.setting, ac_values[i]);
        if (pfnWriteDCValueIndex) {
            pfnWriteDCValueIndex(nullptr, activePolicy, s.subgroup, s.setting, dc_values[i]);
        }
    }

    DWORD set_res = pfnSetActiveScheme(nullptr, activePolicy);
    LocalFree(activePolicy);

    const bool ok = (set_res == ERROR_SUCCESS);
    if (ok) {
        g_apu_power.applied = (mode == ApuPowerMode::GpuPrioritySustained);
    }
    return ok;
}

bool GraphicsOptimizer::IsApuPowerModeApplied() {
    std::lock_guard<std::mutex> lock(g_apu_power.mutex);
    return g_apu_power.applied;
}

bool GraphicsOptimizer::SetApuLaptopPowerProfile(bool optimize, bool dry_run) {
    return SetApuPowerMode(optimize ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced, dry_run);
}

} // namespace vegaflow
