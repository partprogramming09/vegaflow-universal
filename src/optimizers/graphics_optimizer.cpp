#include "graphics_optimizer.hpp"
#include "hardware_profiler.hpp"

#if __has_include("nt_defs.hpp")
#include "nt_defs.hpp"
#else
#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

extern "C" {
    NTSTATUS NTAPI NtQueryTimerResolution(
        PULONG MinimumResolution,
        PULONG MaximumResolution,
        PULONG CurrentResolution
    );

    NTSTATUS NTAPI NtSetTimerResolution(
        ULONG DesiredResolution,
        BOOLEAN SetResolution,
        PULONG CurrentResolution
    );
}
#endif

#include <atomic>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <tlhelp32.h>
#include <winioctl.h>
#include <vector>

namespace vegaflow {

namespace {

std::atomic<bool> s_high_res_active{false};

typedef LONG(NTAPI* pfnNtQueryTimerResolution)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);
typedef LONG(NTAPI* pfnNtSetTimerResolution)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

NTSTATUS QueryTimerRes(PULONG min_res, PULONG max_res, PULONG cur_res) {
    static pfnNtQueryTimerResolution pfn = reinterpret_cast<pfnNtQueryTimerResolution>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
    if (pfn) {
        return pfn(min_res, max_res, cur_res);
    }
    return -1;
}

NTSTATUS SetTimerRes(ULONG desired, BOOLEAN set, PULONG cur_res) {
    static pfnNtSetTimerResolution pfn = reinterpret_cast<pfnNtSetTimerResolution>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution"));
    if (pfn) {
        return pfn(desired, set, cur_res);
    }
    return -1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

typedef DWORD (WINAPI *pfnPowerGetActiveScheme)(HKEY UserRootPowerKey, GUID **ActivePolicyGuid);
typedef DWORD (WINAPI *pfnPowerSetActiveScheme)(HKEY UserRootPowerKey, const GUID *SchemeGuid);
typedef DWORD (WINAPI *pfnPowerWriteACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD AcValueIndex);
typedef DWORD (WINAPI *pfnPowerWriteDCValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD DcValueIndex);
typedef DWORD (WINAPI *pfnPowerReadACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, LPDWORD AcValueIndex);

static const GUID GUID_PCIEXPRESS_SETTINGS_SUBGROUP = {0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
static const GUID GUID_PCIEXPRESS_ASPM = {0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
static const GUID GUID_PROCESSOR_SETTINGS_SUBGROUP = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
static const GUID GUID_PROCESSOR_PERF_BOOST_MODE = {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
static const GUID GUID_PROCESSOR_CORE_PARKING_MIN_CORES = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
static const GUID GUID_PROCESSOR_FREQUENCY_MAX = {0x75b64e17, 0x1334, 0x44c1, {0x8c, 0xbe, 0xec, 0x61, 0x99, 0xea, 0xe7, 0x25}};
static const GUID GUID_PROCESSOR_FREQUENCY_MAX_ALT = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}};
static const GUID GUID_PROCESSOR_THROTTLE_MAXIMUM = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
static const GUID GUID_PROCESSOR_ENERGY_PERF_PREFERENCE = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};

std::wstring ToLower(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t ch : str) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

uint32_t ParseTessellationValue(const BYTE* buf, DWORD sz, DWORD type) {
    if (sz == 0 || !buf) return 0;
    if (type == REG_DWORD && sz >= sizeof(DWORD)) {
        return *reinterpret_cast<const DWORD*>(buf);
    }
    if (sz >= 4 && buf[1] == 0x00 && (sz % 2 == 0)) {
        std::wstring ws(reinterpret_cast<const wchar_t*>(buf), sz / sizeof(wchar_t));
        while (!ws.empty() && ws.back() == L'\0') ws.pop_back();
        try {
            return static_cast<uint32_t>(std::stoul(ws));
        } catch (...) {}
    }
    std::string s(reinterpret_cast<const char*>(buf), sz);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    try {
        return static_cast<uint32_t>(std::stoul(s));
    } catch (...) {}
    return 0;
}

} // anonymous namespace

TimerResolutionState GraphicsOptimizer::QueryTimerResolution() {
    TimerResolutionState state{};
    ULONG min_res = 0;
    ULONG max_res = 0;
    ULONG cur_res = 0;

    NTSTATUS status = QueryTimerRes(&min_res, &max_res, &cur_res);
    if (NT_SUCCESS(status)) {
        state.minimum_ns100 = min_res;
        state.maximum_ns100 = max_res;
        state.current_ns100 = cur_res;
        state.active_high_res = s_high_res_active.load() || (cur_res < min_res) || (cur_res <= 10000);
    }

    return state;
}

bool GraphicsOptimizer::EnableHighPrecisionTimer(ULONG desired_ns100) {
    PROCESS_POWER_THROTTLING_STATE powerThrottling{};
    powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                  PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    powerThrottling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));

    ULONG cur_res = 0;
    NTSTATUS status = SetTimerRes(desired_ns100, TRUE, &cur_res);
    if (NT_SUCCESS(status)) {
        s_high_res_active.store(true);
        return true;
    }
    return false;
}

bool GraphicsOptimizer::RestoreDefaultTimer() {
    ULONG cur_res = 0;
    NTSTATUS status = SetTimerRes(5000, FALSE, &cur_res);
    if (NT_SUCCESS(status)) {
        s_high_res_active.store(false);
        return true;
    }
    return false;
}

bool GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run) {
    return ApplyDxgiFlipModelUpgrade(exe_full_path, dry_run, nullptr);
}

