// Integration test for E1.5: the anti-cheat scan looks into subfolders of the game folder (bounded depth), on real disk.

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

#include "windows/sandbox_guard.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {
// Turns one privilege on or off for this process; returns whether it was on before (false if it is absent).
// SeBackupPrivilege makes FindFirstFile ignore an access-denied ACE, which would hide the case under test.
bool SetPrivilege(const wchar_t* name, bool enable) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    bool was_on = false;
    if (LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
        TOKEN_PRIVILEGES prev{};
        DWORD len = sizeof prev;
        if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof prev, &prev, &len) && prev.PrivilegeCount == 1) {
            was_on = (prev.Privileges[0].Attributes & SE_PRIVILEGE_ENABLED) != 0;
        }
    }
    CloseHandle(token);
    return was_on;
}
} // namespace

void Test91() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / L"vegaflow_test_ac_depth" / L"steamapps" / L"common";
    fs::remove_all(base.parent_path().parent_path(), ec);

    const fs::path clean = base / L"clean_game" / L"Binaries" / L"Win64";
    fs::create_directories(clean, ec);
    { std::ofstream(clean / "game.exe") << "x"; }

    const fs::path deep_ok = base / L"ac_game";
    fs::create_directories(deep_ok / L"Binaries" / L"Win64" / L"Plugins", ec);
    { std::ofstream(deep_ok / L"Binaries" / L"Win64" / L"Plugins" / "EasyAntiCheat_EOS.dll") << "x"; }

    const fs::path too_deep = base / L"far_game";
    fs::create_directories(too_deep / L"a" / L"b" / L"c" / L"d" / L"e" / L"f", ec);
    { std::ofstream(too_deep / L"a" / L"b" / L"c" / L"d" / L"e" / L"f" / "EasyAntiCheat_EOS.dll") << "x"; }

    const fs::path top_level = base / L"top_game";
    fs::create_directories(top_level, ec);
    { std::ofstream(top_level / "BEClient_x64.dll") << "x"; std::ofstream(top_level / "battleye.txt") << "x"; }

    const bool clean_ok = !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"clean_game");
    const bool subfolder_found = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(deep_ok);
    const bool top_level_found = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(top_level);
    const bool depth_is_bounded = !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(too_deep);
    const bool missing_dir_safe = !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"does_not_exist");
    const fs::path nolib = fs::temp_directory_path(ec) / L"vegaflow_test_ac_nolibrary";
    fs::create_directories(nolib / L"a" / L"b", ec);
    { std::ofstream(nolib / L"a" / L"b" / "EasyAntiCheat_EOS.dll") << "x"; }
    const bool outside_library_not_walked = !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(nolib);
    fs::remove_all(nolib, ec);
    // The library folder itself is not a game: a trailing separator, `.` or `..` must not turn it into a whole-library scan.
    const bool library_not_a_game = !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base) &&
                                    !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"clean_game" / L".." / L"..") &&
                                    !vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"clean_game" / L".");
    // `..` that lands on the anti-cheat game is still that game.
    const bool dotdot_resolves = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"clean_game" / L".." / L"ac_game");
    const bool write_blocked = !vegaflow::SandboxGuard::CanModifyGamePath(deep_ok / L"Binaries");

    // A game too big to walk whole (more entries than the scan cap) is refused: the unread part could hold the anti-cheat.
    const fs::path huge = base / L"huge_game" / L"Content";
    fs::create_directories(huge, ec);
    for (int i = 0; i <= 20000; ++i) { std::ofstream(huge / (std::to_wstring(i) + L".pak")); }
    const bool cap_refuses = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"huge_game");

    // F3.3 review: a subfolder the scan may not read could hold the anti-cheat, so it is refused, not skipped as "clean".
    const fs::path locked = base / L"locked_game" / L"Secret";
    fs::create_directories(locked, ec);
    const bool backup_was_on = SetPrivilege(L"SeBackupPrivilege", false);
    const std::wstring deny = L"icacls \"" + locked.wstring() + L"\" /deny *S-1-1-0:(RD,RA,REA) >nul 2>&1";
    const bool denied = _wsystem(deny.c_str()) == 0;
    std::error_code probe_ec;
    fs::directory_iterator probe(locked, fs::directory_options::none, probe_ec);
    const std::string probe_msg = "denied=" + std::to_string(denied) + " probe_ec=" + std::to_string(probe_ec.value());
    const bool unreadable_refuses = !denied || vegaflow::SandboxGuard::DirectoryContainsAntiCheat(base / L"locked_game");
    if (backup_was_on) SetPrivilege(L"SeBackupPrivilege", true);
    _wsystem((L"icacls \"" + locked.wstring() + L"\" /remove:d *S-1-1-0 >nul 2>&1").c_str());

    fs::remove_all(base.parent_path().parent_path(), ec);
    const bool ok = unreadable_refuses && clean_ok && subfolder_found && top_level_found && depth_is_bounded && missing_dir_safe && outside_library_not_walked && library_not_a_game && dotdot_resolves && cap_refuses;
    RecordResult(ok, "Test 91: anti-cheat scan reaches subfolders of the game folder up to a bounded depth",
                 std::string("Clean=") + (clean_ok ? "OK" : "FAIL") + ", Subfolder=" + (subfolder_found ? "OK" : "FAIL") +
                     ", TopLevel=" + (top_level_found ? "OK" : "FAIL") + ", DepthBounded=" + (depth_is_bounded ? "OK" : "FAIL") +
                     ", NoLibraryShallow=" + (outside_library_not_walked ? "OK" : "FAIL") + ", LibraryNotAGame=" + (library_not_a_game ? "OK" : "FAIL") + ", DotDot=" + (dotdot_resolves ? "OK" : "FAIL") + ", MissingDir=" + (missing_dir_safe ? "OK" : "FAIL") + ", Unreadable=" + std::string(unreadable_refuses ? "OK" : "FAIL") + "(" + probe_msg + ")" + ", CapRefuses=" + (cap_refuses ? "OK" : "FAIL") + ", WriteBlocked(info)=" + (write_blocked ? "yes" : "no"));
}

} // namespace vftest
