#include "optimize/graphics/graphics_internal.hpp"
#include "windows/change_journal.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

std::string WideToUtf8Dxgi(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

void QueryPerExeRegString(const wchar_t* subkey, const std::wstring& value_name, bool& out_present, std::wstring& out_val) {
    out_present = false;
    out_val.clear();
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(h, value_name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ) {
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (RegQueryValueExW(h, value_name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &bytes) == ERROR_SUCCESS) {
            out_val.assign(buf.c_str());
            out_present = true;
        }
    }
    RegCloseKey(h);
}

uint64_t JournalBeginDxgiChange(const std::wstring& exe_path, const std::wstring& after_pref, const std::string& reason) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal || exe_path.empty()) return 0;
    JournalEntry e;
    e.kind = JournalTargetKind::DxgiGamePreference;
    e.target = "DXGI:" + WideToUtf8Dxgi(exe_path);
    e.reason = reason;
    e.exe_path = exe_path;
    QueryPerExeRegString(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", exe_path, e.ugp_orig_present, e.ugp_orig_value);
    QueryPerExeRegString(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe_path,
                         e.layers_orig_present, e.layers_orig_value);
    e.before_value = e.ugp_orig_present ? WideToUtf8Dxgi(e.ugp_orig_value) : "(sin preferencia)";
    e.after_value = WideToUtf8Dxgi(after_pref);
    const std::string exe_utf8 = WideToUtf8Dxgi(exe_path);
    e.manual_fix_hint = e.ugp_orig_present
        ? ("reg add \"HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences\" /v \"" + exe_utf8 + "\" /t REG_SZ /d \"" +
           WideToUtf8Dxgi(e.ugp_orig_value) + "\" /f")
        : ("reg delete \"HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences\" /v \"" + exe_utf8 + "\" /f");
    return journal->RecordPending(std::move(e));
}

void JournalEndDxgiChange(uint64_t id, bool ok) {
    if (id == 0) return;
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal) return;
    if (ok) journal->MarkCommitted(id);
    else journal->MarkFailed(id, "Error al escribir preferencia DXGI");
}

} // namespace

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

    const uint64_t j_id = JournalBeginDxgiChange(exe_full_path, final_value, "DXGI Flip Model upgrade");

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
        JournalEndDxgiChange(j_id, false);
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

    JournalEndDxgiChange(j_id, status == ERROR_SUCCESS);
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

    const uint64_t j_id = JournalBeginDxgiChange(exe_full_path, final_value, "DXGI DirectFlip upgrade");

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
        JournalEndDxgiChange(j_id, false);
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

    JournalEndDxgiChange(j_id, status == ERROR_SUCCESS);
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
    if (ChangeJournal* journal = GetActiveChangeJournal()) {
        journal->MarkMatchingReverted(JournalTargetKind::DxgiGamePreference, "DXGI:" + WideToUtf8Dxgi(exe_full_path));
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