bool GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(
    const std::wstring& exe_full_path,
    bool dry_run,
    std::wstring* out_effective_preference) {
    if (exe_full_path.empty()) {
        return false;
    }

    std::wstring final_value = L"SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;";

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
        0,
        KEY_READ,
        &hKey
    );

    if (status == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data_bytes = 0;
        if (RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, nullptr, &data_bytes) == ERROR_SUCCESS &&
            type == REG_SZ && data_bytes >= sizeof(wchar_t)) {
            std::vector<wchar_t> buffer(data_bytes / sizeof(wchar_t) + 1, 0);
            if (RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &data_bytes) == ERROR_SUCCESS) {
                std::wstring existing = buffer.data();

                auto UpdateOrInsertParam = [](std::wstring& str, const std::wstring& key, const std::wstring& val) {
                    std::wstring lower = ToLower(str);
                    std::wstring target_key = ToLower(key) + L"=";
                    auto pos = lower.find(target_key);
                    std::wstring insertion = key + L"=" + val + L";";
                    if (pos != std::wstring::npos) {
                        auto semi = str.find(L';', pos);
                        if (semi != std::wstring::npos) {
                            str.replace(pos, semi - pos + 1, insertion);
                        } else {
                            str.replace(pos, str.size() - pos, insertion);
                        }
                    } else {
                        if (!str.empty() && str.back() != L';') {
                            str.push_back(L';');
                        }
                        str += insertion;
                    }
                };

                auto RemoveParam = [](std::wstring& str, const std::wstring& key) {
                    std::wstring lower = ToLower(str);
                    std::wstring target_key = ToLower(key) + L"=";
                    auto pos = lower.find(target_key);
                    while (pos != std::wstring::npos) {
                        auto semi = str.find(L';', pos);
                        if (semi != std::wstring::npos) {
                            str.erase(pos, semi - pos + 1);
                        } else {
                            str.erase(pos);
                        }
                        lower = ToLower(str);
                        pos = lower.find(target_key);
                    }
                };

                UpdateOrInsertParam(existing, L"SwapEffectUpgradeEnable", L"1");
                UpdateOrInsertParam(existing, L"GpuPreference", L"2");
                UpdateOrInsertParam(existing, L"AutoHDREnable", L"0");
                RemoveParam(existing, L"AutoFlipModel");
                RemoveParam(existing, L"DirectFlip");

                final_value = existing;
            }
        }
        RegCloseKey(hKey);
    }

    if (out_effective_preference) {
        *out_effective_preference = final_value;
    }

    if (dry_run) {
        return true;
    }

    DWORD disposition = 0;
    status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE,
        nullptr,
        &hKey,
        &disposition
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegSetValueExW(
        hKey,
        exe_full_path.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(final_value.c_str()),
        static_cast<DWORD>((final_value.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    // Write ~ HIGHDPIAWARE to AppCompatFlags\Layers
    HKEY hLayers = nullptr;
    LSTATUS statusLayers = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hLayers,
        &disposition
    );
    if (statusLayers == ERROR_SUCCESS) {
        const std::wstring dpi_flag = L"~ HIGHDPIAWARE";
        RegSetValueExW(
            hLayers,
            exe_full_path.c_str(),
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(dpi_flag.c_str()),
            static_cast<DWORD>((dpi_flag.size() + 1) * sizeof(wchar_t))
        );
        RegCloseKey(hLayers);
    }

    return (status == ERROR_SUCCESS);
}

bool GraphicsOptimizer::ApplyDxgiDirectFlipUpgrade(
    const std::wstring& exe_full_path,
    bool dry_run,
    std::wstring* out_effective_preference) {
    if (exe_full_path.empty()) {
        return false;
    }

    std::wstring final_value = L"SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;AutoFlipModel=1;DirectFlip=1;";

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
        0,
        KEY_READ,
        &hKey
    );

    if (status == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD data_bytes = 0;
        if (RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, nullptr, &data_bytes) == ERROR_SUCCESS &&
            type == REG_SZ && data_bytes >= sizeof(wchar_t)) {
            std::vector<wchar_t> buffer(data_bytes / sizeof(wchar_t) + 1, 0);
            if (RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &data_bytes) == ERROR_SUCCESS) {
                std::wstring existing = buffer.data();

                auto UpdateOrInsertParam = [](std::wstring& str, const std::wstring& key, const std::wstring& val) {
                    std::wstring lower = ToLower(str);
                    std::wstring target_key = ToLower(key) + L"=";
                    auto pos = lower.find(target_key);
                    std::wstring insertion = key + L"=" + val + L";";
                    if (pos != std::wstring::npos) {
                        auto semi = str.find(L';', pos);
                        if (semi != std::wstring::npos) {
                            str.replace(pos, semi - pos + 1, insertion);
                        } else {
                            str.replace(pos, str.size() - pos, insertion);
                        }
                    } else {
                        if (!str.empty() && str.back() != L';') {
                            str.push_back(L';');
                        }
                        str += insertion;
                    }
                };

                UpdateOrInsertParam(existing, L"SwapEffectUpgradeEnable", L"1");
                UpdateOrInsertParam(existing, L"GpuPreference", L"2");
                UpdateOrInsertParam(existing, L"AutoHDREnable", L"0");
                UpdateOrInsertParam(existing, L"AutoFlipModel", L"1");
                UpdateOrInsertParam(existing, L"DirectFlip", L"1");

                final_value = existing;
            }
        }
        RegCloseKey(hKey);
    }

    if (out_effective_preference) {
        *out_effective_preference = final_value;
    }

    if (dry_run) {
        return true;
    }

    DWORD disposition = 0;
    status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE,
        nullptr,
        &hKey,
        &disposition
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegSetValueExW(
        hKey,
        exe_full_path.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(final_value.c_str()),
        static_cast<DWORD>((final_value.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    // Write ~ HIGHDPIAWARE to AppCompatFlags\Layers
    HKEY hLayers = nullptr;
    LSTATUS statusLayers = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hLayers,
        &disposition
    );
    if (statusLayers == ERROR_SUCCESS) {
        const std::wstring dpi_flag = L"~ HIGHDPIAWARE";
        RegSetValueExW(
            hLayers,
            exe_full_path.c_str(),
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(dpi_flag.c_str()),
            static_cast<DWORD>((dpi_flag.size() + 1) * sizeof(wchar_t))
        );
        RegCloseKey(hLayers);
    }

    return (status == ERROR_SUCCESS);
}

bool GraphicsOptimizer::QueryUserGpuPreferences(const std::wstring& exe_full_path, std::wstring& out_prefs) {
    out_prefs.clear();
    if (exe_full_path.empty()) {
        return false;
    }

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
        0,
        KEY_READ,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD data_bytes = 0;
    status = RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, nullptr, &data_bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || data_bytes < sizeof(wchar_t)) {
        RegCloseKey(hKey);
        return false;
    }

    std::vector<wchar_t> buffer(data_bytes / sizeof(wchar_t) + 1, 0);
    status = RegQueryValueExW(hKey, exe_full_path.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &data_bytes);
    RegCloseKey(hKey);

    if (status != ERROR_SUCCESS) {
        return false;
    }

    out_prefs = buffer.data();
    return true;
}

bool GraphicsOptimizer::RemoveDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run) {
    if (exe_full_path.empty()) {
        return false;
    }
    if (dry_run) {
        return true;
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, exe_full_path.c_str());
        RegCloseKey(hKey);
    }

    HKEY hLayers = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", 0, KEY_SET_VALUE, &hLayers) == ERROR_SUCCESS) {
        RegDeleteValueW(hLayers, exe_full_path.c_str());
        RegCloseKey(hLayers);
    }
    return true;
}

bool GraphicsOptimizer::HasDirectFlipUpgrade(const std::wstring& exe_full_path) {
    std::wstring val;
    if (!QueryUserGpuPreferences(exe_full_path, val)) {
        return false;
    }
    std::wstring lower = ToLower(val);
    return (lower.find(L"autoflipmodel=1") != std::wstring::npos &&
            lower.find(L"directflip=1") != std::wstring::npos);
}

bool GraphicsOptimizer::HasDxgiFlipModelUpgrade(const std::wstring& exe_full_path) {
    std::wstring val;
    if (!QueryUserGpuPreferences(exe_full_path, val)) {
        return false;
    }
    std::wstring lower = ToLower(val);
    return (lower.find(L"swapeffectupgradeenable=1") != std::wstring::npos);
}

