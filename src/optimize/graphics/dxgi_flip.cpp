#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

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

} // namespace vegaflow
