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

void Test102() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"ping.exe -n 30 127.0.0.1";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {        RecordResult(false, "Test 102: restore puts I/O priority back to Normal", "could not start ping.exe");
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
                 "Test 102: restore puts I/O priority back to Normal",
                 "lowered to: " + std::to_string(lowered) + ", after background restore: " + std::to_string(after_background)
                     + ", after game restore: " + std::to_string(after_game) + ", after crash recovery: "
                     + std::to_string(after_recovery) + (reverted ? "" : " (revert failed: " + err + ")")
                     + " (2 = Normal, 1 = Low)");
}

// F1.4: giving a process back hands EcoQoS and the timer resolution to Windows (ControlMask 0), it does not force them off.
void Test124() {
    auto read_mask = [](HANDLE process, DWORD& control, DWORD& state) {
        PROCESS_POWER_THROTTLING_STATE s{};
        s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        const bool ok = GetProcessInformation(process, ProcessPowerThrottling, &s, sizeof(s)) != FALSE;
        control = s.ControlMask;
        state = s.StateMask;
        return ok;
    };
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"ping.exe -n 30 127.0.0.1";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        RecordResult(false, "Test 124: giving a process back leaves EcoQoS to Windows", "could not start ping.exe");
        return;
    }
    vegaflow::WindowsProcessControl control;
    const uint64_t created = control.QueryCreationTime(pi.dwProcessId);
    DWORD c0 = 9, s0 = 9, c1 = 9, s1 = 9, c2 = 9, s2 = 9, c3 = 9, s3 = 9;
    const bool read0 = read_mask(pi.hProcess, c0, s0);  // untouched: Windows decides

    const auto applied = control.ApplyBackgroundPolicy(pi.dwProcessId, created, vegaflow::ProcessAction::EcoOnly, {}, false);
    const bool read1 = read_mask(pi.hProcess, c1, s1);
    const bool restored_bg = control.RestoreBackgroundProcess(pi.dwProcessId, created, vegaflow::ProcessAction::EcoOnly);
    const bool read2 = read_mask(pi.hProcess, c2, s2);

    vegaflow::GameBoostOutcome boost{};
    uint32_t orig = 0;
    control.BoostGameProcess(pi.dwProcessId, created, false, {}, boost, orig, false);
    DWORD cb = 9, sb = 9;
    const bool readb = read_mask(pi.hProcess, cb, sb);
    control.RestoreGameProcess(pi.dwProcessId, created, orig);
    const bool read3 = read_mask(pi.hProcess, c3, s3);

    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    const bool applied_ok = applied.modified && read1 && c1 == 1 && s1 == 1;
    const bool bg_back = restored_bg && read2 && c2 == 0 && s2 == 0;
    const bool boosted = readb && cb == 5 && sb == 0;
    const bool game_back = read3 && c3 == 0 && s3 == 0;
    RecordResult(read0 && c0 == 0 && applied_ok && bg_back && boosted && game_back,
                 "Test 124: giving a process back leaves EcoQoS to Windows",
                 "start c/s=" + std::to_string(c0) + "/" + std::to_string(s0) + ", eco c/s=" + std::to_string(c1) + "/" + std::to_string(s1) +
                     ", after background restore c/s=" + std::to_string(c2) + "/" + std::to_string(s2) + " (0/0 = Windows decides), boosted c/s=" +
                     std::to_string(cb) + "/" + std::to_string(sb) + ", after game restore c/s=" + std::to_string(c3) + "/" + std::to_string(s3));
}

} // namespace vftest