SystemGraphicsTweaksState GraphicsOptimizer::QuerySystemGraphicsTweaks() {
    SystemGraphicsTweaksState state{};

    // 1. DWM OverlayTestMode (MPO)
    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\Dwm",
        0,
        KEY_READ,
        &hKey
    );

    if (status == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD value = 0;
        DWORD data_size = sizeof(value);
        if (RegQueryValueExW(hKey, L"OverlayTestMode", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &data_size) == ERROR_SUCCESS) {
            if (type == REG_DWORD) {
                state.dwm_overlay_test_mode = value;
                state.mpo_disabled = (value == 5);
            }
        }
        RegCloseKey(hKey);
    }

    // 2. Comprobar proceso MemCompression en el sistema
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                std::wstring proc_name = ToLower(pe.szExeFile);
                if (proc_name.find(L"memcompression") != std::wstring::npos) {
                    state.memory_compression_enabled = true;
                    break;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // 3. Prioridad de CPU (Win32PrioritySeparation)
    HKEY hPrio = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl", 0, KEY_READ, &hPrio) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD value = 0;
        DWORD data_size = sizeof(value);
        if (RegQueryValueExW(hPrio, L"Win32PrioritySeparation", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &data_size) == ERROR_SUCCESS && type == REG_DWORD) {
            state.win32_priority_separation = value;
        }
        RegCloseKey(hPrio);
    }

    // 4. MMCSS Gaming Profile (Tasks\Games -> GPU Priority == 8)
    HKEY hGames = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games", 0, KEY_READ, &hGames) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD value = 0;
        DWORD data_size = sizeof(value);
        if (RegQueryValueExW(hGames, L"GPU Priority", nullptr, &type, reinterpret_cast<LPBYTE>(&value), &data_size) == ERROR_SUCCESS && type == REG_DWORD) {
            if (value == 8) {
                state.mmcss_gaming_optimized = true;
            }
        }
        RegCloseKey(hGames);
    }

    // 5. Inspeccionar HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000..0003
    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ, &hAdapter) == ERROR_SUCCESS) {
            BYTE data[32] = {0};
            DWORD type = 0;
            DWORD data_size = sizeof(data);

            auto check_shader_cache_val = [](BYTE* buf, DWORD sz, DWORD t) -> bool {
                if (t == REG_BINARY && sz >= 2) {
                    return (buf[0] == 0x32 && buf[1] == 0x00);
                } else if (t == REG_SZ && sz >= sizeof(wchar_t)) {
                    auto* ws = reinterpret_cast<wchar_t*>(buf);
                    return (ws[0] == L'2');
                } else if (t == REG_DWORD && sz >= sizeof(DWORD)) {
                    auto* dw = reinterpret_cast<DWORD*>(buf);
                    return (*dw == 2 || *dw == 0x32);
                }
                return false;
            };

            auto check_boost_binary = [](BYTE* buf, DWORD sz, DWORD t) -> bool {
                if (t == REG_BINARY && sz >= 2) {
                    return (buf[0] == 0x31 && buf[1] == 0x00);
                } else if (t == REG_SZ && sz >= sizeof(wchar_t)) {
                    auto* ws = reinterpret_cast<wchar_t*>(buf);
                    return (ws[0] == L'1');
                } else if (t == REG_DWORD && sz >= sizeof(DWORD)) {
                    auto* dw = reinterpret_cast<DWORD*>(buf);
                    return (*dw == 1 || *dw == 0x31);
                }
                return false;
            };

            // Comprobar valor directo en la clave del adaptador
            if (RegQueryValueExW(hAdapter, L"ShaderCache", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                if (check_shader_cache_val(data, data_size, type)) {
                    state.amd_shader_cache_forced = true;
                }
            }

            // Comprobar subclave UMD
            HKEY hUmd = nullptr;
            if (RegOpenKeyExW(hAdapter, L"UMD", 0, KEY_READ, &hUmd) == ERROR_SUCCESS) {
                data_size = sizeof(data);
                if (RegQueryValueExW(hUmd, L"ShaderCache", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                    if (check_shader_cache_val(data, data_size, type)) {
                        state.amd_shader_cache_forced = true;
                    }
                }

                data_size = sizeof(data);
                if (RegQueryValueExW(hUmd, L"SurfaceFormatReplacements", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                    if (check_boost_binary(data, data_size, type)) {
                        state.amd_dx11_uma_boost = true;
                    }
                }

                data_size = sizeof(data);
                if (RegQueryValueExW(hUmd, L"FlipQueueSize", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                    if (check_boost_binary(data, data_size, type) ||
                        (type == REG_BINARY && data_size >= 2 && (data[0] == 0x31 || data[0] == 0x33)) ||
                        (type == REG_SZ && data_size >= sizeof(wchar_t) && (reinterpret_cast<wchar_t*>(data)[0] == L'1' || reinterpret_cast<wchar_t*>(data)[0] == L'3')) ||
                        (type == REG_DWORD && (*reinterpret_cast<DWORD*>(data) == 1 || *reinterpret_cast<DWORD*>(data) == 3 || *reinterpret_cast<DWORD*>(data) == 0x31 || *reinterpret_cast<DWORD*>(data) == 0x33))) {
                        state.amd_dx11_uma_boost = true;
                    }
                }

                data_size = sizeof(data);
                if (RegQueryValueExW(hUmd, L"Tessellation", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                    state.amd_tessellation_max = ParseTessellationValue(data, data_size, type);
                }

                RegCloseKey(hUmd);
            }

            RegCloseKey(hAdapter);
            if (state.amd_shader_cache_forced && state.amd_dx11_uma_boost && state.amd_tessellation_max > 0) {
                break;
            }
        }
    }

    // 6. GameDVR / FSE Behavior
    HKEY hDvrQuery = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"System\\GameConfigStore", 0, KEY_READ, &hDvrQuery) == ERROR_SUCCESS) {
        DWORD dvr_val = 1;
        DWORD dvr_type = 0;
        DWORD dvr_size = sizeof(dvr_val);
        if (RegQueryValueExW(hDvrQuery, L"GameDVR_Enabled", nullptr, &dvr_type, reinterpret_cast<LPBYTE>(&dvr_val), &dvr_size) == ERROR_SUCCESS && dvr_type == REG_DWORD) {
            state.gamedvr_disabled = (dvr_val == 0);
        }
        RegCloseKey(hDvrQuery);
    }

    // 7. APU Laptop Power Profile (PCIe ASPM & Processor Boost Mode)
    static HMODULE s_hPowrProfQuery = LoadLibraryW(L"powrprof.dll");
    if (s_hPowrProfQuery) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        static auto pfnGetActive = reinterpret_cast<pfnPowerGetActiveScheme>(GetProcAddress(s_hPowrProfQuery, "PowerGetActiveScheme"));
        static auto pfnReadAC = reinterpret_cast<pfnPowerReadACValueIndex>(GetProcAddress(s_hPowrProfQuery, "PowerReadACValueIndex"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (pfnGetActive && pfnReadAC) {
            GUID* activePolicy = nullptr;
            if (pfnGetActive(nullptr, &activePolicy) == ERROR_SUCCESS && activePolicy) {
                DWORD aspm_val = 1;
                DWORD boost_val = 2;
                pfnReadAC(nullptr, activePolicy, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM, &aspm_val);
                pfnReadAC(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_PERF_BOOST_MODE, &boost_val);
                state.apu_power_optimized = (aspm_val == 0);
                LocalFree(activePolicy);
            }
        }
    }

    // 8. Blindaje de Memoria de Kernel NT (DisablePagingExecutive & LargeSystemCache)
    state.kernel_paging_shield_active = QueryKernelMemoryPagingShield();

    // 9. MMCSS Audio & Gaming Priority Shield (Ronda 2 - Riesgo 2)
    QueryMmcssTaskTweaks(state.mmcss_scheduling_category, state.mmcss_sfio_priority, state.mmcss_clock_rate);

    // 10. Sobrescritura de Presupuesto VRAM UMA (4 GB DXGI & Driver AMD)
    uint64_t dxgi_vram_bytes = 0;
    QueryAmdVramReporting(state.amd_vram_reported_mb, dxgi_vram_bytes);
    state.amd_vram_override_active = (state.amd_vram_reported_mb >= 2048 || dxgi_vram_bytes >= 2147483648ULL);
    if (state.amd_vram_reported_mb == 0 && dxgi_vram_bytes > 0) {
        state.amd_vram_reported_mb = static_cast<uint32_t>(dxgi_vram_bytes / (1024ULL * 1024ULL));
    }

    return state;
}

bool GraphicsOptimizer::SetAmdShaderCacheForced(bool force_on, bool dry_run) {
    if (dry_run) {
        return true;
    }

    SetAmdDx11UmaDriverBoost(force_on, dry_run);

    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    const uint8_t binary_val[2] = {
        static_cast<uint8_t>(force_on ? 0x32 : 0x31),
        0x00
    };

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            // Comprobar si tiene subclave UMD
            HKEY hUmd = nullptr;
            LSTATUS umd_status = RegOpenKeyExW(hAdapter, L"UMD", 0, KEY_READ | KEY_WRITE, &hUmd);
            if (umd_status == ERROR_SUCCESS) {
                is_amd = true;
            }

            // O si ya tenía el valor ShaderCache presente
            if (!is_amd) {
                DWORD dummy_size = 0;
                if (RegQueryValueExW(hAdapter, L"ShaderCache", nullptr, nullptr, nullptr, &dummy_size) == ERROR_SUCCESS) {
                    is_amd = true;
                }
            }

            if (is_amd) {
                LSTATUS st1 = RegSetValueExW(
                    hAdapter,
                    L"ShaderCache",
                    0,
                    REG_BINARY,
                    binary_val,
                    sizeof(binary_val)
                );
                if (st1 == ERROR_SUCCESS) {
                    updated_any = true;
                }

                if (hUmd) {
                    LSTATUS st2 = RegSetValueExW(
                        hUmd,
                        L"ShaderCache",
                        0,
                        REG_BINARY,
                        binary_val,
                        sizeof(binary_val)
                    );
                    if (st2 == ERROR_SUCCESS) {
                        updated_any = true;
                    }
                }
            }

            if (hUmd) {
                RegCloseKey(hUmd);
            }
            RegCloseKey(hAdapter);
        }
    }

    return updated_any;
}

