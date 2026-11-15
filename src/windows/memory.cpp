#include "windows/memory.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

namespace {

typedef LONG NTSTATUS;
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

constexpr ULONG kSystemMemoryListInformationClass = 80;
constexpr ULONG kMemoryPurgeStandbyListCommand = 4;
constexpr ULONG_PTR kPageSizeBytes = 4096;
constexpr const wchar_t* kSeProfileSingleProcess = L"SeProfileSingleProcessPrivilege";
constexpr const wchar_t* kSeIncreaseQuota = L"SeIncreaseQuotaPrivilege";

struct SystemMemoryListInformation {
    ULONG_PTR zero_page_count;
    ULONG_PTR free_page_count;
    ULONG_PTR modified_page_count;
    ULONG_PTR modified_no_write_page_count;
    ULONG_PTR bad_page_count;
    ULONG_PTR page_count_by_priority[8];
    ULONG_PTR repurposed_pages_by_priority[8];
    ULONG_PTR modified_page_count_page_file;
};

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

PFN_NtQuerySystemInformation ResolveNtQuerySystemInformation() {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    static auto pfn = reinterpret_cast<PFN_NtQuerySystemInformation>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return pfn;
}

PFN_NtSetSystemInformation ResolveNtSetSystemInformation() {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    static auto pfn = reinterpret_cast<PFN_NtSetSystemInformation>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetSystemInformation"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return pfn;
}

std::string WideToUtf8Mem(const wchar_t* str, int len) {
    if (!str || len <= 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, str, len, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, str, len, out.data(), needed, nullptr, nullptr);
    return out;
}

} // namespace

SystemMemoryTelemetry WindowsMemoryProbe::SampleSystemMemory() const {
    SystemMemoryTelemetry telemetry{};

    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        telemetry.total_physical_bytes = memStatus.ullTotalPhys;
    }

    // Exact NT memory lists (no ullAvailPhys fallback: ullAvailPhys includes standby cache).
    auto pfnNtQuery = ResolveNtQuerySystemInformation();
    if (pfnNtQuery) {
        SystemMemoryListInformation memInfo{};
        ULONG retLen = 0;
        NTSTATUS status = pfnNtQuery(
            kSystemMemoryListInformationClass,
            &memInfo,
            sizeof(memInfo),
            &retLen);
        if (NT_SUCCESS(status)) {
            telemetry.free_and_zero_bytes =
                (static_cast<uint64_t>(memInfo.zero_page_count) +
                 static_cast<uint64_t>(memInfo.free_page_count)) *
                kPageSizeBytes;
            uint64_t standby = 0;
            for (ULONG_PTR count : memInfo.page_count_by_priority) {
                standby += static_cast<uint64_t>(count) * kPageSizeBytes;
            }
            telemetry.standby_bytes = standby;
            telemetry.modified_bytes =
                static_cast<uint64_t>(memInfo.modified_page_count) * kPageSizeBytes;
            telemetry.memory_lists_valid = true;
        }
    }

    // DXGI 1.4+ VRAM telemetry
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
                                break;
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

bool WindowsMemoryProbe::SampleProcessDiskReadBytes(uint32_t pid, uint64_t& out_read_bytes) const {
    out_read_bytes = 0;
    if (pid == 0) return false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;
    IO_COUNTERS ioCounters{};
    bool ok = (GetProcessIoCounters(hProc, &ioCounters) != FALSE);
    if (ok) {
        out_read_bytes = ioCounters.ReadTransferCount;
    }
    CloseHandle(hProc);
    return ok;
}

std::string WindowsMemoryProbe::QueryProcessImageName(uint32_t pid) const {
    if (pid == 0 || pid == 4) return "System";
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};
    wchar_t buf[MAX_PATH * 2]{};
    DWORD len = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    std::string result;
    if (QueryFullProcessImageNameW(hProc, 0, buf, &len) && len > 0) {
        result = WideToUtf8Mem(buf, static_cast<int>(len));
    }
    CloseHandle(hProc);
    return result;
}

uint32_t WindowsMemoryProbe::CurrentProcessId() const {
    return static_cast<uint32_t>(GetCurrentProcessId());
}

WindowsMemoryControl::WindowsMemoryControl() {
    EnablePrivilege(kSeProfileSingleProcess);
    EnablePrivilege(kSeIncreaseQuota);
}

uint32_t WindowsMemoryControl::TrimProcessWorkingSets(const std::vector<uint32_t>& pids) {
    uint32_t trimmed = 0;
    for (uint32_t pid : pids) {
        if (pid == 0 || pid == 4) continue;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_QUOTA, FALSE, pid);
        if (hProc) {
            if (K32EmptyWorkingSet(hProc)) {
                ++trimmed;
            }
            CloseHandle(hProc);
        }
    }
    return trimmed;
}

StandbyPurgeOutcome WindowsMemoryControl::PurgeStandbyList(bool dry_run) {
    StandbyPurgeOutcome outcome{};
    if (dry_run) {
        outcome.succeeded = true;
        outcome.detail = "Dry-run: standby list purge simulated";
        return outcome;
    }

    EnablePrivilege(kSeProfileSingleProcess);
    EnablePrivilege(kSeIncreaseQuota);

    auto pfnNtSet = ResolveNtSetSystemInformation();
    if (!pfnNtSet) {
        outcome.succeeded = false;
        outcome.detail = "NtSetSystemInformation not available in ntdll.dll";
        return outcome;
    }

    ULONG cmd = kMemoryPurgeStandbyListCommand;
    NTSTATUS status = pfnNtSet(
        kSystemMemoryListInformationClass,
        &cmd,
        sizeof(cmd));

    outcome.nt_status = static_cast<int32_t>(status);
    if (NT_SUCCESS(status)) {
        outcome.succeeded = true;
        outcome.detail = "Standby list purged successfully";
    } else if (status == static_cast<NTSTATUS>(0xC0000061L)) {
        outcome.succeeded = false;
        outcome.privilege_missing = true;
        outcome.detail = "Standby List purge skipped (requires Administrator / --elevate)";
    } else {
        outcome.succeeded = false;
        outcome.detail = "NtSetSystemInformation failed with NTSTATUS: " + std::to_string(status);
    }
    return outcome;
}

MemoryProbe& DefaultWindowsMemoryProbe() {
    static WindowsMemoryProbe instance;
    return instance;
}

MemoryControl& DefaultWindowsMemoryControl() {
    static WindowsMemoryControl instance;
    return instance;
}

uint64_t QueryCurrentProcessPrivateWorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (K32GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
            sizeof(pmc))) {
        return static_cast<uint64_t>(pmc.PrivateUsage);
    }
    return 0;
}

} // namespace vegaflow
