#include "windows/power.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

namespace vegaflow {

namespace {

typedef DWORD (WINAPI *pfnPowerGetActiveScheme)(HKEY UserRootPowerKey, GUID **ActivePolicyGuid);
typedef DWORD (WINAPI *pfnPowerSetActiveScheme)(HKEY UserRootPowerKey, const GUID *SchemeGuid);
typedef DWORD (WINAPI *pfnPowerWriteACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD AcValueIndex);
typedef DWORD (WINAPI *pfnPowerWriteDCValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD DcValueIndex);
typedef DWORD (WINAPI *pfnPowerReadACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, LPDWORD AcValueIndex);
typedef DWORD (WINAPI *pfnPowerReadDCValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, LPDWORD DcValueIndex);

const GUID GUID_PCIEXPRESS_SETTINGS_SUBGROUP = {0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
const GUID GUID_PCIEXPRESS_ASPM = {0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
const GUID GUID_PROCESSOR_SETTINGS_SUBGROUP = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
const GUID GUID_PROCESSOR_PERF_BOOST_MODE = {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
const GUID GUID_PROCESSOR_CORE_PARKING_MIN_CORES = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
const GUID GUID_PROCESSOR_FREQUENCY_MAX = {0x75b64e17, 0x1334, 0x44c1, {0x8c, 0xbe, 0xec, 0x61, 0x99, 0xea, 0xe7, 0x25}};
const GUID GUID_PROCESSOR_FREQUENCY_MAX_ALT = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}};
const GUID GUID_PROCESSOR_THROTTLE_MAXIMUM = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
const GUID GUID_PROCESSOR_ENERGY_PERF_PREFERENCE = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};

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

struct PowrProfFns {
    pfnPowerGetActiveScheme get_active = nullptr;
    pfnPowerSetActiveScheme set_active = nullptr;
    pfnPowerWriteACValueIndex write_ac = nullptr;
    pfnPowerWriteDCValueIndex write_dc = nullptr;
    pfnPowerReadACValueIndex read_ac = nullptr;
    pfnPowerReadDCValueIndex read_dc = nullptr;
    bool loaded = false;
};

const PowrProfFns& LoadPowrProf() {
    static PowrProfFns fns = []() {
        PowrProfFns r;
        HMODULE h = LoadLibraryW(L"powrprof.dll");
        if (!h) return r;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        r.get_active = reinterpret_cast<pfnPowerGetActiveScheme>(GetProcAddress(h, "PowerGetActiveScheme"));
        r.set_active = reinterpret_cast<pfnPowerSetActiveScheme>(GetProcAddress(h, "PowerSetActiveScheme"));
        r.write_ac = reinterpret_cast<pfnPowerWriteACValueIndex>(GetProcAddress(h, "PowerWriteACValueIndex"));
        r.write_dc = reinterpret_cast<pfnPowerWriteDCValueIndex>(GetProcAddress(h, "PowerWriteDCValueIndex"));
        r.read_ac = reinterpret_cast<pfnPowerReadACValueIndex>(GetProcAddress(h, "PowerReadACValueIndex"));
        r.read_dc = reinterpret_cast<pfnPowerReadDCValueIndex>(GetProcAddress(h, "PowerReadDCValueIndex"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        r.loaded = (r.get_active != nullptr);
        return r;
    }();
    return fns;
}

std::string GuidToUtf8(const GUID& g) {
    wchar_t buf[64]{};
    if (StringFromGUID2(g, buf, 64) <= 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

} // namespace

bool WindowsPowerPort::ReadActiveApuPlan(std::string& out_scheme_guid, ApuPowerPlanValues& out_values) {
    out_scheme_guid.clear();
    out_values.ac.clear();
    out_values.dc.clear();

    const auto& fns = LoadPowrProf();
    if (!fns.loaded || !fns.get_active || !fns.read_ac || !fns.read_dc) {
        return false;
    }

    GUID* activePolicy = nullptr;
    if (fns.get_active(nullptr, &activePolicy) != ERROR_SUCCESS || !activePolicy) {
        return false;
    }

    out_scheme_guid = GuidToUtf8(*activePolicy);
    out_values.ac.resize(kApuPowerSettingCount);
    out_values.dc.resize(kApuPowerSettingCount);

    bool read_ok = true;
    for (size_t i = 0; i < kApuPowerSettingCount && read_ok; ++i) {
        DWORD ac_v = 0;
        DWORD dc_v = 0;
        if (fns.read_ac(nullptr, activePolicy, kApuPowerSettings[i].subgroup, kApuPowerSettings[i].setting, &ac_v) != ERROR_SUCCESS ||
            fns.read_dc(nullptr, activePolicy, kApuPowerSettings[i].subgroup, kApuPowerSettings[i].setting, &dc_v) != ERROR_SUCCESS) {
            read_ok = false;
        } else {
            out_values.ac[i] = ac_v;
            out_values.dc[i] = dc_v;
        }
    }

    LocalFree(activePolicy);
    if (!read_ok) {
        out_values.ac.clear();
        out_values.dc.clear();
    }
    return read_ok;
}

bool WindowsPowerPort::ApplyActiveApuPlan(const ApuPowerPlanValues& values) {
    if (values.ac.size() != kApuPowerSettingCount || values.dc.size() != kApuPowerSettingCount) {
        return false;
    }

    const auto& fns = LoadPowrProf();
    if (!fns.loaded || !fns.get_active || !fns.set_active || !fns.write_ac) {
        return false;
    }

    GUID* activePolicy = nullptr;
    if (fns.get_active(nullptr, &activePolicy) != ERROR_SUCCESS || !activePolicy) {
        return false;
    }

    for (size_t i = 0; i < kApuPowerSettingCount; ++i) {
        fns.write_ac(nullptr, activePolicy, kApuPowerSettings[i].subgroup, kApuPowerSettings[i].setting, values.ac[i]);
        if (fns.write_dc) {
            fns.write_dc(nullptr, activePolicy, kApuPowerSettings[i].subgroup, kApuPowerSettings[i].setting, values.dc[i]);
        }
    }

    const DWORD set_res = fns.set_active(nullptr, activePolicy);
    LocalFree(activePolicy);
    return (set_res == ERROR_SUCCESS);
}

bool WindowsPowerPort::QueryApuPowerOptimized() {
    const auto& fns = LoadPowrProf();
    if (!fns.loaded || !fns.get_active || !fns.read_ac) {
        return false;
    }

    GUID* activePolicy = nullptr;
    if (fns.get_active(nullptr, &activePolicy) != ERROR_SUCCESS || !activePolicy) {
        return false;
    }

    DWORD aspm_val = 1;
    fns.read_ac(nullptr, activePolicy, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM, &aspm_val);
    LocalFree(activePolicy);
    return (aspm_val == 0);
}

} // namespace vegaflow
