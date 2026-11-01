#include "cpu_orchestrator.hpp"

#include <algorithm>

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
#ifndef MEMORY_PRIORITY_MEDIUM
#define MEMORY_PRIORITY_MEDIUM 3
#endif
#ifndef MEMORY_PRIORITY_BELOW_NORMAL
#define MEMORY_PRIORITY_BELOW_NORMAL 4
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

} // anonymous namespace

CpuOrchestrator::~CpuOrchestrator() {
    RestoreAll(false);
}

CpuOptimizationResult CpuOrchestrator::ApplyOptimizations(
    uint32_t game_pid,
    uint32_t presenter_thread_id,
    bool is_dx12_or_vulkan,
    const std::vector<ULONG>& game_cpu_sets,
    const std::vector<ULONG>& background_cpu_sets,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    CpuOptimizationResult result{};

    if (game_pid != 0) {
        if (!dry_run && game_pid == last_optimized_game_pid_ && !game_cpu_sets.empty()) {
            result.game_handle_full_access = last_game_had_full_access_;
            result.power_throttling_disabled = last_power_throttling_disabled_;
            result.io_priority_elevated = last_io_priority_elevated_;
            result.memory_priority_elevated = last_memory_priority_elevated_;
            result.game_cpu_sets_applied = last_game_cpu_sets_applied_;
        } else {
            // 1. Attempt full access first; Anti-Cheat (EAC, BattlEye, Vanguard) may deny PROCESS_SET_INFORMATION
            HANDLE hGame = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
                FALSE,
                game_pid);

            if (hGame != nullptr) {
                result.game_handle_full_access = true;
            } else {
                // Graceful degradation: request read-only telemetry handle without crashing or causing fatal errors
                result.game_handle_full_access = false;
                hGame = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, game_pid);
            }

            if (hGame != nullptr) {
                if (result.game_handle_full_access) {
                    if (dry_run) {
                        result.power_throttling_disabled = true;
                        result.memory_priority_elevated = true;
                        result.io_priority_elevated = true;
                        if (!game_cpu_sets.empty()) {
                            result.game_cpu_sets_applied = true;
                        }
                    } else {
                        // Priority class: ABOVE_NORMAL for DX12/Vulkan (avoids worker-thread starvation of OS/audio),
                        // HIGH for DX11 (render-thread centric).
                        DWORD priorityClass = is_dx12_or_vulkan ? ABOVE_NORMAL_PRIORITY_CLASS : HIGH_PRIORITY_CLASS;
                        SetPriorityClass(hGame, priorityClass);

                        // Disable Power Throttling
                        PROCESS_POWER_THROTTLING_STATE powerThrottling{};
                        powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                        powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                                      PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
                        powerThrottling.StateMask = 0; // Clear throttle bits
                        if (SetProcessInformation(hGame, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling))) {
                            result.power_throttling_disabled = true;
                        }

                        // Elevate Memory Priority to Normal (5)
                        MEMORY_PRIORITY_INFORMATION memPriority{};
                        memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
                        if (SetProcessInformation(hGame, ProcessMemoryPriority, &memPriority, sizeof(memPriority))) {
                            result.memory_priority_elevated = true;
                        }

                        // Elevate I/O Priority to High (2) via NT API
                        static auto pfnNtSetInformationProcess = reinterpret_cast<PFN_NtSetInformationProcess>(
                            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));

                        if (pfnNtSetInformationProcess) {
                            ULONG io_prio = 2; // IoPriorityHigh
                            NTSTATUS status = pfnNtSetInformationProcess(
                                hGame,
                                ProcessIoPriorityInformationClass,
                                &io_prio,
                                sizeof(io_prio));
                            if (NT_SUCCESS(status)) {
                                result.io_priority_elevated = true;
                            }
                        }

                        // Apply Game CPU Sets
                        if (!game_cpu_sets.empty()) {
                            if (SetProcessDefaultCpuSets(
                                    hGame,
                                    game_cpu_sets.data(),
                                    static_cast<ULONG>(game_cpu_sets.size()))) {
                                result.game_cpu_sets_applied = true;
                            }
                        }
                    }
                }
                CloseHandle(hGame);
            }

            if (!dry_run) {
                last_optimized_game_pid_ = game_pid;
                last_game_had_full_access_ = result.game_handle_full_access;
                last_power_throttling_disabled_ = result.power_throttling_disabled;
                last_io_priority_elevated_ = result.io_priority_elevated;
                last_memory_priority_elevated_ = result.memory_priority_elevated;
                last_game_cpu_sets_applied_ = result.game_cpu_sets_applied;
            }
        }
    }

    // 2. Protect Presenter Thread for modern low-level graphics APIs during PSO compilation spikes
    if (is_dx12_or_vulkan && presenter_thread_id != 0) {
        if (dry_run) {
            result.presenter_thread_boosted = true;
        } else if (presenter_thread_id == last_presenter_tid_) {
            result.presenter_thread_boosted = last_presenter_boosted_;
        } else {
            HANDLE hThread = OpenThread(
                THREAD_SET_LIMITED_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION,
                FALSE,
                presenter_thread_id);
            if (!hThread) {
                hThread = OpenThread(THREAD_SET_INFORMATION, FALSE, presenter_thread_id);
            }
            if (hThread) {
                if (SetThreadPriority(hThread, THREAD_PRIORITY_ABOVE_NORMAL)) {
                    result.presenter_thread_boosted = true;
                }
                CloseHandle(hThread);
            }
            last_presenter_tid_ = presenter_thread_id;
            last_presenter_boosted_ = result.presenter_thread_boosted;
        }
    }

    // 3. Background EcoQoS Throttling, I/O & Memory Priority Confinement, and CPU Sets
    if (!background_pids.empty()) {
        uint32_t current_pid = GetCurrentProcessId();
        static auto pfnNtSetInformationProcess = reinterpret_cast<PFN_NtSetInformationProcess>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));

        for (uint32_t bg_pid : background_pids) {
            if (bg_pid == 0 || bg_pid == game_pid || bg_pid == current_pid) {
                continue;
            }
            if (std::find(modified_background_pids_.begin(), modified_background_pids_.end(), bg_pid) != modified_background_pids_.end()) {
                if (!background_cpu_sets.empty()) {
                    result.background_processes_isolated++;
                }
                result.background_processes_throttled++;
                result.background_io_deprioritized = true;
                result.background_memory_deprioritized = true;
                continue;
            }

            if (dry_run) {
                HANDLE hBg = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
                    FALSE,
                    bg_pid);
                if (!hBg) {
                    hBg = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, bg_pid);
                }
                if (hBg) {
                    result.background_processes_throttled++;
                    result.background_io_deprioritized = true;
                    result.background_memory_deprioritized = true;
                    if (!background_cpu_sets.empty()) {
                        result.background_processes_isolated++;
                    }
                    CloseHandle(hBg);
                }
                continue;
            }

            HANDLE hBg = OpenProcess(
                PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                bg_pid);
            if (!hBg) {
                hBg = OpenProcess(
                    PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    bg_pid);
            }

            if (hBg) {
                bool modified = false;

                // 3.1. Enable EcoQoS (Power Throttling: Execution Speed cap for efficiency cores)
                PROCESS_POWER_THROTTLING_STATE powerThrottling{};
                powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                powerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                if (SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling))) {
                    result.background_processes_throttled++;
                    modified = true;
                }

                // 3.2. Confine Memory Priority to MEMORY_PRIORITY_LOW (2)
                MEMORY_PRIORITY_INFORMATION memPriority{};
                memPriority.MemoryPriority = MEMORY_PRIORITY_LOW; // 2
                if (SetProcessInformation(hBg, ProcessMemoryPriority, &memPriority, sizeof(memPriority))) {
                    result.background_memory_deprioritized = true;
                    modified = true;
                }

                // 3.3. Confine I/O Priority to IoPriorityVeryLow (0) via NT API
                if (pfnNtSetInformationProcess) {
                    ULONG io_prio = 0; // IoPriorityVeryLow
                    NTSTATUS status = pfnNtSetInformationProcess(
                        hBg,
                        ProcessIoPriorityInformationClass,
                        &io_prio,
                        sizeof(io_prio));
                    if (NT_SUCCESS(status)) {
                        result.background_io_deprioritized = true;
                        modified = true;
                    }
                }

                // 3.4. Confine to Background CPU Sets
                if (!background_cpu_sets.empty()) {
                    if (SetProcessDefaultCpuSets(
                            hBg,
                            background_cpu_sets.data(),
                            static_cast<ULONG>(background_cpu_sets.size()))) {
                        result.background_processes_isolated++;
                        modified = true;
                    }
                }

                if (modified) {
                    modified_background_pids_.push_back(bg_pid);
                }

                CloseHandle(hBg);
            }
        }
    }

    return result;
}

