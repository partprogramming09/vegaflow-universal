#include "memory_manager.hpp"
#include "rust_bridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

namespace {

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

constexpr ULONG SystemMemoryListInformationClass = 80;
constexpr ULONG MemoryPurgeStandbyListCommand = 4;
constexpr const wchar_t* SE_PROFILE_SINGLE_PROCESS = L"SeProfileSingleProcessPrivilege";
constexpr const wchar_t* SE_INCREASE_QUOTA = L"SeIncreaseQuotaPrivilege";

typedef NTSTATUS(NTAPI* PFN_NtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS(NTAPI* PFN_NtSetSystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
);

typedef HRESULT(WINAPI* PFN_CreateDXGIFactory1)(REFIID riid, void** ppFactory);

bool EnablePrivilege(LPCWSTR privilegeName) {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid)) {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(hToken);

    return (ok && err != ERROR_NOT_ALL_ASSIGNED);
}

} // anonymous namespace

MemoryManager::MemoryManager(const MemoryPurgeConfig& config)
    : config_(config) {
    // Acquire privileges required for Standby List queries and purges
    EnablePrivilege(SE_PROFILE_SINGLE_PROCESS);
    EnablePrivilege(SE_INCREASE_QUOTA);
}

SystemMemoryTelemetry MemoryManager::SampleTelemetry() const {
    SystemMemoryTelemetry telemetry{};

    // 1. Physical RAM via GlobalMemoryStatusEx
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        telemetry.total_physical_bytes = memStatus.ullTotalPhys;
    }

    // 2. Exact memory list information via Rust Core (NtQuerySystemInformation)
    bool queriedNt = false;
    VfNtMemoryListSample ntSample = vf_rust_query_nt_memory_lists();
    if (ntSample.valid) {
        telemetry.free_and_zero_bytes = ntSample.free_and_zero_bytes;
        telemetry.standby_bytes = ntSample.standby_bytes;
        telemetry.modified_bytes = ntSample.modified_bytes;
        queriedNt = true;
    }

    if (!queriedNt) {
        // Fallback if NtQuerySystemInformation fails or privileges are denied
        telemetry.free_and_zero_bytes = memStatus.ullAvailPhys;
        telemetry.standby_bytes = 0;
        telemetry.modified_bytes = 0;
    }

    // 3. VRAM Telemetry via DXGI
    static HMODULE s_hDxgi = LoadLibraryW(L"dxgi.dll");
    if (s_hDxgi) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        static auto s_pfnCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(
            GetProcAddress(s_hDxgi, "CreateDXGIFactory1"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

        if (s_pfnCreateDXGIFactory1) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(s_pfnCreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
                UINT adapterIndex = 0;
                IDXGIAdapter1* adapter = nullptr;
                while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
                    DXGI_ADAPTER_DESC1 desc{};
                    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                        // Skip software renderers (like Microsoft Basic Render Driver)
                        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                            IDXGIAdapter3* adapter3 = nullptr;
                            if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3))) && adapter3) {
                                DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
                                if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo))) {
                                    telemetry.vram.local_budget_bytes = localInfo.Budget;
                                    telemetry.vram.local_usage_bytes = localInfo.CurrentUsage;
                                    telemetry.vram.valid = true;
                                }

                                DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
                                if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo))) {
                                    telemetry.vram.non_local_budget_bytes = nonLocalInfo.Budget;
                                    telemetry.vram.non_local_usage_bytes = nonLocalInfo.CurrentUsage;
                                }

                                adapter3->Release();
                                adapter->Release();
                                break; // Found primary hardware adapter
                            }
                        }
                    }
                    adapter->Release();
                    ++adapterIndex;
                }
                factory->Release();
            }
        }
    }

    return telemetry;
}

