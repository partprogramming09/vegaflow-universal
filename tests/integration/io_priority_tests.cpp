// Integration test: restoring a process puts its I/O priority back to Normal (IO_PRIORITY_HINT 2), not Low (1).

#include <windows.h>

#include <string>

#include "windows/process_control.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

// Reads ProcessIoPriority (class 33); -1 when it cannot be read.
long ReadIoPriority(HANDLE process) {
    auto query = reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    ULONG value = 0;
    if (!query || query(process, 33, &value, sizeof(value), nullptr) < 0) return -1;
    return static_cast<long>(value);
}

} // namespace

void Test94() {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"ping.exe -n 30 127.0.0.1";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        RecordResult(false, "Test 94: restore puts I/O priority back to Normal", "could not start ping.exe");
        return;
    }
    vegaflow::WindowsProcessControl control;
    const uint64_t created = control.QueryCreationTime(pi.dwProcessId);
    control.RestoreBackgroundProcess(pi.dwProcessId, created, vegaflow::ProcessAction::Throttle);
    const long after_background = ReadIoPriority(pi.hProcess);
    control.RestoreGameProcess(pi.dwProcessId, created, NORMAL_PRIORITY_CLASS);
    const long after_game = ReadIoPriority(pi.hProcess);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    RecordResult(after_background == 2 && after_game == 2, "Test 94: restore puts I/O priority back to Normal",
                 "after background restore: " + std::to_string(after_background) + ", after game restore: "
                     + std::to_string(after_game) + " (2 = Normal, 1 = Low)");
}

} // namespace vftest
