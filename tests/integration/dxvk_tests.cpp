// Integration tests: dxvk deployment and profile. They run on a real Windows machine (registry, power plan, processes).
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "safety/frame_telemetry.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"
#include "windows/safe_file_system.hpp"

namespace vftest {

namespace {
// DxvkManager over the real file system (SandboxGuard), as the daemon uses it.
vegaflow::DxvkManager& RealDxvk() {
    static vegaflow::SafeFileSystem file_system;
    static vegaflow::DxvkManager manager(file_system);
    return manager;
}
} // namespace

void Test16() {
    // -------------------------------------------------------------------------
    // TEST 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path temp_base = std::filesystem::temp_directory_path() / "vegaflow_test_dxvk_suite";
        std::filesystem::remove_all(temp_base, ec);
        std::filesystem::create_directories(temp_base, ec);

        std::filesystem::path game_exe = temp_base / "shooter.exe";
        {
            std::ofstream out(game_exe, std::ios::binary);
            out << "MZ_DUMMY_EXE_CONTENT";
        }

        // A clean checkout has no DXVK DLLs (third-party files that are not versioned). In that case use stand-in
        // files in a temporary working directory, so the deployment, backup and removal logic is still exercised.
        const std::filesystem::path original_cwd = std::filesystem::current_path(ec);
        const std::filesystem::path stand_in_root = std::filesystem::temp_directory_path() / "vegaflow_test_dxvk_assets";
        bool using_stand_ins = false;
        if (vegaflow::DxvkManager::GetDxvkAssetsPath().empty()) {
            std::filesystem::remove_all(stand_in_root, ec);
            for (const char* arch : {"x32", "x64"}) {
                const std::filesystem::path dir = stand_in_root / "assets" / "dxvk" / "dxvk-2.5.3" / arch;
                std::filesystem::create_directories(dir, ec);
                std::ofstream(dir / "d3d11.dll", std::ios::binary) << "STAND_IN_DXVK_D3D11";
                std::ofstream(dir / "dxgi.dll", std::ios::binary) << "STAND_IN_DXVK_DXGI";
            }
            std::filesystem::current_path(stand_in_root, ec);
            using_stand_ins = true;
        }

        // 1. Assets location verification
        std::wstring assets_path = vegaflow::DxvkManager::GetDxvkAssetsPath();
        bool assets_found = !assets_path.empty();

        // 2. AntiCheat Block Verification
        std::string ac_msg;
        bool ac_blocked = !RealDxvk().DeployDxvk(game_exe.wstring(), DetectedPolicy(), true /* has_anticheat */, true, false, &ac_msg);
        bool ac_msg_ok = (ac_msg.find("Anti-Cheat") != std::string::npos);
        bool no_files_installed = !vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        // 3. Dry-Run Verification
        std::string dry_msg;
        bool dry_ok = RealDxvk().DeployDxvk(game_exe.wstring(), DetectedPolicy(), false, true, true /* dry_run */, &dry_msg);
        bool still_no_files = !vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        // 4. Pre-existing custom DLL backup test
        std::filesystem::path d3d11_file = temp_base / "d3d11.dll";
        {
            std::ofstream out(d3d11_file, std::ios::binary);
            out << "CUSTOM_D3D11_ORIGINAL_DATA";
        }

        // 5. Real Deployment Execution
        std::string deploy_msg;
        bool deploy_ok = RealDxvk().DeployDxvk(game_exe.wstring(), DetectedPolicy(), false, true, false, &deploy_msg);
        bool is_installed = vegaflow::DxvkManager::IsDxvkInstalled(game_exe.wstring());

        bool d3d11_exists = std::filesystem::exists(temp_base / "d3d11.dll", ec);
        bool dxgi_exists = std::filesystem::exists(temp_base / "dxgi.dll", ec);
        bool conf_exists = std::filesystem::exists(temp_base / "dxvk.conf", ec);
        bool bak_exists = std::filesystem::exists(temp_base / "d3d11.dll.original.bak", ec);

        // Verify dxvk.conf content (GPL & 6 threads)
        bool conf_content_ok = false;
        if (conf_exists) {
            std::ifstream cfile(temp_base / "dxvk.conf");
            std::string line;
            bool found_gpl = false;
            bool found_threads = false;
            bool found_async = false;
            bool found_vram = false;
            bool found_shared = false;
            while (std::getline(cfile, line)) {
                if (line.find("dxvk.gpl = True") != std::string::npos) found_gpl = true;
                if (line.find("dxvk.numCompilerThreads = 6") != std::string::npos) found_threads = true;
                if (line.find("dxvk.enableAsync = True") != std::string::npos) found_async = true;
                if (line.find("dxgi.maxDeviceMemory = 4096") != std::string::npos) found_vram = true;
                if (line.find("dxgi.maxSharedMemory = 8192") != std::string::npos) found_shared = true;
            }
            conf_content_ok = (found_gpl && found_threads && found_async && found_vram && found_shared);
        }

        // 6. Verification through SteamEngineOptimizer
        auto scanned_game = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 991122, "Test Shooter");
        bool scan_detected_dxvk = scanned_game.dxvk_installed;