MemoryActionReport MemoryManager::EvaluateAndOptimize(
    uint32_t active_game_pid,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    MemoryActionReport report{};
    SystemMemoryTelemetry telemetry = SampleTelemetry();

    // 1. Evaluate trigger conditions
    const uint64_t min_free_bytes = config_.min_free_ram_mb * 1024ULL * 1024ULL;
    const uint64_t min_standby_bytes = config_.min_standby_mb * 1024ULL * 1024ULL;

    const bool low_free_ram = telemetry.free_and_zero_bytes < min_free_bytes;
    const bool high_vram_pressure = config_.is_uma_architecture &&
        telemetry.vram.valid &&
        (telemetry.vram.local_budget_bytes > 0) &&
        ((telemetry.vram.local_usage_bytes * 100 / telemetry.vram.local_budget_bytes) >= 85);

    const bool condition_1 = low_free_ram || high_vram_pressure;
    const bool condition_2 = telemetry.standby_bytes >= min_standby_bytes;

    if (!condition_1 || !condition_2) {
        report.reason = "Thresholds not met: Free/Zero=" +
            std::to_string(telemetry.free_and_zero_bytes / (1024 * 1024)) + "MB (min " +
            std::to_string(config_.min_free_ram_mb) + "MB), Standby=" +
            std::to_string(telemetry.standby_bytes / (1024 * 1024)) + "MB (min " +
            std::to_string(config_.min_standby_mb) + "MB)";
        return report;
    }

    // 2. Cooldown check
    auto now = std::chrono::steady_clock::now();
    if (last_purge_time_.time_since_epoch().count() > 0) {
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - last_purge_time_).count();
        if (elapsed_sec < static_cast<int64_t>(config_.cooldown_seconds)) {
            report.skipped_due_to_cooldown = true;
            report.reason = "Skipped due to cooldown (" +
                std::to_string(config_.cooldown_seconds - elapsed_sec) + "s remaining)";
            return report;
        }
    }

    // 3. Game disk read peak check
    if (active_game_pid != 0) {
        HANDLE hGame = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, active_game_pid);
        if (hGame) {
            IO_COUNTERS ioCounters{};
            if (GetProcessIoCounters(hGame, &ioCounters)) {
                uint64_t current_read_bytes = ioCounters.ReadTransferCount;
                if (last_io_sample_time_.time_since_epoch().count() > 0 && now > last_io_sample_time_) {
                    double elapsed_s = std::chrono::duration<double>(now - last_io_sample_time_).count();
                    if (elapsed_s > 0.001) {
                        uint64_t bytes_read = (current_read_bytes >= last_game_read_bytes_)
                            ? (current_read_bytes - last_game_read_bytes_) : 0;
                        uint64_t rate = static_cast<uint64_t>(bytes_read / elapsed_s);
                        if (rate > config_.max_game_disk_read_bytes_per_sec) {
                            last_game_read_bytes_ = current_read_bytes;
                            last_io_sample_time_ = now;
                            CloseHandle(hGame);
                            report.skipped_due_to_disk_io = true;
                            report.reason = "Skipped due to game disk I/O read peak: " +
                                std::to_string(rate / (1024 * 1024)) + " MB/s (max " +
                                std::to_string(config_.max_game_disk_read_bytes_per_sec / (1024 * 1024)) + " MB/s)";
                            return report;
                        }
                    }
                }
                last_game_read_bytes_ = current_read_bytes;
                last_io_sample_time_ = now;
            }
            CloseHandle(hGame);
        }
    }

    // 4. In dry-run mode, report conditions met without applying system state changes
    if (dry_run) {
        report.reason = "Dry run: purge condition met, no action taken";
        return report;
    }

    // 5. Trim working set of background processes (never the game, nor VegaFlow itself)
    uint32_t current_pid = GetCurrentProcessId();
    for (uint32_t pid : background_pids) {
        if (pid == 0 || pid == active_game_pid || pid == current_pid) {
            continue;
        }
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
        if (hProc) {
            if (K32EmptyWorkingSet(hProc)) {
                report.background_processes_trimmed++;
            }
            CloseHandle(hProc);
        }
    }

    // 6. Purge Standby List via NtSetSystemInformation
    last_purge_time_ = std::chrono::steady_clock::now();

    static auto pfnNtSetSystemInformation = reinterpret_cast<PFN_NtSetSystemInformation>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation")));

    if (pfnNtSetSystemInformation) {
        EnablePrivilege(SE_PROFILE_SINGLE_PROCESS);
        ULONG cmd = MemoryPurgeStandbyListCommand;
        NTSTATUS status = pfnNtSetSystemInformation(
            SystemMemoryListInformationClass,
            &cmd,
            sizeof(cmd));

        if (NT_SUCCESS(status)) {
            report.standby_purged = true;
            report.reason = "Standby list purged successfully and background processes trimmed";
        } else if (status == static_cast<NTSTATUS>(0xC0000061L)) {
            report.reason = "Background Working Sets trimmed (" +
                std::to_string(report.background_processes_trimmed) +
                " processes); Standby List purge skipped (requires Administrator / --elevate)";
        } else {
            report.reason = "NtSetSystemInformation failed with NTSTATUS: " + std::to_string(status);
        }
    } else {
        report.reason = "NtSetSystemInformation not available in ntdll.dll";
    }

    return report;
}

