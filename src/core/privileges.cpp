#include "privileges.hpp"

#include <shellapi.h>
#include <iterator>

namespace vegaflow {

bool PrivilegeManager::IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_ELEVATION elevation{};
    DWORD return_length = 0;
    bool elevated = false;

    if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &return_length)) {
        elevated = (elevation.TokenIsElevated != 0);
    }

    CloseHandle(token);
    return elevated;
}

bool PrivilegeManager::EnablePrivilege(HANDLE token, const wchar_t* privilege_name) {
    if (!token || !privilege_name) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, privilege_name, &luid)) {
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    SetLastError(ERROR_SUCCESS);
    if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
        return false;
    }

    return (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
}

PrivilegeStatus PrivilegeManager::EnableRequiredPrivileges(bool enable_debug) {
    PrivilegeStatus status{};
    status.is_elevated = IsProcessElevated();

    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return status;
    }

    status.profile_single_process = EnablePrivilege(token, L"SeProfileSingleProcessPrivilege");
    status.increase_base_priority = EnablePrivilege(token, L"SeIncreaseBasePriorityPrivilege");

    if (enable_debug) {
        status.debug_privilege = EnablePrivilege(token, L"SeDebugPrivilege");
    }

    CloseHandle(token);
    return status;
}

bool PrivilegeManager::RelaunchAsAdmin(const std::wstring& arguments, bool dry_run) {
    wchar_t exe_path[MAX_PATH * 4] = {0};
    DWORD len = GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(std::size(exe_path)));
    if (len == 0 || len >= std::size(exe_path)) {
        return false;
    }

    if (dry_run) {
        return true;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe_path;
    sei.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_DEFAULT;

    return ShellExecuteExW(&sei) != FALSE;
}

} // namespace vegaflow