bool GraphicsOptimizer::SetMemoryCompressionEnabled(bool enable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"";
    if (enable) {
        cmd += L"Enable-MMAgent -MemoryCompression\"";
    } else {
        cmd += L"Disable-MMAgent -MemoryCompression\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmd_buffer(cmd.begin(), cmd.end());
    cmd_buffer.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        cmd_buffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!created) {
        return false;
    }

    HCURSOR hOldCursor = SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32650)));
    ULONGLONG start_tick = GetTickCount64();
    constexpr ULONGLONG kTimeoutMs = 5000;
    DWORD wait_res = WAIT_TIMEOUT;

    while (true) {
        ULONGLONG elapsed = GetTickCount64() - start_tick;
        if (elapsed >= kTimeoutMs) {
            wait_res = WAIT_TIMEOUT;
            break;
        }
        DWORD remaining = static_cast<DWORD>(kTimeoutMs - elapsed);
        DWORD mw = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, remaining, QS_ALLINPUT);
        if (mw == WAIT_OBJECT_0) {
            wait_res = WAIT_OBJECT_0;
            break;
        } else if (mw == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            wait_res = mw;
            break;
        }
    }
    if (hOldCursor) {
        SetCursor(hOldCursor);
    }
    DWORD exit_code = 1;
    if (wait_res == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return (wait_res == WAIT_OBJECT_0 && exit_code == 0);
}

bool GraphicsOptimizer::SetMpoDisabled(bool disable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hKey = nullptr;
    DWORD disposition = 0;
    LSTATUS status = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\Dwm",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        &disposition
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    bool success = false;
    if (disable) {
        DWORD value = 5;
        LSTATUS set_status = RegSetValueExW(
            hKey,
            L"OverlayTestMode",
            0,
            REG_DWORD,
            reinterpret_cast<const BYTE*>(&value),
            sizeof(value)
        );
        success = (set_status == ERROR_SUCCESS);
    } else {
        LSTATUS del_status = RegDeleteValueW(hKey, L"OverlayTestMode");
        if (del_status == ERROR_SUCCESS || del_status == ERROR_FILE_NOT_FOUND) {
            success = true;
        } else {
            DWORD value = 0;
            success = (RegSetValueExW(hKey, L"OverlayTestMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS);
        }
    }

    RegCloseKey(hKey);
    return success;
}

bool GraphicsOptimizer::SetMmcssGamingProfileOptimized(bool optimize, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hProfile = nullptr;
    DWORD disposition = 0;
    LSTATUS stProfile = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hProfile,
        &disposition
    );

    bool profile_ok = false;
    if (stProfile == ERROR_SUCCESS) {
        DWORD sys_resp = optimize ? 10 : 20;
        LSTATUS s1 = RegSetValueExW(hProfile, L"SystemResponsiveness", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sys_resp), sizeof(sys_resp));
        profile_ok = (s1 == ERROR_SUCCESS);
        RegCloseKey(hProfile);
    }

    HKEY hGames = nullptr;
    LSTATUS stGames = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hGames,
        &disposition
    );

    bool games_ok = false;
    if (stGames == ERROR_SUCCESS) {
        DWORD gpu_prio = optimize ? 8 : 2;
        DWORD prio = optimize ? 6 : 2;
        const wchar_t* sched_cat = optimize ? L"High" : L"Medium";
        const wchar_t* sfio_prio = optimize ? L"High" : L"Normal";

        LSTATUS s1 = RegSetValueExW(hGames, L"GPU Priority", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&gpu_prio), sizeof(gpu_prio));
        LSTATUS s2 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&prio), sizeof(prio));
        LSTATUS s3 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
        LSTATUS s4 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));

        games_ok = (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS && s4 == ERROR_SUCCESS);
        RegCloseKey(hGames);
    }

    return profile_ok && games_ok;
}

bool GraphicsOptimizer::SetDynamicPrioritySeparation(uint32_t value, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD dwVal = static_cast<DWORD>(value);
    LSTATUS set_status = RegSetValueExW(
        hKey,
        L"Win32PrioritySeparation",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&dwVal),
        sizeof(dwVal)
    );

    RegCloseKey(hKey);
    return (set_status == ERROR_SUCCESS);
}

bool GraphicsOptimizer::SetKernelMemoryPagingShield(bool enable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD disable_paging = enable ? 1 : 0;
    DWORD large_cache = 0; // Evita que la caché de archivos del sistema desplace la memoria de los juegos

    LSTATUS st1 = RegSetValueExW(
        hKey,
        L"DisablePagingExecutive",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&disable_paging),
        sizeof(disable_paging)
    );

    LSTATUS st2 = RegSetValueExW(
        hKey,
        L"LargeSystemCache",
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&large_cache),
        sizeof(large_cache)
    );

    RegCloseKey(hKey);
    return (st1 == ERROR_SUCCESS && st2 == ERROR_SUCCESS);
}

