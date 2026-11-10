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

} // namespace vftest
