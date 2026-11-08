#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

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

bool GraphicsOptimizer::SetDynamicPrioritySeparation(uint32_t value, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hKey = nullptr;
    LSTATUS status = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl",
        0,
        KEY_READ | KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring prio_path = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
    const bool is_default = (value == 0x02);
    if (!is_default) RegBackupValue(kHiveLocalMachine, prio_path, hKey, L"Win32PrioritySeparation");
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
    if (is_default) RegRestoreValue(kHiveLocalMachine, prio_path, L"Win32PrioritySeparation");
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
        KEY_READ | KEY_SET_VALUE,
        &hKey
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring mm_path = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";
    if (enable) {
        RegBackupValue(kHiveLocalMachine, mm_path, hKey, L"DisablePagingExecutive");
        RegBackupValue(kHiveLocalMachine, mm_path, hKey, L"LargeSystemCache");
    }
    DWORD disable_paging = enable ? 1 : 0;
    DWORD large_cache = 0; // Prevents the system file cache from displacing game memory

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
    if (!enable) {
        RegRestoreValue(kHiveLocalMachine, mm_path, L"DisablePagingExecutive");
        RegRestoreValue(kHiveLocalMachine, mm_path, L"LargeSystemCache");
    }
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

} // namespace vegaflow