bool GraphicsOptimizer::QueryKernelMemoryPagingShield() {
    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
        0,
        KEY_READ,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    DWORD val = 0;
    DWORD type = 0;
    DWORD size = sizeof(val);
    bool active = false;

    if (RegQueryValueExW(hKey, L"DisablePagingExecutive", nullptr, &type, reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS && type == REG_DWORD) {
        active = (val == 1);
    }

    RegCloseKey(hKey);
    return active;
}

uint32_t GraphicsOptimizer::QueryAmdTessellationCap() {
    return QuerySystemGraphicsTweaks().amd_tessellation_max;
}

bool GraphicsOptimizer::ApplyMmcssTaskTweaks(bool dryRun) {
    if (dryRun) {
        // Simular validación de acceso a Multimedia\SystemProfile y Tasks\Games sin modificar registro
        HKEY hProfile = nullptr;
        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
            0, KEY_READ, &hProfile) == ERROR_SUCCESS) {
            RegCloseKey(hProfile);
        }
        HKEY hGames = nullptr;
        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
            0, KEY_READ, &hGames) == ERROR_SUCCESS) {
            RegCloseKey(hGames);
        }
        OutputDebugStringW(L"[VegaFlow] Dry-Run: ApplyMmcssTaskTweaks simulated successfully.\n");
        return true;
    }

    // 1. Multimedia\SystemProfile -> SystemResponsiveness = 10 (DWORD)
    HKEY hProfile = nullptr;
    LSTATUS stProfile = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0, KEY_SET_VALUE, &hProfile
    );
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }
    const DWORD sys_resp = 10;
    stProfile = RegSetValueExW(
        hProfile,
        L"SystemResponsiveness",
        0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&sys_resp),
        sizeof(sys_resp)
    );
    RegCloseKey(hProfile);
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }

    // 2. Tasks\Games -> Scheduling Category, SFIO Priority, Background Only, Clock Rate, GPU Priority, Priority
    HKEY hGames = nullptr;
    LSTATUS stGames = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_SET_VALUE, &hGames
    );
    if (stGames != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* sched_cat = L"High";
    const wchar_t* sfio_prio = L"High";
    const wchar_t* bg_only = L"False";
    const DWORD clock_rate = 10000;
    const DWORD gpu_prio = 8;
    const DWORD prio = 6;

    LSTATUS s1 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
    LSTATUS s2 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));
    LSTATUS s3 = RegSetValueExW(hGames, L"Background Only", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(bg_only), static_cast<DWORD>((wcslen(bg_only) + 1) * sizeof(wchar_t)));
    LSTATUS s4 = RegSetValueExW(hGames, L"Clock Rate", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&clock_rate), sizeof(clock_rate));
    LSTATUS s5 = RegSetValueExW(hGames, L"GPU Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&gpu_prio), sizeof(gpu_prio));
    LSTATUS s6 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&prio), sizeof(prio));

    RegCloseKey(hGames);

    return (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS &&
            s4 == ERROR_SUCCESS && s5 == ERROR_SUCCESS && s6 == ERROR_SUCCESS);
}

bool GraphicsOptimizer::RestoreMmcssTaskTweaks(bool dryRun) {
    if (dryRun) {
        OutputDebugStringW(L"[VegaFlow] Dry-Run: RestoreMmcssTaskTweaks simulated successfully.\n");
        return true;
    }

    // 1. Multimedia\SystemProfile -> SystemResponsiveness = 20 (default estándar de Windows NT)
    HKEY hProfile = nullptr;
    LSTATUS stProfile = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0, KEY_SET_VALUE, &hProfile
    );
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }
    const DWORD default_resp = 20;
    stProfile = RegSetValueExW(
        hProfile,
        L"SystemResponsiveness",
        0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&default_resp),
        sizeof(default_resp)
    );
    RegCloseKey(hProfile);
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }

    // 2. Tasks\Games -> Scheduling Category = Medium, SFIO Priority = Normal, Background Only = True, Clock Rate = 10000, Priority = 2
    HKEY hGames = nullptr;
    LSTATUS stGames = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_SET_VALUE, &hGames
    );
    if (stGames != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* sched_cat = L"Medium";
    const wchar_t* sfio_prio = L"Normal";
    const wchar_t* bg_only = L"True";
    const DWORD clock_rate = 10000;
    const DWORD prio = 2;

    LSTATUS s1 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
    LSTATUS s2 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));
    LSTATUS s3 = RegSetValueExW(hGames, L"Background Only", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(bg_only), static_cast<DWORD>((wcslen(bg_only) + 1) * sizeof(wchar_t)));
    LSTATUS s4 = RegSetValueExW(hGames, L"Clock Rate", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&clock_rate), sizeof(clock_rate));
    LSTATUS s5 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&prio), sizeof(prio));

    RegCloseKey(hGames);

    return (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS &&
            s4 == ERROR_SUCCESS && s5 == ERROR_SUCCESS);
}

bool GraphicsOptimizer::QueryMmcssTaskTweaks(std::string& schedulingCategory, std::string& sfioPriority, uint32_t& clockRate) {
    schedulingCategory = "Medium";
    sfioPriority = "Normal";
    clockRate = 10000;

    HKEY hGames = nullptr;
    if (RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_READ, &hGames) != ERROR_SUCCESS) {
        return false;
    }

    auto ReadRegSz = [](HKEY hKey, const wchar_t* valName, std::string& outVal) {
        wchar_t buf[256] = {0};
        DWORD type = 0;
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(hKey, valName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS && type == REG_SZ) {
            buf[(sizeof(buf) / sizeof(wchar_t)) - 1] = L'\0';
            int required = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            if (required > 1) {
                std::vector<char> utf8(required);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), required, nullptr, nullptr);
                outVal = utf8.data();
            } else if (required == 1) {
                outVal.clear();
            }
        }
    };

    ReadRegSz(hGames, L"Scheduling Category", schedulingCategory);
    ReadRegSz(hGames, L"SFIO Priority", sfioPriority);

    DWORD cr_val = 0;
    DWORD type = 0;
    DWORD size = sizeof(cr_val);
    if (RegQueryValueExW(hGames, L"Clock Rate", nullptr, &type, reinterpret_cast<LPBYTE>(&cr_val), &size) == ERROR_SUCCESS && type == REG_DWORD) {
        clockRate = cr_val;
    }

    RegCloseKey(hGames);
    return true;
}

