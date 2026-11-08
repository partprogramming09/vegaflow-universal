#pragma once
// Internal helpers shared by the GraphicsOptimizer implementation files. Not part of the public API.

#include "optimize/graphics/graphics_optimizer.hpp"
#include "detect/hardware_profiler.hpp"

#if __has_include("windows/nt_defs.hpp")
#include "windows/nt_defs.hpp"
#else
#ifndef NTSTATUS
typedef LONG NTSTATUS;
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

extern "C" {
    NTSTATUS NTAPI NtQueryTimerResolution(
        PULONG MinimumResolution,
        PULONG MaximumResolution,
        PULONG CurrentResolution
    );

    NTSTATUS NTAPI NtSetTimerResolution(
        ULONG DesiredResolution,
        BOOLEAN SetResolution,
        PULONG CurrentResolution
    );
}
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <tlhelp32.h>
#include <winioctl.h>
#include <vector>

namespace vegaflow::graphics_internal {

typedef DWORD (WINAPI *pfnPowerGetActiveScheme)(HKEY UserRootPowerKey, GUID **ActivePolicyGuid);
typedef DWORD (WINAPI *pfnPowerSetActiveScheme)(HKEY UserRootPowerKey, const GUID *SchemeGuid);
typedef DWORD (WINAPI *pfnPowerWriteACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD AcValueIndex);
typedef DWORD (WINAPI *pfnPowerWriteDCValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, DWORD DcValueIndex);
typedef DWORD (WINAPI *pfnPowerReadACValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, LPDWORD AcValueIndex);

inline const GUID GUID_PCIEXPRESS_SETTINGS_SUBGROUP = {0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
inline const GUID GUID_PCIEXPRESS_ASPM = {0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
inline const GUID GUID_PROCESSOR_SETTINGS_SUBGROUP = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
inline const GUID GUID_PROCESSOR_PERF_BOOST_MODE = {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
inline const GUID GUID_PROCESSOR_CORE_PARKING_MIN_CORES = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
inline const GUID GUID_PROCESSOR_FREQUENCY_MAX = {0x75b64e17, 0x1334, 0x44c1, {0x8c, 0xbe, 0xec, 0x61, 0x99, 0xea, 0xe7, 0x25}};
inline const GUID GUID_PROCESSOR_FREQUENCY_MAX_ALT = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}};
inline const GUID GUID_PROCESSOR_THROTTLE_MAXIMUM = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
inline const GUID GUID_PROCESSOR_ENERGY_PERF_PREFERENCE = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};

typedef DWORD (WINAPI *pfnPowerReadDCValueIndex)(HKEY RootPowerKey, const GUID *SchemeGuid, const GUID *SubGroupOfPowerSettingsGuid, const GUID *PowerSettingGuid, LPDWORD DcValueIndex);

constexpr int kHiveLocalMachine = 0;
constexpr int kHiveCurrentUser = 1;

inline constexpr wchar_t kMmcssProfilePath[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
inline constexpr wchar_t kMmcssGamesPath[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";
inline constexpr const wchar_t* kMmcssGamesValues[] = {
    L"GPU Priority", L"Priority", L"Scheduling Category", L"SFIO Priority", L"Background Only", L"Clock Rate"
};

/// Values of the AMD driver UMA subkey that modify the fast-boot and tessellation profiles.
inline constexpr const wchar_t* kUmdBoostValues[] = {
    L"SurfaceFormatReplacements", L"FlipQueueSize", L"TFQ", L"Tessellation_OPTION",
    L"Tessellation", L"Main3D_DEF", L"TurboSync", L"DisableFlipQueueOptimization"
};

std::wstring ToLower(std::wstring_view str);

/// Saves value `name` of `hKey` (opened at `subkey` of the given hive) if there is no backup yet.
void RegBackupValue(int hive, const std::wstring& subkey, HKEY hKey, const wchar_t* name);

/// Restores one specific value from its backup; false if there was no backup or it could not be restored.
bool RegRestoreValue(int hive, const std::wstring& subkey, const wchar_t* name);

} // namespace vegaflow::graphics_internal
