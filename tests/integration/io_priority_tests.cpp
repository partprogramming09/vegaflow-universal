// Integration test: restoring a process puts its I/O priority back to Normal (IO_PRIORITY_HINT 2), not Low (1).

#include <windows.h>

#include <string>

#include "windows/change_journal.hpp"
#include "windows/process_control.hpp"
#include "windows/safe_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using NtSetInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);

// Reads ProcessIoPriority (class 33); -1 when it cannot be read.
long ReadIoPriority(HANDLE process) {
    auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    ULONG value = 0;
    if (!query || query(process, 33, &value, sizeof(value), nullptr) < 0) return -1;
    return static_cast<long>(value);
}

// Lowers the process to VeryLow (0), so each restore below has to change it.
void SetVeryLow(HANDLE process) {
    auto set = reinterpret_cast<NtSetInformationProcessFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));
    ULONG value = 0;
    if (set) set(process, 33, &value, sizeof(value));
}

} // namespace

void Test94() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"ping.exe -n 30 127.0.0.1";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {        RecordResult(false, "Test 94: restore puts I/O priority back to Normal", "could not start ping.exe");
        return;
    }
    vegaflow::WindowsProcessControl control;
    const uint64_t created = control.QueryCreationTime(pi.dwProcessId);
    SetVeryLow(pi.hProcess);
    const long lowered = ReadIoPriority(pi.hProcess);
    control.RestoreBackgroundProcess(pi.dwProcessId, created, vegaflow::ProcessAction::Throttle);
    const long after_background = ReadIoPriority(pi.hProcess);
    SetVeryLow(pi.hProcess);
    control.RestoreGameProcess(pi.dwProcessId, created, NORMAL_PRIORITY_CLASS);
    const long after_game = ReadIoPriority(pi.hProcess);

    // Crash recovery goes through the journal, not through WindowsProcessControl.
    SetVeryLow(pi.hProcess);
    vegaflow::SafeFileSystem fs;
    vegaflow::WindowsSystemUndo undo(fs);
    vegaflow::JournalEntry entry;
    entry.kind = vegaflow::JournalTargetKind::ProcessState;
    entry.pid = pi.dwProcessId;
    entry.process_creation_time = created;
    std::string err;
    const bool reverted = undo.RevertEntry(entry, &err);
    const long after_recovery = ReadIoPriority(pi.hProcess);

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    RecordResult(lowered == 0 && after_background == 2 && after_game == 2 && reverted && after_recovery == 2,
                 "Test 94: restore puts I/O priority back to Normal",
                 "lowered to: " + std::to_string(lowered) + ", after background restore: " + std::to_string(after_background)
                     + ", after game restore: " + std::to_string(after_game) + ", after crash recovery: "
                     + std::to_string(after_recovery) + (reverted ? "" : " (revert failed: " + err + ")")
                     + " (2 = Normal, 1 = Low)");
}

} // namespace vftest
