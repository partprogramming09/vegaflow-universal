#include "windows/process_control.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vegaflow {

namespace {

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

constexpr ULONG ProcessIoPriorityInformationClass = 33;

typedef NTSTATUS(NTAPI* PFN_NtSetInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength
);

#ifndef MEMORY_PRIORITY_VERY_LOW
#define MEMORY_PRIORITY_VERY_LOW 1
#endif
#ifndef MEMORY_PRIORITY_LOW
#define MEMORY_PRIORITY_LOW 2
#endif
#ifndef MEMORY_PRIORITY_NORMAL
#define MEMORY_PRIORITY_NORMAL 5
#endif

#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#endif
#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

PFN_NtSetInformationProcess GetNtSetInformationProcess() {
    static auto pfn = reinterpret_cast<PFN_NtSetInformationProcess>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));
    return pfn;
}

uint64_t FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

bool HandleMatchesCreationTime(HANDLE h, uint64_t expected_creation_time) {
    if (!h) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        return expected_creation_time == 0;
    }
    if (expected_creation_time == 0) {
        return true;
    }
    return FileTimeToU64(created) == expected_creation_time;
}

std::vector<ULONG> ToUlongSets(const std::vector<uint32_t>& sets) {
    std::vector<ULONG> out;
    out.reserve(sets.size());
    for (uint32_t id : sets) out.push_back(static_cast<ULONG>(id));
    return out;
}

} // namespace

uint32_t WindowsProcessControl::CurrentPid() const {
    return GetCurrentProcessId();
}

uint64_t WindowsProcessControl::TickCountMs() const {
    return GetTickCount64();
}

uint64_t WindowsProcessControl::QueryCreationTime(uint32_t pid) const {
    if (pid == 0 || pid == 4) return 0;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    uint64_t ct = 0;
    if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        ct = FileTimeToU64(created);
    }
    CloseHandle(h);
    return ct;
}

bool WindowsProcessControl::QueryCpuTime100ns(
    uint32_t pid,
    uint64_t expected_creation_time,
    uint64_t& out_total_100ns) const {

    out_total_100ns = 0;
    if (pid == 0 || pid == 4) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    bool ok = false;
    if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        if (expected_creation_time == 0 || FileTimeToU64(created) == expected_creation_time) {
            out_total_100ns = FileTimeToU64(kernel) + FileTimeToU64(user);
            ok = true;
        }
    }
    CloseHandle(h);
    return ok;
}

bool WindowsProcessControl::VerifyProcessIdentity(uint32_t pid, uint64_t expected_creation_time) const {
    if (pid == 0 || pid == 4) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exit_code = 0;
    bool alive = (GetExitCodeProcess(h, &exit_code) != FALSE && exit_code == STILL_ACTIVE);
    bool match = alive && HandleMatchesCreationTime(h, expected_creation_time);
    CloseHandle(h);
    return match;
}