        // 7. Dummy logs generation and removal test
        {
            std::ofstream out(temp_base / "shooter_d3d11.log");
            out << "d3d11 log sample";
        }
        {
            std::ofstream out(temp_base / "shooter_dxgi.log");
            out << "dxgi log sample";
        }

        std::string remove_msg;
        bool remove_ok = RealDxvk().RemoveDxvk(game_exe.wstring(), false, &remove_msg);

        bool conf_deleted = !std::filesystem::exists(temp_base / "dxvk.conf", ec);
        bool logs_deleted = !std::filesystem::exists(temp_base / "shooter_d3d11.log", ec) &&
                            !std::filesystem::exists(temp_base / "shooter_dxgi.log", ec);
        bool bak_deleted = !std::filesystem::exists(temp_base / "d3d11.dll.original.bak", ec);
        bool dxgi_deleted = !std::filesystem::exists(temp_base / "dxgi.dll", ec);

        // Verify original d3d11 was restored
        bool original_restored = false;
        if (std::filesystem::exists(d3d11_file, ec)) {
            std::ifstream f(d3d11_file, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            original_restored = (content == "CUSTOM_D3D11_ORIGINAL_DATA");
        }

        // Cleanup
        std::filesystem::remove_all(temp_base, ec);
        if (using_stand_ins) {
            std::filesystem::current_path(original_cwd, ec);
            std::filesystem::remove_all(stand_in_root, ec);
        }

        bool t16_success = assets_found && ac_blocked && ac_msg_ok && no_files_installed &&
                           dry_ok && still_no_files && deploy_ok && is_installed &&
                           d3d11_exists && dxgi_exists && conf_exists && bak_exists &&
                           conf_content_ok && scan_detected_dxvk && remove_ok &&
                           conf_deleted && logs_deleted && bak_deleted && dxgi_deleted && original_restored;

        std::string details = "Assets: " + std::string(assets_found ? (using_stand_ins ? "PASS (stand-in files)" : "PASS") : "FAIL") + ", "
            + "AC Protection: " + std::string(ac_blocked && ac_msg_ok ? "PASS" : "FAIL") + ", "
            + "Deployment & Conf: " + std::string(deploy_ok && conf_content_ok ? "PASS" : "FAIL") + ", "
            + "Backup & Restore: " + std::string(bak_exists && original_restored ? "PASS" : "FAIL") + ", "
            + "Logs & Conf Cleaned: " + std::string(conf_deleted && logs_deleted ? "PASS" : "FAIL");

        RecordResult(t16_success, "Test 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)", details);
    }
}