bool GraphicsOptimizer::SetAmdDx11UmaDriverBoost(bool enable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    const auto policy = HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware());
    const uint32_t active_tess_cap = enable ? policy.amd_tessellation_cap : 64;
    const std::string tess_str = std::to_string(active_tess_cap);
    std::vector<uint8_t> tess_val(tess_str.begin(), tess_str.end());
    tess_val.push_back(0x00);
    const DWORD tess_len = static_cast<DWORD>(tess_val.size());
    const std::wstring sz_tess(tess_str.begin(), tess_str.end());

    const uint8_t sfr_val[2] = { static_cast<uint8_t>(enable ? 0x31 : 0x30), 0x00 };
    const uint8_t fqs_val[2] = { 0x31, 0x00 }; // FlipQueueSize = 1 (ASCII '1', ultra-low input latency)
    const uint8_t tfq_val[2] = { static_cast<uint8_t>(enable ? 0x32 : 0x30), 0x00 };
    const uint8_t tess_opt_val[2] = { static_cast<uint8_t>(enable ? 0x31 : 0x30), 0x00 };
    const uint8_t main3d_val[2] = { static_cast<uint8_t>(enable ? 0x31 : 0x30), 0x00 };
    const uint8_t turbosync_val[2] = { 0x30, 0x00 };
    const uint8_t dfqo_val[2] = { static_cast<uint8_t>(enable ? 0x30 : 0x31), 0x00 };

    auto SetUmdValue = [](HKEY hKey, const wchar_t* name, const uint8_t bin_val[2]) -> LSTATUS {
        DWORD type = 0;
        if (RegQueryValueExW(hKey, name, nullptr, &type, nullptr, nullptr) == ERROR_SUCCESS && type == REG_SZ) {
            wchar_t sz_val[2] = { static_cast<wchar_t>(bin_val[0]), L'\0' };
            return RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(sz_val), sizeof(sz_val));
        }
        return RegSetValueExW(hKey, name, 0, REG_BINARY, bin_val, 2);
    };

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            HKEY hUmd = nullptr;
            LSTATUS umd_status = RegOpenKeyExW(hAdapter, L"UMD", 0, KEY_READ | KEY_WRITE, &hUmd);
            if (umd_status == ERROR_SUCCESS) {
                is_amd = true;
            }

            if (!is_amd) {
                DWORD dummy_size = 0;
                if (RegQueryValueExW(hAdapter, L"ShaderCache", nullptr, nullptr, nullptr, &dummy_size) == ERROR_SUCCESS) {
                    is_amd = true;
                }
            }

            if (is_amd) {
                if (hUmd || RegCreateKeyExW(hAdapter, L"UMD", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &hUmd, nullptr) == ERROR_SUCCESS) {
                    LSTATUS st1 = SetUmdValue(hUmd, L"SurfaceFormatReplacements", sfr_val);
                    LSTATUS st2 = SetUmdValue(hUmd, L"FlipQueueSize", fqs_val);
                    LSTATUS st3 = SetUmdValue(hUmd, L"TFQ", tfq_val);
                    LSTATUS st4 = SetUmdValue(hUmd, L"Tessellation_OPTION", tess_opt_val);

                    DWORD t_type = 0;
                    LSTATUS st5 = ERROR_SUCCESS;
                    if (RegQueryValueExW(hUmd, L"Tessellation", nullptr, &t_type, nullptr, nullptr) == ERROR_SUCCESS && t_type == REG_SZ) {
                        st5 = RegSetValueExW(hUmd, L"Tessellation", 0, REG_SZ, reinterpret_cast<const BYTE*>(sz_tess.c_str()), static_cast<DWORD>((sz_tess.size() + 1) * sizeof(wchar_t)));
                    } else {
                        st5 = RegSetValueExW(hUmd, L"Tessellation", 0, REG_BINARY, tess_val.data(), tess_len);
                    }

                    LSTATUS st6 = SetUmdValue(hUmd, L"Main3D_DEF", main3d_val);
                    LSTATUS st7 = SetUmdValue(hUmd, L"TurboSync", turbosync_val);
                    LSTATUS st8 = SetUmdValue(hUmd, L"DisableFlipQueueOptimization", dfqo_val);
                    if (st1 == ERROR_SUCCESS || st2 == ERROR_SUCCESS || st3 == ERROR_SUCCESS ||
                        st4 == ERROR_SUCCESS || st5 == ERROR_SUCCESS || st6 == ERROR_SUCCESS ||
                        st7 == ERROR_SUCCESS || st8 == ERROR_SUCCESS) {
                        updated_any = true;
                    }
                }
            }

            if (hUmd) {
                RegCloseKey(hUmd);
            }
            RegCloseKey(hAdapter);
        }
    }

    return updated_any;
}

bool GraphicsOptimizer::ApplyAmdDx11DriverBoost(bool enable, bool dry_run) {
    return SetAmdDx11UmaDriverBoost(enable, dry_run);
}

bool GraphicsOptimizer::SetAmdTessellationCap(uint32_t max_tess, bool dry_run) {
    if (dry_run) {
        return true;
    }

    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    bool enable_override = (max_tess > 0 && max_tess < 64);
    const uint8_t tess_opt_val[2] = { static_cast<uint8_t>(enable_override ? 0x31 : 0x30), 0x00 };

    std::string tess_str = (max_tess > 0) ? std::to_string(max_tess) : "64";
    std::vector<uint8_t> tess_bytes(tess_str.begin(), tess_str.end());
    tess_bytes.push_back(0x00);
    std::wstring tess_wstr(tess_str.begin(), tess_str.end());

    auto SetUmdValue = [](HKEY hKey, const wchar_t* name, const uint8_t bin_val[2]) -> LSTATUS {
        DWORD type = 0;
        if (RegQueryValueExW(hKey, name, nullptr, &type, nullptr, nullptr) == ERROR_SUCCESS && type == REG_SZ) {
            wchar_t sz_val[2] = { static_cast<wchar_t>(bin_val[0]), L'\0' };
            return RegSetValueExW(hKey, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(sz_val), sizeof(sz_val));
        }
        return RegSetValueExW(hKey, name, 0, REG_BINARY, bin_val, 2);
    };

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            HKEY hUmd = nullptr;
            LSTATUS umd_status = RegOpenKeyExW(hAdapter, L"UMD", 0, KEY_READ | KEY_WRITE, &hUmd);
            if (umd_status == ERROR_SUCCESS) {
                is_amd = true;
            }

            if (is_amd) {
                if (hUmd || RegCreateKeyExW(hAdapter, L"UMD", 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, &hUmd, nullptr) == ERROR_SUCCESS) {
                    LSTATUS stOpt = SetUmdValue(hUmd, L"Tessellation_OPTION", tess_opt_val);

                    DWORD t_type = 0;
                    LSTATUS stTess = ERROR_SUCCESS;
                    if (RegQueryValueExW(hUmd, L"Tessellation", nullptr, &t_type, nullptr, nullptr) == ERROR_SUCCESS && t_type == REG_SZ) {
                        stTess = RegSetValueExW(hUmd, L"Tessellation", 0, REG_SZ, reinterpret_cast<const BYTE*>(tess_wstr.c_str()), static_cast<DWORD>((tess_wstr.size() + 1) * sizeof(wchar_t)));
                    } else {
                        stTess = RegSetValueExW(hUmd, L"Tessellation", 0, REG_BINARY, tess_bytes.data(), static_cast<DWORD>(tess_bytes.size()));
                    }

                    if (stOpt == ERROR_SUCCESS || stTess == ERROR_SUCCESS) {
                        updated_any = true;
                    }
                }
            }

            if (hUmd) {
                RegCloseKey(hUmd);
            }
            RegCloseKey(hAdapter);
        }
    }

    return updated_any;
}