bool WindowsProcessControl::BoostGameProcess(
    uint32_t pid,
    uint64_t expected_creation_time,
    bool is_dx12_or_vulkan,
    const std::vector<uint32_t>& game_cpu_sets,
    GameBoostOutcome& out,
    uint32_t& out_orig_priority_class,
    bool dry_run) {

    out = GameBoostOutcome{};
    out_orig_priority_class = NORMAL_PRIORITY_CLASS;
    if (pid == 0 || pid == 4) return false;

    HANDLE hGame = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
    if (hGame != nullptr) {
        out.full_access = true;
    } else {
        out.full_access = false;
        hGame = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (!hGame) return false;

    if (!HandleMatchesCreationTime(hGame, expected_creation_time)) {
        CloseHandle(hGame);
        out = GameBoostOutcome{};
        return false;
    }

    if (out.full_access) {
        const DWORD orig = GetPriorityClass(hGame);
        out_orig_priority_class = (orig != 0) ? orig : NORMAL_PRIORITY_CLASS;

        if (dry_run) {
            out.power_throttling_disabled = true;
            out.memory_priority_elevated = true;
            out.io_priority_elevated = true;
            out.cpu_sets_applied = !game_cpu_sets.empty();
        } else {
            DWORD priorityClass = is_dx12_or_vulkan ? ABOVE_NORMAL_PRIORITY_CLASS : HIGH_PRIORITY_CLASS;
            SetPriorityClass(hGame, priorityClass);

            PROCESS_POWER_THROTTLING_STATE powerThrottling{};
            powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                          PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
            powerThrottling.StateMask = 0;
            if (SetProcessInformation(hGame, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling))) {
                out.power_throttling_disabled = true;
            }

            MEMORY_PRIORITY_INFORMATION memPriority{};
            memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
            if (SetProcessInformation(hGame, ProcessMemoryPriority, &memPriority, sizeof(memPriority))) {
                out.memory_priority_elevated = true;
            }

            if (auto pfn = GetNtSetInformationProcess()) {
                ULONG io_prio = 2; // IoPriorityHigh
                if (NT_SUCCESS(pfn(hGame, ProcessIoPriorityInformationClass, &io_prio, sizeof(io_prio)))) {
                    out.io_priority_elevated = true;
                }
            }

            if (!game_cpu_sets.empty()) {
                auto sets = ToUlongSets(game_cpu_sets);
                if (SetProcessDefaultCpuSets(hGame, sets.data(), static_cast<ULONG>(sets.size()))) {
                    out.cpu_sets_applied = true;
                }
            }
        }
    }

    CloseHandle(hGame);
    return true;
}

bool WindowsProcessControl::BoostPresenterThread(uint32_t thread_id, bool dry_run) {
    if (thread_id == 0) return false;
    if (dry_run) return true;
    HANDLE hThread = OpenThread(THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
    if (!hThread) {
        hThread = OpenThread(THREAD_SET_INFORMATION, FALSE, thread_id);
    }
    if (!hThread) return false;
    const bool ok = (SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL) != FALSE);
    CloseHandle(hThread);
    return ok;
}

bool WindowsProcessControl::RestoreGameProcess(
    uint32_t pid,
    uint64_t expected_creation_time,
    uint32_t orig_priority_class) {

    if (pid == 0 || pid == 4) return false;
    HANDLE hGame = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hGame) return false;

    if (!HandleMatchesCreationTime(hGame, expected_creation_time)) {
        CloseHandle(hGame);
        return false;
    }

    SetProcessDefaultCpuSets(hGame, nullptr, 0);
    SetPriorityClass(hGame, orig_priority_class != 0 ? orig_priority_class : NORMAL_PRIORITY_CLASS);

    MEMORY_PRIORITY_INFORMATION memPriority{};
    memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
    SetProcessInformation(hGame, ProcessMemoryPriority, &memPriority, sizeof(memPriority));

    if (auto pfn = GetNtSetInformationProcess()) {
        ULONG io_prio = 1; // IoPriorityNormal
        pfn(hGame, ProcessIoPriorityInformationClass, &io_prio, sizeof(io_prio));
    }

    CloseHandle(hGame);
    return true;
}

