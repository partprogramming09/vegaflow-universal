// Integration test for F5.3 and F5.4: Epic and GOG game folders are allowed through SandboxGuard with the same checks as a
// Steam game folder (real path, anti-cheat). Runs on Windows: junctions and real files.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows/sandbox_guard.hpp"
#include "../test_support.hpp"

#include <filesystem>
#include <fstream>

namespace vftest {

namespace {

bool MakeJunctionTo(const std::filesystem::path& link, const std::filesystem::path& target) {
    const std::wstring cmd = L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\" >nul 2>&1";
    std::error_code ec;
    return _wsystem(cmd.c_str()) == 0 && std::filesystem::exists(link, ec);
}

} // namespace

void Test161() {
    using vegaflow::LauncherGameFolder;
    using vegaflow::SandboxAction;
    using vegaflow::SandboxDenialReason;
    using vegaflow::SandboxGuard;
    std::error_code ec;
    std::string failures;
    auto expect = [&](bool ok, const std::string& name) { if (!ok) failures += " [" + name + "]"; };
    auto verdict = [](const std::filesystem::path& p) { return SandboxGuard::ValidatePath(p.wstring(), SandboxAction::Write); };
    auto denied = [&](const std::filesystem::path& p) { return !verdict(p).allowed; };

    // A folder no rule allows and any user can write (like Test 41): the games live under it.
    const std::filesystem::path base = L"C:\\Users\\Public\\vegaflow_test_launchers";
    RemoveDirectoryW((base / L"link_to_game").c_str());
    std::filesystem::remove_all(base, ec);
    const auto game = base / L"Epic Games" / L"SampleGame";
    const auto gog = base / L"GOG Games" / L"Sample";
    const auto eac_game = base / L"Epic Games" / L"Shooter";
    const auto deep_eac_game = base / L"Epic Games" / L"Arena";
    const auto flagged = base / L"Epic Games" / L"Flagged";
    std::filesystem::create_directories(game / L"Config", ec);
    std::filesystem::create_directories(gog, ec);
    std::filesystem::create_directories(eac_game / L"EasyAntiCheat", ec);
    std::filesystem::create_directories(deep_eac_game / L"Binaries" / L"Win64" / L"EasyAntiCheat", ec);
    std::filesystem::create_directories(flagged, ec);

    SandboxGuard::SetLauncherGameFolders({});
    expect(denied(game / L"Game.ini"), "unlisted game folder is not allowed");

    SandboxGuard::SetLauncherGameFolders({{game.wstring(), false}, {gog.wstring(), false}, {eac_game.wstring(), false},
                                          {deep_eac_game.wstring(), false}, {flagged.wstring(), true}});
    expect(!denied(game / L"Game.ini"), "listed Epic folder allows a write");
    expect(!denied(game / L"Config" / L"Engine.ini"), "listed Epic folder allows a nested write");
    expect(!denied(gog / L"goggame.ini"), "listed GOG folder allows a write");
    expect(SandboxGuard::SafeWriteFile((game / L"Config" / L"vegaflow_real.ini").wstring(), "x") &&
               std::filesystem::exists(game / L"Config" / L"vegaflow_real.ini", ec), "a real write lands inside a listed folder");

    // Outside the roots.
    expect(denied(base / L"Epic Games" / L"Other" / L"Game.ini"), "a sibling game folder is not allowed");
    expect(denied(base / L"Epic Games" / L"SampleGame2" / L"Game.ini"), "a folder that only starts like the game's is not allowed");
    expect(denied(base / L"Epic Games" / L"Game.ini"), "the launcher folder above the game is not allowed");
    expect(denied(L"C:\\Windows\\System32\\vegaflow_x.ini"), "Windows is never allowed");

    // `..` and links.
    expect(denied(game / L".." / L"Other" / L"Game.ini"), "`..` out of the game folder");
    expect(denied((game.wstring() + L"\\..\\..\\..\\Public\\vegaflow_x.ini")), "`..` far out of the game folder");
    expect(denied(game / L"Config" / L".." / L".." / L".." / L"x.ini"), "`..` from a nested folder");
    const auto link = base / L"link_to_game";
    const auto elsewhere = base / L"Elsewhere";
    std::filesystem::create_directories(elsewhere, ec);
    expect(MakeJunctionTo(link, elsewhere), "junction created");
    SandboxGuard::SetLauncherGameFolders({{link.wstring(), false}});
    expect(denied(link / L"Game.ini"), "a listed folder that is a junction allows nothing");
    expect(denied(elsewhere / L"Game.ini"), "the target of a listed junction is not allowed either");
    expect(!SandboxGuard::SafeWriteFile((link / L"vegaflow_link.ini").wstring(), "x") &&
               !std::filesystem::exists(elsewhere / L"vegaflow_link.ini", ec), "nothing written through the junction");
    RemoveDirectoryW(link.c_str());
    SandboxGuard::SetLauncherGameFolders({{game.wstring(), false}});
    // A junction made inside a listed folder that leads out is refused as for Steam.
    const auto inner_link = game / L"escape";
    expect(MakeJunctionTo(inner_link, elsewhere), "inner junction created");
    expect(denied(inner_link / L"Game.ini"), "a junction inside the game folder leading out");
    expect(!SandboxGuard::SafeWriteFile((inner_link / L"vegaflow_inner.ini").wstring(), "x") &&
               !std::filesystem::exists(elsewhere / L"vegaflow_inner.ini", ec), "nothing written through the inner junction");
    RemoveDirectoryW(inner_link.c_str());

    // Anti-cheat: never modified, whether the manifest names it or only the disk shows it.
    SandboxGuard::SetLauncherGameFolders({{game.wstring(), false}, {eac_game.wstring(), false},
                                          {deep_eac_game.wstring(), false}, {flagged.wstring(), true}});
    auto eac = verdict(eac_game / L"Config.ini");
    expect(!eac.allowed && eac.reason == SandboxDenialReason::AntiCheatLocked, "EasyAntiCheat folder in an Epic game blocks writes");
    expect(denied(deep_eac_game / L"Game.ini"), "EasyAntiCheat three folders down blocks writes at the game root");
    expect(denied(deep_eac_game / L"Binaries" / L"Win64" / L"Game.ini"), "EasyAntiCheat blocks writes next to it");
    expect(!SandboxGuard::SafeWriteFile((eac_game / L"vegaflow_eac.ini").wstring(), "x") &&
               !std::filesystem::exists(eac_game / L"vegaflow_eac.ini", ec), "no file is written into an anti-cheat game");
    expect(!SandboxGuard::SafeCopyFile((game / L"Config" / L"vegaflow_real.ini").wstring(), (eac_game / L"copy.ini").wstring(), true), "no copy into an anti-cheat game");
    expect(!SandboxGuard::SafeRemoveFile((eac_game / L"EasyAntiCheat" / L"x").wstring()), "no delete in an anti-cheat game");
    expect(denied(flagged / L"Game.ini"), "a manifest that names an anti-cheat is not a writable root");
    expect(denied(flagged / L"Config" / L"Game.ini"), "...nor a nested folder of it");
    expect(!denied(game / L"Game.ini"), "a clean game next to them is still allowed");
    expect(SandboxGuard::PathContainsAntiCheatSignature(L"C:\\Program Files\\Epic Games\\Shooter\\EasyAntiCheat_EOS.dll"),
           "the single anti-cheat list knows EasyAntiCheat");

    SandboxGuard::ReloadSteamLibraryFolders();
    SandboxGuard::ClearCustomAllowedPaths();
    std::filesystem::remove_all(base, ec);
    RecordResult(failures.empty(), "Test 161: Epic and GOG folders go through SandboxGuard with the Steam checks (anti-cheat, .., junctions)",
                 failures.empty() ? "0 mismatches" : "mismatches:" + failures);
}

} // namespace vftest