bool GraphicsOptimizer::SetGameDvrAndFseOptimized(bool optimize, bool dry_run) {
    if (dry_run) {
        return true;
    }

    bool success = true;

    // 1. HKCU\System\GameConfigStore
    HKEY hStore = nullptr;
    LSTATUS stStore = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"System\\GameConfigStore",
        0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE,
        nullptr, &hStore, nullptr
    );
    if (stStore == ERROR_SUCCESS) {
        DWORD dvr_enabled = optimize ? 0 : 1;
        DWORD fse_mode = optimize ? 2 : 0;
        DWORD honor_user = optimize ? 1 : 0;
        DWORD honor_dxgi = optimize ? 1 : 0;
        DWORD efse_flags = 0;

        RegSetValueExW(hStore, L"GameDVR_Enabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dvr_enabled), sizeof(dvr_enabled));
        RegSetValueExW(hStore, L"GameDVR_FSEBehaviorMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&fse_mode), sizeof(fse_mode));
        RegSetValueExW(hStore, L"GameDVR_HonorUserFSEBehaviorMode", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&honor_user), sizeof(honor_user));
        RegSetValueExW(hStore, L"GameDVR_DXGIHonorFSEWindowsCompatible", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&honor_dxgi), sizeof(honor_dxgi));
        RegSetValueExW(hStore, L"GameDVR_EFSEFeatureFlags", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&efse_flags), sizeof(efse_flags));

        RegCloseKey(hStore);
    } else {
        success = false;
    }

    // 2. HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\GameDVR
    HKEY hDvr = nullptr;
    LSTATUS stDvr = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR",
        0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE,
        nullptr, &hDvr, nullptr
    );
    if (stDvr == ERROR_SUCCESS) {
        DWORD app_capture = optimize ? 0 : 1;
        RegSetValueExW(hDvr, L"AppCaptureEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&app_capture), sizeof(app_capture));
        RegCloseKey(hDvr);
    } else {
        success = false;
    }

    return success;
}

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
        boost_val = 4;        // Efficient Aggressive (reduce el voltaje de CPU y evita picos de calor innecesarios)
        cpu_max_freq = policy.cpu_stapm_cap_mhz;  // 0 = 100% desbloqueado (Smart Uncapped Boost)
        epp_val = 25;         // Alto rendimiento eficiente SMU, priorizando respuesta inmediata en hilos del juego y GPU al 100%
        aspm_val = 0;         // PCIe ASPM deshabilitado
        core_parking_val = 100;
        cpu_throttle_max = 100;
    } else { // ApuPowerMode::Balanced
        boost_val = 2;        // Default: Boost agresivo estándar (4.0 GHz)
        cpu_max_freq = 0;     // Desbloqueado
        epp_val = 50;         // EPP balanceado estándar de Windows
        aspm_val = 1;         // PCIe ASPM habilitado
        core_parking_val = 50;
        cpu_throttle_max = 100;
    }

    // Escribir en AC
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM, aspm_val);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_PERF_BOOST_MODE, boost_val);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_CORE_PARKING_MIN_CORES, core_parking_val);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX, cpu_max_freq);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX_ALT, cpu_max_freq);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM, cpu_throttle_max);
    pfnWriteACValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_ENERGY_PERF_PREFERENCE, epp_val);

    // Escribir en DC si está disponible para paridad
    if (pfnWriteDCValueIndex) {
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM, aspm_val);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_PERF_BOOST_MODE, boost_val);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_CORE_PARKING_MIN_CORES, core_parking_val);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX, cpu_max_freq);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX_ALT, cpu_max_freq);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM, cpu_throttle_max);
        pfnWriteDCValueIndex(nullptr, activePolicy, &GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_ENERGY_PERF_PREFERENCE, epp_val);
    }

    DWORD set_res = pfnSetActiveScheme(nullptr, activePolicy);
    LocalFree(activePolicy);

    return (set_res == ERROR_SUCCESS);
}

bool GraphicsOptimizer::SetApuLaptopPowerProfile(bool optimize, bool dry_run) {
    return SetApuPowerMode(optimize ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced, dry_run);
}

std::vector<ShaderCacheReport> GraphicsOptimizer::InspectShaderCaches() {
    std::vector<ShaderCacheReport> reports;

    wchar_t local_app_data[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return reports;
    }

    std::filesystem::path local_base(local_app_data);

    // Standard shader cache folders across DirectX, AMD, NVIDIA, and Intel
    std::vector<std::filesystem::path> target_dirs = {
        local_base / L"D3DSCache",
        local_base / L"AMD" / L"DxCache",
        local_base / L"NVIDIA" / L"DXCache",
        local_base / L"AMD" / L"GLCache",
        local_base / L"NVIDIA" / L"GLCache",
        local_base / L"Intel" / L"ShaderCache"
    };

    for (const auto& dir : target_dirs) {
        ShaderCacheReport rep;
        rep.path = dir.wstring();
        rep.total_size_bytes = 0;
        rep.file_count = 0;

        std::error_code ec;
        if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec)) {
            rep.exists = true;

            auto it = std::filesystem::recursive_directory_iterator(
                dir,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );

            for (const auto& entry : it) {
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec)) {
                    rep.file_count++;
                    auto fsize = entry.file_size(entry_ec);
                    if (!entry_ec) {
                        rep.total_size_bytes += fsize;
                    }
                }
            }
        } else {
            rep.exists = false;
        }

        reports.push_back(rep);
    }

    return reports;
}

bool GraphicsOptimizer::ApplyAmdVramReportingOverride(uint32_t target_mb, bool dry_run) {
    if (dry_run) {
        return true;
    }

    bool updated_driver = false;
    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    uint64_t target_bytes = static_cast<uint64_t>(target_mb) * 1024ULL * 1024ULL;
    DWORD dword_bytes = (target_bytes > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : static_cast<DWORD>(target_bytes);

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            if (is_amd) {
                LSTATUS s1 = RegSetValueExW(hAdapter, L"HardwareInformation.MemorySize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dword_bytes), sizeof(dword_bytes));
                LSTATUS s2 = RegSetValueExW(hAdapter, L"HardwareInformation.qwMemorySize", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&target_bytes), sizeof(target_bytes));
                if (s1 == ERROR_SUCCESS || s2 == ERROR_SUCCESS) {
                    updated_driver = true;
                }
            }
            RegCloseKey(hAdapter);
        }
    }

    bool updated_dxgi = false;
    HKEY hDxKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ | KEY_WRITE, &hDxKey) == ERROR_SUCCESS) {
        wchar_t subName[256];
        DWORD subLen = sizeof(subName) / sizeof(subName[0]);
        DWORD index = 0;

        while (RegEnumKeyExW(hDxKey, index++, subName, &subLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subLen = sizeof(subName) / sizeof(subName[0]);
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hDxKey, subName, 0, KEY_READ | KEY_WRITE, &hSub) == ERROR_SUCCESS) {
                bool is_amd = false;
                wchar_t desc_buf[256] = {0};
                DWORD desc_size = sizeof(desc_buf);
                DWORD type = 0;

                if (RegQueryValueExW(hSub, L"Description", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS && type == REG_SZ) {
                    std::wstring desc = ToLower(desc_buf);
                    if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                        is_amd = true;
                    }
                }

                DWORD vendor_id = 0;
                DWORD vendor_size = sizeof(vendor_id);
                if (RegQueryValueExW(hSub, L"VendorId", nullptr, &type, reinterpret_cast<LPBYTE>(&vendor_id), &vendor_size) == ERROR_SUCCESS && (type == REG_DWORD || type == REG_BINARY)) {
                    if (vendor_id == 4098 || vendor_id == 0x1002) {
                        is_amd = true;
                    }
                }

                if (is_amd) {
                    LSTATUS st = RegSetValueExW(hSub, L"DedicatedVideoMemory", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&target_bytes), sizeof(target_bytes));
                    if (st == ERROR_SUCCESS) {
                        updated_dxgi = true;
                    }
                }
                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hDxKey);
    }

    return updated_driver || updated_dxgi;
}