ManualPurgeReport MemoryManager::ForcePurgeNow(const std::vector<uint32_t>& background_pids, bool dry_run) {
    ManualPurgeReport report{};

    // 1. Tomar SampleTelemetry() antes
    SystemMemoryTelemetry before = SampleTelemetry();
    report.free_bytes_before = before.free_and_zero_bytes;
    report.standby_bytes_before = before.standby_bytes;

    // 2. Si dry_run == true, devolver reporte simulado
    if (dry_run) {
        report.free_bytes_after = before.free_and_zero_bytes;
        report.standby_bytes_after = before.standby_bytes;
        report.standby_purge_succeeded = true;
        report.status_message = "Dry-run: purge simulated";
        return report;
    }

    // 3. Recortar Working Set de background_pids
    uint32_t current_pid = GetCurrentProcessId();
    for (uint32_t pid : background_pids) {
        if (pid == 0 || pid == 4 || pid == current_pid) {
            continue;
        }
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
        if (hProc) {
            if (K32EmptyWorkingSet(hProc)) {
                report.trimmed_processes++;
            }
            CloseHandle(hProc);
        }
    }

    // 4. Habilitar SeProfileSingleProcessPrivilege y vaciar Standby List
    static auto pfnNtSetSystemInformation = reinterpret_cast<PFN_NtSetSystemInformation>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation")));

    if (pfnNtSetSystemInformation) {
        EnablePrivilege(SE_PROFILE_SINGLE_PROCESS);
        ULONG cmd = MemoryPurgeStandbyListCommand;
        NTSTATUS status = pfnNtSetSystemInformation(
            SystemMemoryListInformationClass,
            &cmd,
            sizeof(cmd));

        if (NT_SUCCESS(status)) {
            report.standby_purge_succeeded = true;
            last_purge_time_ = std::chrono::steady_clock::now();
            report.status_message = "Standby list purged successfully";
        } else {
            report.standby_purge_succeeded = false;
            report.status_message = "Failed to purge standby list (NTSTATUS " + std::to_string(status) +
                "). Administrator privileges required (--elevate) to purge standby list";
        }
    } else {
        report.standby_purge_succeeded = false;
        report.status_message = "NtSetSystemInformation not available in ntdll.dll. Administrator privileges required (--elevate)";
    }

    // 5. Tomar SampleTelemetry() después
    SystemMemoryTelemetry after = SampleTelemetry();
    report.free_bytes_after = after.free_and_zero_bytes;
    report.standby_bytes_after = after.standby_bytes;

    return report;
}

uint64_t MemoryManager::GetCurrentProcessPrivateWorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc))) {
        return static_cast<uint64_t>(pmc.PrivateUsage);
    }
    return 0;
}

