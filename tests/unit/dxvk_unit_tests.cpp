// Unit tests: DxvkManager against a fake file system. They check the use case's decisions without touching
// the disk (only an empty temporary folder is created, because the manager still asks std::filesystem whether
// the folder exists). Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

#include "optimize/dxvk_manager.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test36() {
    // -------------------------------------------------------------------------
    // Test 36: DxvkManager changes game files only through the FileSystem port (fake file system)
    // -------------------------------------------------------------------------
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / L"vegaflow_unit_dxvk";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::wstring conf = (dir / L"dxvk.conf").wstring();

    vegaflow::ApuOptimizationPolicy policy;
    policy.dxvk_compiler_threads = 3; // a value no real tier uses, so it can only come from this policy

    FakeFileSystem refused;
    refused.allow_game_dir = false;
    const bool refused_ok = !vegaflow::DxvkManager(refused).ConfigureApuProfile(dir, policy) && refused.Changes() == 0;

    FakeFileSystem allowed;
    const bool configured = vegaflow::DxvkManager(allowed).ConfigureApuProfile(dir, policy);
    const bool conf_through_port = configured && allowed.writes.size() == 1 && allowed.writes[0] == conf &&
                                   allowed.files[conf].find("dxvk.numCompilerThreads = 3\n") != std::string::npos;
    const bool nothing_on_disk = !std::filesystem::exists(conf, ec);

    FakeFileSystem anticheat;
    std::string msg;
    const bool anticheat_ok =
        !vegaflow::DxvkManager(anticheat).DeployDxvk((dir / L"game.exe").wstring(), policy, true /* has_anticheat */,
                                                     true, false, &msg) &&
        anticheat.Changes() == 0;

    std::filesystem::remove_all(dir, ec);

    auto pass = [](bool b) { return std::string(b ? "PASS" : "FAIL"); };
    RecordResult(refused_ok && conf_through_port && nothing_on_disk && anticheat_ok,
                 "Test 36: DxvkManager changes game files only through the FileSystem port (fake file system)",
                 "refused folder, no change: " + pass(refused_ok) + ", dxvk.conf via port with policy threads: " +
                     pass(conf_through_port) + ", nothing on disk: " + pass(nothing_on_disk) +
                     ", anti-cheat, no change: " + pass(anticheat_ok));
}

void Test71() {
    // Test 71: a dxvk.conf the user already had is kept as .vegaflow.bak before ours replaces it, and put back on removal
    const std::wstring conf = L"C:\\Games\\G\\dxvk.conf";
    const std::wstring bak = conf + L".vegaflow.bak";
    using vegaflow::DxvkManager;

    // 1. The user's file is copied once, byte for byte, through the port.
    FakeFileSystem a;
    a.files[conf] = "dxgi.maxDeviceMemory = 1024\n";
    const bool kept = DxvkManager::BackupUserDxvkConf(a, conf, false) && a.files[bak] == "dxgi.maxDeviceMemory = 1024\n";
    // 2. A second deploy does not overwrite that copy with our own file.
    a.files[conf] = "dxvk.gpl = True\n";
    const bool once = DxvkManager::BackupUserDxvkConf(a, conf, false) && a.files[bak] == "dxgi.maxDeviceMemory = 1024\n" && a.copies.size() == 1;
    // 3. Our own file from an earlier deploy needs no copy; no file needs none either.
    FakeFileSystem b;
    b.files[conf] = "# VegaFlow DXVK AMD Vega 6 Profile\ndxvk.gpl = True\n";
    const bool ours = DxvkManager::BackupUserDxvkConf(b, conf, false) && b.copies.empty();
    FakeFileSystem none;
    const bool absent = DxvkManager::BackupUserDxvkConf(none, conf, false) && none.copies.empty();
    // 4. A failed copy reports it, so the deploy stops before changing anything.
    FakeFileSystem failing;
    failing.files[conf] = "x = 1\n";
    failing.fail_copy = true;
    const bool failed = !DxvkManager::BackupUserDxvkConf(failing, conf, false);
    // 5. Removing DXVK puts the user's file back and drops the copy; with no copy, ours is deleted.
    const bool restored = DxvkManager::RestoreUserDxvkConf(a, conf) && a.files[conf] == "dxgi.maxDeviceMemory = 1024\n" && a.files.count(bak) == 0;
    const bool dropped = DxvkManager::RestoreUserDxvkConf(b, conf) && b.files.count(conf) == 0;
    // 6. If the user's file cannot be put back, nothing is deleted and the caller is told (the copy stays for them).
    FakeFileSystem stuck;
    stuck.files[conf] = "dxvk.gpl = True\n";
    stuck.files[bak] = "dxgi.maxDeviceMemory = 1024\n";
    stuck.fail_copy = true;
    const bool told = !DxvkManager::RestoreUserDxvkConf(stuck, conf) && stuck.files.count(bak) == 1 && stuck.files.count(conf) == 1;
    RecordResult(kept && once && ours && absent && failed && restored && dropped && told,
                 "Test 71: a user's dxvk.conf is backed up once before DXVK writes its own, and restored on removal");
}

} // namespace vftest
