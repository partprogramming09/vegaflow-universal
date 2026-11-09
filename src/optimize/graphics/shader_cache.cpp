#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool GraphicsOptimizer::SetAmdShaderCacheForced(bool force_on, const ApuOptimizationPolicy& policy, bool dry_run) {
    if (dry_run) {
        return true;
    }

    SetAmdDx11UmaDriverBoost(force_on, policy, dry_run);

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

            // Check whether it has a UMA subkey
            HKEY hUmd = nullptr;
            LSTATUS umd_status = RegOpenKeyExW(hAdapter, L"UMD", 0, KEY_READ | KEY_WRITE, &hUmd);
            if (umd_status == ERROR_SUCCESS) {
                is_amd = true;
            }

            // Or whether it already had the ShaderCache value present
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

} // namespace vegaflow
