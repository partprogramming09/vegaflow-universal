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
        const uint64_t jid = JournalBeginRegChange(kHiveLocalMachine, dwm_path, hKey, L"OverlayTestMode", "5", "Desactivar MPO (DWM)");
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
        JournalEndRegChange(jid, success);
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
        if (!RegRestoreValue(kHiveLocalMachine, dwm_path, L"OverlayTestMode") && success) {
            JournalMarkRegRestored(kHiveLocalMachine, dwm_path, L"OverlayTestMode");
        }
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
        DWORD dvr_enabled = optimize ? 0 : 1;
        DWORD fse_mode = optimize ? 2 : 0;
        DWORD honor_user = optimize ? 1 : 0;
        DWORD honor_dxgi = optimize ? 1 : 0;
        DWORD efse_flags = 0;
        const DWORD vals[5] = {dvr_enabled, fse_mode, honor_user, honor_dxgi, efse_flags};
        uint64_t jids[5] = {};
        if (optimize) {
            for (size_t i = 0; i < 5; ++i) {
                RegBackupValue(kHiveCurrentUser, store_path, hStore, kStoreValues[i]);
                jids[i] = JournalBeginRegChange(kHiveCurrentUser, store_path, hStore, kStoreValues[i],
                                                std::to_string(vals[i]), "Desactivar GameDVR / FSE");
            }
        }
        for (size_t i = 0; i < 5; ++i) {
            const LSTATUS st = RegSetValueExW(hStore, kStoreValues[i], 0, REG_DWORD,
                                              reinterpret_cast<const BYTE*>(&vals[i]), sizeof(DWORD));
            if (optimize) JournalEndRegChange(jids[i], st == ERROR_SUCCESS);
        }
        RegCloseKey(hStore);
        if (!optimize) {
            for (const wchar_t* n : kStoreValues) {
                if (!RegRestoreValue(kHiveCurrentUser, store_path, n)) {
                    JournalMarkRegRestored(kHiveCurrentUser, store_path, n);
                }
            }
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
        uint64_t jid = 0;
        DWORD app_capture = optimize ? 0 : 1;
        if (optimize) {
            RegBackupValue(kHiveCurrentUser, dvr_path, hDvr, L"AppCaptureEnabled");
            jid = JournalBeginRegChange(kHiveCurrentUser, dvr_path, hDvr, L"AppCaptureEnabled",
                                        std::to_string(app_capture), "Desactivar GameDVR (AppCapture)");
        }
        const LSTATUS st = RegSetValueExW(hDvr, L"AppCaptureEnabled", 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&app_capture), sizeof(app_capture));
        if (optimize) JournalEndRegChange(jid, st == ERROR_SUCCESS);
        RegCloseKey(hDvr);
        if (!optimize) {
            if (!RegRestoreValue(kHiveCurrentUser, dvr_path, L"AppCaptureEnabled")) {
                JournalMarkRegRestored(kHiveCurrentUser, dvr_path, L"AppCaptureEnabled");
            }
        }
    } else {
        success = false;
    }

    return success;
}

} // namespace vegaflow
