#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#ifndef _NTSTATUS_DEFINED
#define _NTSTATUS_DEFINED
typedef LONG NTSTATUS;
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef NTSYSAPI
#define NTSYSAPI __declspec(dllimport)
#endif

#ifndef NTAPI
#define NTAPI __stdcall
#endif

extern "C" {
    NTSYSAPI NTSTATUS NTAPI NtSetTimerResolution(
        ULONG DesiredResolution,
        BOOLEAN SetResolution,
        PULONG CurrentResolution
    );

    NTSYSAPI NTSTATUS NTAPI NtQueryTimerResolution(
        PULONG MaximumResolution,
        PULONG MinimumResolution,
        PULONG CurrentResolution
    );

    NTSYSAPI NTSTATUS NTAPI NtQuerySystemInformation(
        ULONG SystemInformationClass,
        PVOID SystemInformation,
        ULONG SystemInformationLength,
        PULONG ReturnLength
    );

    NTSYSAPI NTSTATUS NTAPI NtSetSystemInformation(
        ULONG SystemInformationClass,
        PVOID SystemInformation,
        ULONG SystemInformationLength
    );

    NTSYSAPI NTSTATUS NTAPI NtSetInformationProcess(
        HANDLE ProcessHandle,
        ULONG ProcessInformationClass,
        PVOID ProcessInformation,
        ULONG ProcessInformationLength
    );
}

namespace vegaflow {

enum SYSTEM_INFORMATION_CLASS {
    SystemProcessInformation = 5,
    SystemMemoryListInformation = 80
};

enum SYSTEM_MEMORY_LIST_COMMAND {
    MemoryCaptureAccessedBits = 0,
    MemoryCaptureAndResetAccessedBits = 1,
    MemoryEmptyWorkingSets = 2,
    MemoryFlushModifiedList = 3,
    MemoryPurgeStandbyList = 4,
    MemoryPurgeLowPriorityStandbyList = 5
};

struct SYSTEM_MEMORY_LIST_INFORMATION {
    ULONG_PTR ZeroPageCount;
    ULONG_PTR FreePageCount;
    ULONG_PTR ModifiedPageCount;
    ULONG_PTR ModifiedNoWritePageCount;
    ULONG_PTR BadPageCount;
    ULONG_PTR PageCountByPriority[8];
    ULONG_PTR RepurposedPagesByPriority[8];
    ULONG_PTR ModifiedPageCountPageFile;
};

enum PROCESS_INFORMATION_CLASS_NT {
    ProcessIoPriority = 33,
    ProcessPagePriority = 39
};

using ::NtSetTimerResolution;
using ::NtQueryTimerResolution;
using ::NtQuerySystemInformation;
using ::NtSetSystemInformation;
using ::NtSetInformationProcess;

} // namespace vegaflow