bool MemoryManager::PurgeStandbyList(bool dryRun) {
    if (!dryRun) {
        EnablePrivilege(SE_PROFILE_SINGLE_PROCESS);
        EnablePrivilege(SE_INCREASE_QUOTA);
    }

    bool success = vf_rust_purge_nt_standby_list(dryRun);
    if (success && !dryRun) {
        K32EmptyWorkingSet(GetCurrentProcess());
    }
    return success;
}

bool MemoryManager::QueryStandbyMemoryMib(uint64_t& standbyMib, uint64_t& freeMib) {
    standbyMib = 0;
    freeMib = 0;

    VfNtMemoryListSample ntSample = vf_rust_query_nt_memory_lists();
    if (ntSample.valid) {
        standbyMib = ntSample.standby_bytes / (1024ULL * 1024ULL);
        freeMib = ntSample.free_and_zero_bytes / (1024ULL * 1024ULL);
        return true;
    }

    // Fallback: GetPerformanceInfo & GlobalMemoryStatusEx
    PERFORMANCE_INFORMATION pi{};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus)) {
            freeMib = memStatus.ullAvailPhys / (1024ULL * 1024ULL);
        } else {
            freeMib = (static_cast<uint64_t>(pi.PhysicalAvailable) * pi.PageSize) / (1024ULL * 1024ULL);
        }
        standbyMib = 0;
        return true;
    }

    return false;
}

UmaGuardStatus MemoryManager::EvaluateUmaPressureGuard(
    const SystemMemoryTelemetry& telem,
    const ApuOptimizationPolicy& policy,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    VfUmaTelemetryInput input{};
    input.total_physical_bytes = telem.total_physical_bytes;
    input.free_and_zero_bytes = telem.free_and_zero_bytes;
    input.standby_bytes = telem.standby_bytes;
    input.local_budget_bytes = telem.vram.local_budget_bytes;
    input.local_usage_bytes = telem.vram.local_usage_bytes;
    input.runtime_pressure_threshold = policy.runtime_pressure_threshold;
    input.vram_valid = telem.vram.valid;
    input.is_discrete_tier = (policy.tier == ApuTier::Tier4_DiscreteOrCustom);

    VfUmaGuardDecision dec = vf_rust_evaluate_uma_pressure(&input, guard_interventions_count_);
    guard_interventions_count_ = dec.total_interventions;

    UmaGuardStatus status{};
    status.available_os_headroom_mb = dec.available_os_headroom_mb;
    status.dxgi_budget_utilization = dec.dxgi_budget_utilization;
    status.guard_triggered = dec.guard_triggered;
    status.total_interventions = dec.total_interventions;

    if (dec.guard_triggered) {
        if (dec.dxgi_over_ceiling && dec.free_ram_critical) {
            status.active_reason = "UMA Guard Triggered: DXGI budget utilization above threshold (" +
                std::to_string(static_cast<int>(status.dxgi_budget_utilization * 100)) + "%) and low free RAM (" +
                std::to_string(telem.free_and_zero_bytes / (1024ULL * 1024ULL)) + " MB)";
        } else if (dec.dxgi_over_ceiling) {
            status.active_reason = "UMA Guard Triggered: DXGI budget utilization above threshold (" +
                std::to_string(static_cast<int>(status.dxgi_budget_utilization * 100)) + "%)";
        } else {
            status.active_reason = "UMA Guard Triggered: Low free RAM (" +
                std::to_string(telem.free_and_zero_bytes / (1024ULL * 1024ULL)) + " MB < 1536 MB)";
        }

        if (!dry_run) {
            uint32_t current_pid = GetCurrentProcessId();
            for (uint32_t pid : background_pids) {
                if (pid == 0 || pid == 4 || pid == current_pid) {
                    continue;
                }
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
                if (hProc) {
                    K32EmptyWorkingSet(hProc);
                    CloseHandle(hProc);
                }
            }
        }
    } else {
        status.active_reason = "UMA Guard Passive: Memory pressure within safe bounds";
    }

    last_dxgi_pressure_ratio_ = status.dxgi_budget_utilization;
    last_guard_triggered_ = status.guard_triggered;

    return status;
}

} // namespace vegaflow
