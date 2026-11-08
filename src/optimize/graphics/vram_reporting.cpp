#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

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