void CpuOrchestrator::RestoreAll(bool dry_run) {
    if (!dry_run) {
        static auto pfnNtSetInformationProcess = reinterpret_cast<PFN_NtSetInformationProcess>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess")));

        if (last_optimized_game_pid_ != 0) {
            HANDLE hGame = OpenProcess(
                PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                last_optimized_game_pid_);
            if (!hGame) {
                hGame = OpenProcess(PROCESS_SET_INFORMATION, FALSE, last_optimized_game_pid_);
            }
            if (hGame) {
                SetProcessDefaultCpuSets(hGame, nullptr, 0);
                SetPriorityClass(hGame, NORMAL_PRIORITY_CLASS);

                // Restore game memory priority to normal (5)
                MEMORY_PRIORITY_INFORMATION memPriority{};
                memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
                SetProcessInformation(hGame, ProcessMemoryPriority, &memPriority, sizeof(memPriority));

                // Restore game I/O priority to normal (1)
                if (pfnNtSetInformationProcess) {
                    ULONG io_prio = 1; // Normal
                    pfnNtSetInformationProcess(
                        hGame,
                        ProcessIoPriorityInformationClass,
                        &io_prio,
                        sizeof(io_prio));
                }

                CloseHandle(hGame);
            }
        }
        last_optimized_game_pid_ = 0;
        last_presenter_tid_ = 0;
        last_game_had_full_access_ = false;
        last_power_throttling_disabled_ = false;
        last_io_priority_elevated_ = false;
        last_memory_priority_elevated_ = false;
        last_game_cpu_sets_applied_ = false;
        last_presenter_boosted_ = false;

        for (uint32_t bg_pid : modified_background_pids_) {
            HANDLE hBg = OpenProcess(
                PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                bg_pid);
            if (!hBg) {
                hBg = OpenProcess(
                    PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    bg_pid);
            }
            if (hBg) {
                // 1. Reset CPU Sets
                SetProcessDefaultCpuSets(hBg, nullptr, 0);

                // 2. Restore Power Throttling (clear EcoQoS StateMask = 0)
                PROCESS_POWER_THROTTLING_STATE powerThrottling{};
                powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                powerThrottling.StateMask = 0;
                SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));

                // 3. Restore Memory Priority to Normal (5)
                MEMORY_PRIORITY_INFORMATION memPriority{};
                memPriority.MemoryPriority = MEMORY_PRIORITY_NORMAL;
                SetProcessInformation(hBg, ProcessMemoryPriority, &memPriority, sizeof(memPriority));

                // 4. Restore I/O Priority to Normal (1) via NT API
                if (pfnNtSetInformationProcess) {
                    ULONG io_prio = 1; // Normal
                    pfnNtSetInformationProcess(
                        hBg,
                        ProcessIoPriorityInformationClass,
                        &io_prio,
                        sizeof(io_prio));
                }

                CloseHandle(hBg);
            }
        }
        modified_background_pids_.clear();
    }
}

} // namespace vegaflow