bool GraphicsOptimizer::RestoreAmdVramReporting(bool dry_run) {
    if (dry_run) {
        return true;
    }

    bool updated_driver = false;
    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    DWORD default_mem_size = 536870912UL; // 512 MB
    uint64_t default_qw_size = 536870912ULL;

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ | KEY_WRITE, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            if (is_amd) {
                LSTATUS s1 = RegSetValueExW(hAdapter, L"HardwareInformation.MemorySize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&default_mem_size), sizeof(default_mem_size));
                LSTATUS s2 = RegSetValueExW(hAdapter, L"HardwareInformation.qwMemorySize", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&default_qw_size), sizeof(default_qw_size));
                if (s1 == ERROR_SUCCESS || s2 == ERROR_SUCCESS) {
                    updated_driver = true;
                }
            }
            RegCloseKey(hAdapter);
        }
    }

    bool updated_dxgi = false;
    HKEY hDxKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ | KEY_WRITE, &hDxKey) == ERROR_SUCCESS) {
        wchar_t subName[256];
        DWORD subLen = sizeof(subName) / sizeof(subName[0]);
        DWORD index = 0;

        uint64_t default_dxgi = 519634944ULL; // 512 MB nominal Windows DXGI reporting

        while (RegEnumKeyExW(hDxKey, index++, subName, &subLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subLen = sizeof(subName) / sizeof(subName[0]);
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hDxKey, subName, 0, KEY_READ | KEY_WRITE, &hSub) == ERROR_SUCCESS) {
                bool is_amd = false;
                wchar_t desc_buf[256] = {0};
                DWORD desc_size = sizeof(desc_buf);
                DWORD type = 0;

                if (RegQueryValueExW(hSub, L"Description", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS && type == REG_SZ) {
                    std::wstring desc = ToLower(desc_buf);
                    if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                        is_amd = true;
                    }
                }

                DWORD vendor_id = 0;
                DWORD vendor_size = sizeof(vendor_id);
                if (RegQueryValueExW(hSub, L"VendorId", nullptr, &type, reinterpret_cast<LPBYTE>(&vendor_id), &vendor_size) == ERROR_SUCCESS && (type == REG_DWORD || type == REG_BINARY)) {
                    if (vendor_id == 4098 || vendor_id == 0x1002) {
                        is_amd = true;
                    }
                }

                if (is_amd) {
                    LSTATUS st = RegSetValueExW(hSub, L"DedicatedVideoMemory", 0, REG_QWORD, reinterpret_cast<const BYTE*>(&default_dxgi), sizeof(default_dxgi));
                    if (st == ERROR_SUCCESS) {
                        updated_dxgi = true;
                    }
                }
                RegCloseKey(hSub);
            }
        }
        RegCloseKey(hDxKey);
    }

    return updated_driver || updated_dxgi;
}

bool GraphicsOptimizer::QueryAmdVramReporting(uint32_t& out_driver_mb, uint64_t& out_dxgi_bytes) {
    out_driver_mb = 0;
    out_dxgi_bytes = 0;

    static const wchar_t* const kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
    const std::wstring class_base = L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";

    for (const auto* sub : kAdapterSubkeys) {
        std::wstring adapter_path = class_base + L"\\" + sub;
        HKEY hAdapter = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, adapter_path.c_str(), 0, KEY_READ, &hAdapter) == ERROR_SUCCESS) {
            bool is_amd = false;
            wchar_t desc_buf[256] = {0};
            DWORD desc_size = sizeof(desc_buf);
            DWORD type = 0;

            if (RegQueryValueExW(hAdapter, L"DriverDesc", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                std::wstring desc = ToLower(desc_buf);
                if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                    is_amd = true;
                }
            }

            if (!is_amd) {
                desc_size = sizeof(desc_buf);
                if (RegQueryValueExW(hAdapter, L"ProviderName", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS) {
                    std::wstring prov = ToLower(desc_buf);
                    if (prov.find(L"amd") != std::wstring::npos || prov.find(L"advanced micro devices") != std::wstring::npos) {
                        is_amd = true;
                    }
                }
            }

            if (is_amd) {
                uint64_t qw = 0;
                DWORD qw_size = sizeof(qw);
                DWORD qw_type = 0;
                if (RegQueryValueExW(hAdapter, L"HardwareInformation.qwMemorySize", nullptr, &qw_type, reinterpret_cast<LPBYTE>(&qw), &qw_size) == ERROR_SUCCESS && (qw_type == REG_QWORD || qw_type == REG_BINARY)) {
                    if (qw > 0) {
                        out_driver_mb = static_cast<uint32_t>(qw / (1024ULL * 1024ULL));
                    }
                }

                if (out_driver_mb == 0) {
                    DWORD dw = 0;
                    DWORD dw_size = sizeof(dw);
                    DWORD dw_type = 0;
                    if (RegQueryValueExW(hAdapter, L"HardwareInformation.MemorySize", nullptr, &dw_type, reinterpret_cast<LPBYTE>(&dw), &dw_size) == ERROR_SUCCESS && (dw_type == REG_DWORD || dw_type == REG_BINARY)) {
                        if (dw == 0xFFFFFFFFUL) {
                            out_driver_mb = 4096;
                        } else {
                            out_driver_mb = dw / (1024 * 1024);
                        }
                    }
                }
            }
            RegCloseKey(hAdapter);
            if (out_driver_mb > 0) break;
        }
    }

    HKEY hDxKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ, &hDxKey) == ERROR_SUCCESS) {
        wchar_t subName[256];
        DWORD subLen = sizeof(subName) / sizeof(subName[0]);
        DWORD index = 0;

        while (RegEnumKeyExW(hDxKey, index++, subName, &subLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            subLen = sizeof(subName) / sizeof(subName[0]);
            HKEY hSub = nullptr;
            if (RegOpenKeyExW(hDxKey, subName, 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                bool is_amd = false;
                wchar_t desc_buf[256] = {0};
                DWORD desc_size = sizeof(desc_buf);
                DWORD type = 0;

                if (RegQueryValueExW(hSub, L"Description", nullptr, &type, reinterpret_cast<LPBYTE>(desc_buf), &desc_size) == ERROR_SUCCESS && type == REG_SZ) {
                    std::wstring desc = ToLower(desc_buf);
                    if (desc.find(L"amd") != std::wstring::npos || desc.find(L"radeon") != std::wstring::npos) {
                        is_amd = true;
                    }
                }

                DWORD vendor_id = 0;
                DWORD vendor_size = sizeof(vendor_id);
                if (RegQueryValueExW(hSub, L"VendorId", nullptr, &type, reinterpret_cast<LPBYTE>(&vendor_id), &vendor_size) == ERROR_SUCCESS && (type == REG_DWORD || type == REG_BINARY)) {
                    if (vendor_id == 4098 || vendor_id == 0x1002) {
                        is_amd = true;
                    }
                }

                if (is_amd) {
                    uint64_t qw = 0;
                    DWORD qw_size = sizeof(qw);
                    DWORD qw_type = 0;
                    if (RegQueryValueExW(hSub, L"DedicatedVideoMemory", nullptr, &qw_type, reinterpret_cast<LPBYTE>(&qw), &qw_size) == ERROR_SUCCESS && (qw_type == REG_QWORD || qw_type == REG_BINARY)) {
                        out_dxgi_bytes = qw;
                    }
                }
                RegCloseKey(hSub);
                if (out_dxgi_bytes > 0) break;
            }
        }
        RegCloseKey(hDxKey);
    }

    return (out_driver_mb > 0 || out_dxgi_bytes > 0);
}

} // namespace vegaflow
