#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

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
        KEY_READ | KEY_SET_VALUE,
        nullptr,
        &hKey,
        &disposition
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring dwm_path = L"SOFTWARE\\Microsoft\\Windows\\Dwm";
    bool success = false;
    if (disable) {
        RegBackupValue(kHiveLocalMachine, dwm_path, hKey, L"OverlayTestMode");
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
    if (!disable) {
        RegRestoreValue(kHiveLocalMachine, dwm_path, L"OverlayTestMode");
    }
    return success;
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
    static const wchar_t* const kStoreValues[] = {
        L"GameDVR_Enabled", L"GameDVR_FSEBehaviorMode", L"GameDVR_HonorUserFSEBehaviorMode",
        L"GameDVR_DXGIHonorFSEWindowsCompatible", L"GameDVR_EFSEFeatureFlags"
    };
    const std::wstring store_path = L"System\\GameConfigStore";
    const std::wstring dvr_path = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
    if (stStore == ERROR_SUCCESS) {
        if (optimize) {
            for (const wchar_t* n : kStoreValues) RegBackupValue(kHiveCurrentUser, store_path, hStore, n);
        }
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
        if (!optimize) {
            for (const wchar_t* n : kStoreValues) RegRestoreValue(kHiveCurrentUser, store_path, n);
        }
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
        if (optimize) RegBackupValue(kHiveCurrentUser, dvr_path, hDvr, L"AppCaptureEnabled");
        DWORD app_capture = optimize ? 0 : 1;
        RegSetValueExW(hDvr, L"AppCaptureEnabled", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&app_capture), sizeof(app_capture));
        RegCloseKey(hDvr);
        if (!optimize) RegRestoreValue(kHiveCurrentUser, dvr_path, L"AppCaptureEnabled");
    } else {
        success = false;
    }

    return success;
}

} // namespace vegaflow