void Test22() {
    // -------------------------------------------------------------------------
    // TEST 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path test_dir = "scratch/test_dxvk_apu";
        std::filesystem::create_directories(test_dir, ec);
        if (ec || !std::filesystem::exists(test_dir, ec)) {
            test_dir = std::filesystem::temp_directory_path() / "test_dxvk_apu";
            std::filesystem::create_directories(test_dir, ec);
        }

        // Clean up residual files inside the test directory
        std::filesystem::remove(test_dir / "dxvk.conf", ec);
        std::filesystem::remove_all(test_dir / "quarantine_genshin", ec);
        std::filesystem::remove_all(test_dir / "quarantine_zzz", ec);

        // 1. Dry-Run simulation (validation without writing to disk)
        bool dry_res = RealDxvk().ConfigureApuProfile(test_dir, DetectedPolicy(), true /* dryRun */);
        bool dry_no_file = !std::filesystem::exists(test_dir / "dxvk.conf", ec);
        bool dry_ok = dry_res && dry_no_file;

        // 2. Real generation of an optimized profile for Zen 2 / Vega 6 APU
        bool config_ok = RealDxvk().ConfigureApuProfile(test_dir, DetectedPolicy(), false /* dryRun */);
        bool conf_exists = std::filesystem::exists(test_dir / "dxvk.conf", ec);

        // 3. Validation of active directives
        bool async_active = false;
        uint32_t compiler_threads = 0;
        bool validate_ok = RealDxvk().ValidateDxvkApuProfile(test_dir, async_active, compiler_threads);
        bool directives_val_ok = validate_ok && async_active && (compiler_threads == 6);

        // Verification of directives in the physical file
        bool file_directives_ok = false;
        if (conf_exists) {
            std::ifstream cfile(test_dir / "dxvk.conf");
            std::string line;
            bool found_early_discard = false;
            bool found_pipe_lib = false;
            bool found_gpl = false;
            bool found_lifetime = false;
            while (std::getline(cfile, line)) {
                if (line.find("dxvk.useEarlyDiscard = true") != std::string::npos) found_early_discard = true;
                if (line.find("dxvk.pipelineLibrary = true") != std::string::npos) found_pipe_lib = true;
                if (line.find("dxvk.enableGraphicsPipelineLibrary = true") != std::string::npos) found_gpl = true;
                if (line.find("dxvk.trackPipelineLifetime = true") != std::string::npos) found_lifetime = true;
            }
            file_directives_ok = found_early_discard && found_pipe_lib && found_gpl && found_lifetime;
        }

        // 4. Incremental update preserving existing settings
        {
            std::ofstream custom_tweak(test_dir / "dxvk.conf", std::ios::app);
            custom_tweak << "d3d11.samplerAnisotropy = 8\n";
        }
        bool reconfigure_ok = RealDxvk().ConfigureApuProfile(test_dir, DetectedPolicy(), false /* dryRun */);
        bool custom_preserved = false;
        {
            std::ifstream cfile(test_dir / "dxvk.conf");
            std::string content((std::istreambuf_iterator<char>(cfile)), std::istreambuf_iterator<char>());
            custom_preserved = (content.find("d3d11.samplerAnisotropy = 8") != std::string::npos) &&
                               (content.find("dxvk.enableAsync = true") != std::string::npos);
        }

        // 5. SandboxGuard Anti-Cheat protection
        // 5a. Simulated directory with GenshinImpact.exe
        std::filesystem::path genshin_dir = test_dir / "quarantine_genshin";
        std::filesystem::create_directories(genshin_dir, ec);
        {
            std::ofstream ac_file(genshin_dir / "GenshinImpact.exe");
            ac_file << "dummy_genshin_binary";
        }
        bool genshin_guard_blocked = !vegaflow::SandboxGuard::CanModifyGamePath(genshin_dir);
        bool genshin_deploy_blocked = !RealDxvk().ConfigureApuProfile(genshin_dir, DetectedPolicy(), false);
        bool genshin_dry_blocked = !RealDxvk().ConfigureApuProfile(genshin_dir, DetectedPolicy(), true);
        bool genshin_no_conf = !std::filesystem::exists(genshin_dir / "dxvk.conf", ec);

        // 5b. Simulated directory with ZenlessZoneZero.exe
        std::filesystem::path zzz_dir = test_dir / "quarantine_zzz";
        std::filesystem::create_directories(zzz_dir, ec);
        {
            std::ofstream ac_file(zzz_dir / "ZenlessZoneZero.exe");
            ac_file << "dummy_zzz_binary";
        }
        bool zzz_guard_blocked = !vegaflow::SandboxGuard::CanModifyGamePath(zzz_dir);
        bool zzz_deploy_blocked = !RealDxvk().ConfigureApuProfile(zzz_dir, DetectedPolicy(), false);
        bool zzz_dry_blocked = !RealDxvk().ConfigureApuProfile(zzz_dir, DetectedPolicy(), true);
        bool zzz_no_conf = !std::filesystem::exists(zzz_dir / "dxvk.conf", ec);

        bool anticheat_protection_ok = genshin_guard_blocked && genshin_deploy_blocked &&
                                       genshin_dry_blocked && genshin_no_conf &&
                                       zzz_guard_blocked && zzz_deploy_blocked &&
                                       zzz_dry_blocked && zzz_no_conf;

        // 6. Clean removal (RemoveApuProfile)
        bool remove_dry_ok = RealDxvk().RemoveApuProfile(test_dir, true /* dryRun */);
        bool conf_still_there = std::filesystem::exists(test_dir / "dxvk.conf", ec);

        bool remove_real_ok = RealDxvk().RemoveApuProfile(test_dir, false /* dryRun */);
        bool conf_deleted = !std::filesystem::exists(test_dir / "dxvk.conf", ec);

        bool post_async = false;
        uint32_t post_threads = 0;
        bool post_val_fails = !RealDxvk().ValidateDxvkApuProfile(test_dir, post_async, post_threads);

        bool removal_ok = remove_dry_ok && conf_still_there && remove_real_ok && conf_deleted && post_val_fails;

        // Final cleanup of the test directory
        std::filesystem::remove(test_dir / "dxvk.conf", ec);
        std::filesystem::remove_all(test_dir / "quarantine_genshin", ec);
        std::filesystem::remove_all(test_dir / "quarantine_zzz", ec);
        std::filesystem::remove_all(test_dir, ec);

        bool t22_success = dry_ok && config_ok && conf_exists && directives_val_ok &&
                           file_directives_ok && reconfigure_ok && custom_preserved &&
                           anticheat_protection_ok && removal_ok;

        std::string details = "Dry-Run: " + std::string(dry_ok ? "PASS" : "FAIL") + ", "
            + "APU Profile (GPL/Async/6T): " + std::string((config_ok && directives_val_ok && file_directives_ok) ? "PASS" : "FAIL") + ", "
            + "Custom Config Preserved: " + std::string(custom_preserved ? "PASS" : "FAIL") + ", "
            + "Anti-Cheat Lockdown (Genshin/ZZZ): " + std::string(anticheat_protection_ok ? "PASS (Kernel Shield)" : "FAIL") + ", "
            + "Clean Removal: " + std::string(removal_ok ? "PASS" : "FAIL");

        RecordResult(t22_success, "Test 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming", details);
    }
}

} // namespace vftest
