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
#include <pdh.h>
#include <pdhmsg.h>

#include <cstdint>
#include <cwchar>
#include <mutex>
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
constexpr ULONG kMemoryPurgeLowPriorityStandbyListCommand = 5;
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

// E2.3: global GPU memory (all processes) from PDH. DXGI CurrentUsage is this process only, so the guard never saw the game's VRAM.
struct GpuMemoryQuery {
    uint64_t luid = 0;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER shared = nullptr;
    PDH_HCOUNTER dedicated = nullptr;
};

// Sums every instance of a wildcard counter (one adapter can expose several `_phys_N` instances).
bool SumCounterInstances(PDH_HCOUNTER counter, uint64_t& out) {
    DWORD bytes = 0;
    DWORD items = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bytes, &items, nullptr) != static_cast<PDH_STATUS>(PDH_MORE_DATA) ||
        bytes == 0) {
        return false;
    }
    std::vector<unsigned char> buffer(bytes);
    auto* array = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bytes, &items, array) != ERROR_SUCCESS || items == 0) {
        return false;
    }
    uint64_t sum = 0;
    for (DWORD i = 0; i < items; ++i) {
        const DWORD status = array[i].FmtValue.CStatus;
        if (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) return false;
        if (array[i].FmtValue.largeValue > 0) sum += static_cast<uint64_t>(array[i].FmtValue.largeValue);
    }
    out = sum;
    return true;
}

// PdhAddEnglishCounterW: the localized names of PdhAddCounterW do not match on a Spanish Windows.
// false = counters unavailable (old Windows, no GPU counters): the caller marks the VRAM signal invalid.
bool QueryGpuAdapterMemory(uint64_t luid, uint64_t& shared, uint64_t& dedicated) {
    static std::mutex mutex;
    static GpuMemoryQuery cached;
    std::lock_guard<std::mutex> lock(mutex);

    if (cached.query && cached.luid != luid) {
        PdhCloseQuery(cached.query);
        cached = GpuMemoryQuery{};
    }
    if (!cached.query) {
        wchar_t instance[64]{};
        std::swprintf(instance, 64, L"luid_0x%08X_0x%08X_phys_*", static_cast<unsigned>(luid >> 32),
                      static_cast<unsigned>(luid & 0xFFFFFFFFu));
        const std::wstring prefix = std::wstring(L"\\GPU Adapter Memory(") + instance + L")\\";
        GpuMemoryQuery fresh{};
        fresh.luid = luid;
        const bool ok = PdhOpenQueryW(nullptr, 0, &fresh.query) == ERROR_SUCCESS &&
                        PdhAddEnglishCounterW(fresh.query, (prefix + L"Shared Usage").c_str(), 0, &fresh.shared) == ERROR_SUCCESS &&
                        PdhAddEnglishCounterW(fresh.query, (prefix + L"Dedicated Usage").c_str(), 0, &fresh.dedicated) == ERROR_SUCCESS;
        if (!ok) {
            if (fresh.query) PdhCloseQuery(fresh.query);
            return false;
        }
        cached = fresh;
    }

    if (PdhCollectQueryData(cached.query) != ERROR_SUCCESS) return false;
    uint64_t shared_sum = 0;
    uint64_t dedicated_sum = 0;
    if (!SumCounterInstances(cached.shared, shared_sum) || !SumCounterInstances(cached.dedicated, dedicated_sum)) return false;
    shared = shared_sum;
    dedicated = dedicated_sum;
    return true;
}

} // namespace

SystemMemoryTelemetry WindowsMemoryProbe::SampleSystemMemory(uint64_t target_luid) const {
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
                IDXGIAdapter1* adapter = nullptr;
                if (target_luid != 0) {
                    // Exact adapter: EnumAdapterByLuid (DXGI 1.4), else walk the adapters comparing AdapterLuid.
                    LUID luid{};
                    luid.LowPart = static_cast<DWORD>(target_luid & 0xFFFFFFFFu);
                    luid.HighPart = static_cast<LONG>(static_cast<uint32_t>(target_luid >> 32));
                    IDXGIFactory4* factory4 = nullptr;
                    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory4))) && factory4) {
                        factory4->EnumAdapterByLuid(luid, __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter));
                        factory4->Release();
                    }
                    for (UINT i = 0; !adapter; ++i) {
                        IDXGIAdapter1* candidate = nullptr;
                        if (factory->EnumAdapters1(i, &candidate) == DXGI_ERROR_NOT_FOUND) break;
                        DXGI_ADAPTER_DESC1 d{};
                        if (SUCCEEDED(candidate->GetDesc1(&d)) &&
                            ((static_cast<uint64_t>(static_cast<uint32_t>(d.AdapterLuid.HighPart)) << 32) | d.AdapterLuid.LowPart) == target_luid) {
                            adapter = candidate;
                        } else {
                            candidate->Release();
                        }
                    }
                } else {
                    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                        DXGI_ADAPTER_DESC1 d{};
                        if (SUCCEEDED(adapter->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) break;
                        adapter->Release();
                        adapter = nullptr;
                    }
                }
                if (adapter) {
                    uint64_t adapter_luid = target_luid;
                    DXGI_ADAPTER_DESC1 adapter_desc{};
                    if (SUCCEEDED(adapter->GetDesc1(&adapter_desc))) {
                        adapter_luid = (static_cast<uint64_t>(static_cast<uint32_t>(adapter_desc.AdapterLuid.HighPart)) << 32) |
                                       adapter_desc.AdapterLuid.LowPart;
                    }
                    IDXGIAdapter3* adapter3 = nullptr;
                    if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&adapter3))) && adapter3) {
                        DXGI_QUERY_VIDEO_MEMORY_INFO localInfo{};
                        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localInfo))) {
                            telemetry.vram.local_budget_bytes = localInfo.Budget;
                            telemetry.vram.local_usage_bytes = localInfo.CurrentUsage;  // this process only
                            // Valid only with the global counters too: without them the guard would read the per-process number.
                            telemetry.vram.valid = adapter_luid != 0 &&
                                QueryGpuAdapterMemory(adapter_luid, telemetry.vram.gpu_shared_usage_bytes,
                                                      telemetry.vram.gpu_dedicated_usage_bytes);
                        }
                        DXGI_QUERY_VIDEO_MEMORY_INFO nonLocalInfo{};
                        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocalInfo))) {
                            telemetry.vram.non_local_budget_bytes = nonLocalInfo.Budget;
                            telemetry.vram.non_local_usage_bytes = nonLocalInfo.CurrentUsage;
                        }
                        adapter3->Release();
                    }
                    adapter->Release();
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

StandbyPurgeOutcome WindowsMemoryControl::PurgeStandbyList(uint8_t level, bool dry_run) {
    StandbyPurgeOutcome outcome{};
    if (level != 1 && level != 2) {
        outcome.detail = "No standby purge level requested";
        return outcome;
    }
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

    ULONG cmd = (level == 1) ? kMemoryPurgeLowPriorityStandbyListCommand : kMemoryPurgeStandbyListCommand;
    NTSTATUS status = pfnNtSet(
        kSystemMemoryListInformationClass,
        &cmd,
        sizeof(cmd));

    outcome.nt_status = static_cast<int32_t>(status);
    if (NT_SUCCESS(status)) {
        outcome.succeeded = true;
        outcome.detail = level == 1 ? "Low-priority standby list purged successfully" : "Standby list purged successfully";
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
