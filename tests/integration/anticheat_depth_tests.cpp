// Integration test for E1.5: the anti-cheat scan looks into subfolders of the game folder (bounded depth), on real disk.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "windows/sandbox_guard.hpp"
#include "../test_support.hpp"

namespace vftest {

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

    fs::remove_all(base.parent_path().parent_path(), ec);
    const bool ok = clean_ok && subfolder_found && top_level_found && depth_is_bounded && missing_dir_safe && outside_library_not_walked && library_not_a_game && dotdot_resolves;
    RecordResult(ok, "Test 91: anti-cheat scan reaches subfolders of the game folder up to a bounded depth",
                 std::string("Clean=") + (clean_ok ? "OK" : "FAIL") + ", Subfolder=" + (subfolder_found ? "OK" : "FAIL") +
                     ", TopLevel=" + (top_level_found ? "OK" : "FAIL") + ", DepthBounded=" + (depth_is_bounded ? "OK" : "FAIL") +
                     ", NoLibraryShallow=" + (outside_library_not_walked ? "OK" : "FAIL") + ", LibraryNotAGame=" + (library_not_a_game ? "OK" : "FAIL") + ", DotDot=" + (dotdot_resolves ? "OK" : "FAIL") + ", MissingDir=" + (missing_dir_safe ? "OK" : "FAIL") + ", WriteBlocked(info)=" + (write_blocked ? "yes" : "no"));
}

} // namespace vftest
