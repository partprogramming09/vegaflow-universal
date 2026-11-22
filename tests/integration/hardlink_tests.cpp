// F3 investigation: can an account without write access to a protected file hard-link it? A filtered token (the
// Administrators SID turned deny-only, no privileges) stands for a standard user; a control file the same token may write
// proves the probe works. Real disk, real token; nothing outside a temp folder is touched.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "../test_support.hpp"

namespace vftest {

namespace {

// Creates `link` -> `target` while impersonating the filtered token; 1 = created, 0 = refused, -1 = no token.
int HardLinkAsFilteredToken(const std::wstring& link, const std::wstring& target, DWORD* error) {
    HANDLE proc = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &proc)) return -1;
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD sid_len = sizeof sid;
    HANDLE restricted = nullptr;
    HANDLE imp = nullptr;
    int result = -1;
    if (CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, sid, &sid_len)) {
        SID_AND_ATTRIBUTES deny{sid, 0};
        if (CreateRestrictedToken(proc, DISABLE_MAX_PRIVILEGE, 1, &deny, 0, nullptr, 0, nullptr, &restricted) &&
            DuplicateTokenEx(restricted, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenImpersonation, &imp) &&
            SetThreadToken(nullptr, imp)) {
            result = CreateHardLinkW(link.c_str(), target.c_str(), nullptr) ? 1 : 0;
            *error = GetLastError();
            RevertToSelf();
        }
    }
    for (HANDLE h : {imp, restricted, proc}) if (h) CloseHandle(h);
    return result;
}

bool Icacls(const std::wstring& args) { return _wsystem((L"icacls " + args + L" >nul 2>&1").c_str()) == 0; }

} // namespace

void Test140() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / L"vegaflow_test_hardlink";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path locked = dir / L"locked.txt", open_file = dir / L"open.txt";
    { std::ofstream(locked) << "x"; std::ofstream(open_file) << "x"; }
    // Protected like the install folder: SYSTEM and Administrators full, Users read and execute only.
    const bool acl_ok = Icacls(L"\"" + locked.wstring() + L"\" /inheritance:r /grant:r *S-1-5-18:F *S-1-5-32-544:F *S-1-5-32-545:RX") &&
                        Icacls(L"\"" + open_file.wstring() + L"\" /grant:r *S-1-5-32-545:M");
    // The folder must stay writable for the filtered token (it is the user's own temp folder, so it is).
    DWORD err_locked = 0, err_open = 0;
    const int locked_result = HardLinkAsFilteredToken((dir / L"link_locked.txt").wstring(), locked.wstring(), &err_locked);
    const int open_result = HardLinkAsFilteredToken((dir / L"link_open.txt").wstring(), open_file.wstring(), &err_open);
    fs::remove_all(dir, ec);
    const bool ok = acl_ok && locked_result == 0 && open_result == 1;
    RecordResult(ok, "Test 140: an account without write access to a protected file cannot hard-link it (filtered token)",
                 "acl=" + std::string(acl_ok ? "OK" : "FAIL") + ", protected_link=" + std::to_string(locked_result) +
                     " (error " + std::to_string(err_locked) + "), control_link=" + std::to_string(open_result) + " (error " +
                     std::to_string(err_open) + ")");
}

} // namespace vftest