BackgroundApplyOutcome WindowsProcessControl::ApplyBackgroundPolicy(
    uint32_t pid,
    uint64_t expected_creation_time,
    ProcessAction action,
    const std::vector<uint32_t>& bg_cpu_sets,
    bool dry_run) {

    BackgroundApplyOutcome out{};
    if (pid == 0 || pid == 4 ||
        (action != ProcessAction::EcoOnly && action != ProcessAction::Throttle)) {
        return out;
    }

    if (dry_run) {
        HANDLE hBg = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hBg) hBg = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hBg) return out;
        const bool ok_id = HandleMatchesCreationTime(hBg, expected_creation_time);
        CloseHandle(hBg);
        if (!ok_id) return out;

        out.modified = true;
        out.power_throttling_applied = true;
        if (action == ProcessAction::Throttle) {
            out.memory_deprioritized = true;
            out.io_deprioritized = true;
            out.cpu_sets_applied = !bg_cpu_sets.empty();
        }
        return out;
    }

    HANDLE hBg = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hBg) {
        hBg = OpenProcess(PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (!hBg) return out;

    if (!HandleMatchesCreationTime(hBg, expected_creation_time)) {
        CloseHandle(hBg);
        return out;
    }

    PROCESS_POWER_THROTTLING_STATE powerThrottling{};
    powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    powerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    if (SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling))) {
        out.power_throttling_applied = true;
        out.modified = true;
    }

    if (action == ProcessAction::Throttle) {
        MEMORY_PRIORITY_INFORMATION memPriority{};
        memPriority.MemoryPriority = MEMORY_PRIORITY_LOW;
        if (SetProcessInformation(hBg, ProcessMemoryPriority, &memPriority, sizeof(memPriority))) {
            out.memory_deprioritized = true;
            out.modified = true;
        }

        if (auto pfn = GetNtSetInformationProcess()) {
            ULONG io_prio = 0; // IoPriorityVeryLow
            if (NT_SUCCESS(pfn(hBg, ProcessIoPriorityInformationClass, &io_prio, sizeof(io_prio)))) {
                out.io_deprioritized = true;
                out.modified = true;
            }
        }

        if (!bg_cpu_sets.empty()) {
            auto sets = ToUlongSets(bg_cpu_sets);
            if (SetProcessDefaultCpuSets(hBg, sets.data(), static_cast<ULONG>(sets.size()))) {
                out.cpu_sets_applied = true;
                out.modified = true;
            }
        }
    }

    CloseHandle(hBg);
    return out;
}

bool WindowsProcessControl::UpdateBackgroundCpuSets(
    uint32_t pid,
    uint64_t expected_creation_time,
    const std::vector<uint32_t>& bg_cpu_sets) {

    if (pid == 0 || pid == 4) return false;
    HANDLE h = OpenProcess(PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    bool ok = false;
    if (HandleMatchesCreationTime(h, expected_creation_time)) {
        if (bg_cpu_sets.empty()) {
            ok = (SetProcessDefaultCpuSets(h, nullptr, 0) != FALSE);
        } else {
            auto sets = ToUlongSets(bg_cpu_sets);
            ok = (SetProcessDefaultCpuSets(h, sets.data(), static_cast<ULONG>(sets.size())) != FALSE);
        }
    }
    CloseHandle(h);
    return ok;
}

bool WindowsProcessControl::RestoreBackgroundProcess(
    uint32_t pid,
    uint64_t expected_creation_time,
    ProcessAction applied_action) {

    if (pid == 0 || pid == 4 ||
        (applied_action != ProcessAction::EcoOnly && applied_action != ProcessAction::Throttle)) {
        return false;
    }

    HANDLE hBg = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hBg) {
        hBg = OpenProcess(PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (!hBg) return false;

    if (!HandleMatchesCreationTime(hBg, expected_creation_time)) {
        CloseHandle(hBg);
        return false;
    }

    PROCESS_POWER_THROTTLING_STATE powerThrottling{};
    powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    powerThrottling.StateMask = 0;
    SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));

    if (applied_action == ProcessAction::Throttle) {
        SetProcessDefaultCpuSets(hBg, nullptr, 0);

        MEMORY_PRIORITY_INFORMATION memPriority{};
        memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
        SetProcessInformation(hBg, ProcessMemoryPriority, &memPriority, sizeof(memPriority));

        if (auto pfn = GetNtSetInformationProcess()) {
            ULONG io_prio = 1; // IoPriorityNormal
            pfn(hBg, ProcessIoPriorityInformationClass, &io_prio, sizeof(io_prio));
        }
    }

    CloseHandle(hBg);
    return true;
}

} // namespace vegaflow
