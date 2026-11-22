#include "windows/power.hpp"
#include "windows/power_settings.hpp"

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
        const DWORD ac_st = fns.read_ac(nullptr, activePolicy, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, &ac_v);
        const DWORD dc_st = fns.read_dc(nullptr, activePolicy, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, &dc_v);
        if (ac_st == ERROR_FILE_NOT_FOUND && dc_st == ERROR_FILE_NOT_FOUND) {
            // The plan has no such setting: it is read as absent and left alone, the other settings still count.
            out_values.ac[i] = kApuPowerSettingAbsent;
            out_values.dc[i] = kApuPowerSettingAbsent;
        } else if (ac_st != ERROR_SUCCESS || dc_st != ERROR_SUCCESS) {
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
        if (values.ac[i] == kApuPowerSettingAbsent) continue;  // the plan has no such setting
        fns.write_ac(nullptr, activePolicy, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, values.ac[i]);
        if (fns.write_dc) {
            fns.write_dc(nullptr, activePolicy, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, values.dc[i]);
        }
    }

    const DWORD set_res = fns.set_active(nullptr, activePolicy);
    LocalFree(activePolicy);
    return (set_res == ERROR_SUCCESS);
}

bool WindowsPowerPort::ApplyApuPlan(const std::string& scheme_guid, const ApuPowerPlanValues& values) {
    if (values.ac.size() != kApuPowerSettingCount || values.dc.size() != kApuPowerSettingCount || scheme_guid.empty()) {
        return false;
    }
    const auto& fns = LoadPowrProf();
    if (!fns.loaded || !fns.get_active || !fns.set_active || !fns.write_ac) {
        return false;
    }
    GUID scheme{};
    const std::wstring wguid(scheme_guid.begin(), scheme_guid.end());  // the GUID text is ASCII
    if (FAILED(CLSIDFromString(wguid.c_str(), &scheme))) {
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < kApuPowerSettingCount; ++i) {
        if (values.ac[i] == kApuPowerSettingAbsent) continue;  // the plan has no such setting
        ok = fns.write_ac(nullptr, &scheme, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, values.ac[i]) == ERROR_SUCCESS && ok;
        if (fns.write_dc) {
            ok = fns.write_dc(nullptr, &scheme, &kApuPowerSettings[i].subgroup, &kApuPowerSettings[i].setting, values.dc[i]) == ERROR_SUCCESS && ok;
        }
    }
    GUID* active = nullptr;
    if (fns.get_active(nullptr, &active) == ERROR_SUCCESS && active) {
        if (IsEqualGUID(*active, scheme)) ok = fns.set_active(nullptr, active) == ERROR_SUCCESS && ok;
        LocalFree(active);
    }
    return ok;
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
    fns.read_ac(nullptr, activePolicy, &kApuPowerSettings[0].subgroup, &kApuPowerSettings[0].setting, &aspm_val);  // ASPM
    LocalFree(activePolicy);
    return (aspm_val == 0);
}

} // namespace vegaflow
