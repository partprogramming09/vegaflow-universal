#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>

namespace vegaflow {

struct PrivilegeStatus {
    bool is_elevated = false;
    bool profile_single_process = false; // SeProfileSingleProcessPrivilege
    bool increase_base_priority = false; // SeIncreaseBasePriorityPrivilege
    bool debug_privilege = false;        // SeDebugPrivilege (opcional)
};

class PrivilegeManager {
public:
    static PrivilegeStatus EnableRequiredPrivileges(bool enable_debug = false);
    static bool IsProcessElevated();

    // Relaunches the current executable requesting UAC elevation ("runas") with the given arguments.
    // If dry_run == true, it only checks that it can obtain the executable path and returns true without opening a UAC window.
    static bool RelaunchAsAdmin(const std::wstring& arguments = L"", bool dry_run = false);

private:
    static bool EnablePrivilege(HANDLE token, const wchar_t* privilege_name);
};

} // namespace vegaflow
