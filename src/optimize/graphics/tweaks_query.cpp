#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

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

} // namespace

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

    // 2. Check for the MemCompression process on the system
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

    // 3. CPU priority (Win32PrioritySeparation)
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

    // 5. Inspect HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}\0000..0003
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

            // Check the value directly on the adapter key
            if (RegQueryValueExW(hAdapter, L"ShaderCache", nullptr, &type, data, &data_size) == ERROR_SUCCESS) {
                if (check_shader_cache_val(data, data_size, type)) {
                    state.amd_shader_cache_forced = true;
                }
            }

            // Check the UMA subkey
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

    // 8. NT Kernel Memory Shield (DisablePagingExecutive & LargeSystemCache)
    state.kernel_paging_shield_active = QueryKernelMemoryPagingShield();

    // 9. MMCSS Audio & Gaming Priority Shield (Round 2 - Risk 2)
    QueryMmcssTaskTweaks(state.mmcss_scheduling_category, state.mmcss_sfio_priority, state.mmcss_clock_rate);

    // 10. UMA VRAM Budget Override (4 GB DXGI & AMD Driver)
    uint64_t dxgi_vram_bytes = 0;
    QueryAmdVramReporting(state.amd_vram_reported_mb, dxgi_vram_bytes);
    state.amd_vram_override_active = (state.amd_vram_reported_mb >= 2048 || dxgi_vram_bytes >= 2147483648ULL);
    if (state.amd_vram_reported_mb == 0 && dxgi_vram_bytes > 0) {
        state.amd_vram_reported_mb = static_cast<uint32_t>(dxgi_vram_bytes / (1024ULL * 1024ULL));
    }

    return state;
}

} // namespace vegaflow
