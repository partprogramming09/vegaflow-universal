#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

std::atomic<bool> s_high_res_active{false};

typedef LONG(NTAPI* pfnNtQueryTimerResolution)(PULONG MinimumResolution, PULONG MaximumResolution, PULONG CurrentResolution);
typedef LONG(NTAPI* pfnNtSetTimerResolution)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

NTSTATUS QueryTimerRes(PULONG min_res, PULONG max_res, PULONG cur_res) {
    static pfnNtQueryTimerResolution pfn = reinterpret_cast<pfnNtQueryTimerResolution>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
    if (pfn) {
        return pfn(min_res, max_res, cur_res);
    }
    return -1;
}

NTSTATUS SetTimerRes(ULONG desired, BOOLEAN set, PULONG cur_res) {
    static pfnNtSetTimerResolution pfn = reinterpret_cast<pfnNtSetTimerResolution>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution"));
    if (pfn) {
        return pfn(desired, set, cur_res);
    }
    return -1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

} // namespace

TimerResolutionState GraphicsOptimizer::QueryTimerResolution() {
    TimerResolutionState state{};
    ULONG min_res = 0;
    ULONG max_res = 0;
    ULONG cur_res = 0;

    NTSTATUS status = QueryTimerRes(&min_res, &max_res, &cur_res);
    if (NT_SUCCESS(status)) {
        state.minimum_ns100 = min_res;
        state.maximum_ns100 = max_res;
        state.current_ns100 = cur_res;
        state.active_high_res = s_high_res_active.load() || (cur_res < min_res) || (cur_res <= 10000);
    }

    return state;
}

bool GraphicsOptimizer::EnableHighPrecisionTimer(ULONG desired_ns100) {
    PROCESS_POWER_THROTTLING_STATE powerThrottling{};
    powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                  PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    powerThrottling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));

    ULONG cur_res = 0;
    NTSTATUS status = SetTimerRes(desired_ns100, TRUE, &cur_res);
    if (NT_SUCCESS(status)) {
        s_high_res_active.store(true);
        return true;
    }
    return false;
}

bool GraphicsOptimizer::RestoreDefaultTimer() {
    ULONG cur_res = 0;
    NTSTATUS status = SetTimerRes(5000, FALSE, &cur_res);
    if (NT_SUCCESS(status)) {
        s_high_res_active.store(false);
        return true;
    }
    return false;
}

} // namespace vegaflow
