#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

uint32_t GraphicsOptimizer::QueryAmdTessellationCap() {
    return QuerySystemGraphicsTweaks().amd_tessellation_max;
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
                    const std::wstring umd_path = adapter_path + L"\\UMD";
                    if (enable) {
                        for (const wchar_t* n : kUmdBoostValues) RegBackupValue(kHiveLocalMachine, umd_path, hUmd, n);
                    }
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
                    if (!enable) {
                        for (const wchar_t* n : kUmdBoostValues) RegRestoreValue(kHiveLocalMachine, umd_path, n);
                    }
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
                    const std::wstring umd_path = adapter_path + L"\\UMD";
                    if (enable_override) {
                        RegBackupValue(kHiveLocalMachine, umd_path, hUmd, L"Tessellation_OPTION");
                        RegBackupValue(kHiveLocalMachine, umd_path, hUmd, L"Tessellation");
                    }
                    LSTATUS stOpt = SetUmdValue(hUmd, L"Tessellation_OPTION", tess_opt_val);

                    DWORD t_type = 0;
                    LSTATUS stTess = ERROR_SUCCESS;
                    if (RegQueryValueExW(hUmd, L"Tessellation", nullptr, &t_type, nullptr, nullptr) == ERROR_SUCCESS && t_type == REG_SZ) {
                        stTess = RegSetValueExW(hUmd, L"Tessellation", 0, REG_SZ, reinterpret_cast<const BYTE*>(tess_wstr.c_str()), static_cast<DWORD>((tess_wstr.size() + 1) * sizeof(wchar_t)));
                    } else {
                        stTess = RegSetValueExW(hUmd, L"Tessellation", 0, REG_BINARY, tess_bytes.data(), static_cast<DWORD>(tess_bytes.size()));
                    }

                    if (!enable_override) {
                        RegRestoreValue(kHiveLocalMachine, umd_path, L"Tessellation_OPTION");
                        RegRestoreValue(kHiveLocalMachine, umd_path, L"Tessellation");
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

} // namespace vegaflow
