#include "optimize/graphics/graphics_internal.hpp"
#include "windows/change_journal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool GraphicsOptimizer::SetMemoryCompressionEnabled(bool enable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    ChangeJournal* journal = GetActiveChangeJournal();
    uint64_t jid = 0;
    if (journal) {
        const bool was_enabled = QuerySystemGraphicsTweaks().memory_compression_enabled;
        if (!enable) {
            JournalEntry e;
            e.kind = JournalTargetKind::MemoryCompression;
            e.target = "MMAgent\\MemoryCompression";
            e.reason = "Desactivar compresión de memoria";
            e.before_value = was_enabled ? "Activada" : "Desactivada";
            e.after_value = "Desactivada";
            e.manual_fix_hint = "powershell -Command \"Enable-MMAgent -MemoryCompression\"";
            e.orig_mem_compression_enabled = was_enabled;
            jid = journal->RecordPending(std::move(e));
        }
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
        if (journal && jid != 0) journal->MarkFailed(jid, "No se pudo lanzar PowerShell");
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

    const bool ok = (wait_res == WAIT_OBJECT_0 && exit_code == 0);
    if (journal) {
        if (enable && ok) {
            journal->MarkMatchingReverted(JournalTargetKind::MemoryCompression, "MMAgent\\MemoryCompression");
        } else if (jid != 0) {
            if (ok) journal->MarkCommitted(jid);
            else journal->MarkFailed(jid, "MMAgent devolvió error");
        }
    }
    return ok;
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
    uint64_t jid = 0;
    if (!is_default) {
        RegBackupValue(kHiveLocalMachine, prio_path, hKey, L"Win32PrioritySeparation");
        char hexbuf[16];
        std::snprintf(hexbuf, sizeof(hexbuf), "0x%x", value);
        jid = JournalBeginRegChange(kHiveLocalMachine, prio_path, hKey, L"Win32PrioritySeparation",
                                    hexbuf, "Prioridad de quantum CPU en juego");
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
    if (!is_default) JournalEndRegChange(jid, set_status == ERROR_SUCCESS);

    RegCloseKey(hKey);
    if (is_default) {
        if (!RegRestoreValue(kHiveLocalMachine, prio_path, L"Win32PrioritySeparation") && set_status == ERROR_SUCCESS) {
            JournalMarkRegRestored(kHiveLocalMachine, prio_path, L"Win32PrioritySeparation");
        }
    }
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
    uint64_t jid1 = 0, jid2 = 0;
    if (enable) {
        RegBackupValue(kHiveLocalMachine, mm_path, hKey, L"DisablePagingExecutive");
        RegBackupValue(kHiveLocalMachine, mm_path, hKey, L"LargeSystemCache");
        jid1 = JournalBeginRegChange(kHiveLocalMachine, mm_path, hKey, L"DisablePagingExecutive", "1", "Blindaje de memoria del kernel");
        jid2 = JournalBeginRegChange(kHiveLocalMachine, mm_path, hKey, L"LargeSystemCache", "0", "Evitar desplazamiento por caché");
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
    if (enable) {
        JournalEndRegChange(jid1, st1 == ERROR_SUCCESS);
        JournalEndRegChange(jid2, st2 == ERROR_SUCCESS);
    }

    RegCloseKey(hKey);
    if (!enable) {
        if (!RegRestoreValue(kHiveLocalMachine, mm_path, L"DisablePagingExecutive") && st1 == ERROR_SUCCESS) {
            JournalMarkRegRestored(kHiveLocalMachine, mm_path, L"DisablePagingExecutive");
        }
        if (!RegRestoreValue(kHiveLocalMachine, mm_path, L"LargeSystemCache") && st2 == ERROR_SUCCESS) {
            JournalMarkRegRestored(kHiveLocalMachine, mm_path, L"LargeSystemCache");
        }
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
