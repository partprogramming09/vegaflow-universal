// Integration tests: steam library, engine presets and single instance. They run on a real Windows machine (registry, power plan, processes).
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

namespace vftest {

void Test13() {
    // -------------------------------------------------------------------------
    // TEST 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        auto temp_base = std::filesystem::temp_directory_path() / "vegaflow_test_steam_opt";
        std::filesystem::remove_all(temp_base, ec);
        std::filesystem::create_directories(temp_base / "Engine" / "Binaries" / "Win64", ec);

        std::filesystem::path dummy_exe = temp_base / "MyGame-Win64-Shipping.exe";
        {
            std::ofstream out(dummy_exe, std::ios::binary);
            out << "MZ dummy executable header";
        }

        // 1. Detection of Unreal Engine 4/5
        auto game = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "My Unreal Game");
        bool engine_ok = (game.engine == vegaflow::GameEngineType::UnrealEngine4_5);
        bool ac_clean_ok = (!game.has_anticheat);
        bool exe_ok = (!game.primary_exe_path.empty());

        // 2. Safe config modification and .vegaflow.bak creation
        auto custom_cfg_dir = temp_base / "Config";
        std::filesystem::create_directories(custom_cfg_dir, ec);
        auto ini_path = custom_cfg_dir / "Engine.ini";
        {
            std::ofstream out(ini_path);
            out << "[Core.System]\nPaths=../../../Engine/Content\n";
        }

        auto rep_apply = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game, 3000, false, custom_cfg_dir.wstring());
        bool apply_ok = rep_apply.success;
        bool bak_created = rep_apply.backup_created;
        auto bak_path = custom_cfg_dir / "Engine.ini.vegaflow.bak";
        bool bak_exists = std::filesystem::exists(bak_path, ec);

        std::string modified_content;
        {
            std::ifstream in(ini_path);
            std::stringstream ss;
            ss << in.rdbuf();
            modified_content = ss.str();
        }
        bool has_preset = (modified_content.find("r.Streaming.PoolSize=4096") != std::string::npos) &&
                          (modified_content.find("r.Streaming.LimitPoolSizeToVRAM=0") != std::string::npos) &&
                          (modified_content.find("r.Streaming.MaxNumTexturesToStreamPerFrame=120") != std::string::npos);

        // 3. Restoration of original config from .vegaflow.bak
        auto rep_restore = vegaflow::SteamEngineOptimizer::RestoreEngineOriginalConfig(game, false, custom_cfg_dir.wstring());
        bool restore_ok = rep_restore.success;
        bool bak_removed = !std::filesystem::exists(bak_path, ec);

        std::string restored_content;
        {
            std::ifstream in(ini_path);
            std::stringstream ss;
            ss << in.rdbuf();
            restored_content = ss.str();
        }
        bool preset_removed = (restored_content.find("r.Streaming.PoolSize=") == std::string::npos);
        bool orig_intact = (restored_content.find("[Core.System]") != std::string::npos);

        // 4. Anti-Cheat safety verification (block file edits)
        std::filesystem::path dummy_eac = temp_base / "EasyAntiCheat_EOS.dll";
        {
            std::ofstream out(dummy_eac, std::ios::binary);
            out << "dummy eac";
        }
        auto game_protected = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "My Protected Game");
        bool ac_detect_ok = game_protected.has_anticheat;

        auto rep_ac = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game_protected, 3000, false, custom_cfg_dir.wstring());
        bool ac_block_ok = (rep_ac.blocked_by_anticheat && rep_ac.success);

        // 4b. HoYoverse Anti-Cheat safety verification (HoYoKProtect / mhypbase)
        std::filesystem::remove(dummy_eac, ec);
        std::filesystem::path dummy_hoyo = temp_base / "HoYoKProtect.sys";
        {
            std::ofstream out(dummy_hoyo, std::ios::binary);
            out << "dummy hoyokprotect";
        }
        auto game_hoyo = vegaflow::SteamEngineOptimizer::InspectGameDirectory(temp_base.wstring(), 999999, "Zenless Zone Zero");
        bool hoyo_detect_ok = game_hoyo.has_anticheat;
        auto rep_hoyo = vegaflow::SteamEngineOptimizer::ApplyEngineUmaPreset(game_hoyo, 3000, false, custom_cfg_dir.wstring());
        bool hoyo_block_ok = (rep_hoyo.blocked_by_anticheat && rep_hoyo.success);

        // Cleanup
        std::filesystem::remove_all(temp_base, ec);

        bool t13_success = engine_ok && ac_clean_ok && exe_ok && apply_ok && bak_created &&
                           bak_exists && has_preset && restore_ok && bak_removed &&
                           preset_removed && orig_intact && ac_detect_ok && ac_block_ok &&
                           hoyo_detect_ok && hoyo_block_ok;

        std::string details = "UE Detection: " + std::string(engine_ok ? "PASS" : "FAIL") + ", "
            + "Backup Created: " + std::string(bak_created && bak_exists ? "PASS" : "FAIL") + ", "
            + "Preset Applied: " + std::string(has_preset ? "PASS" : "FAIL") + ", "
            + "Restored & Bak Cleared: " + std::string(restore_ok && bak_removed && orig_intact ? "PASS" : "FAIL") + ", "
            + "AntiCheat Blocking: " + std::string(ac_detect_ok && ac_block_ok && hoyo_detect_ok && hoyo_block_ok ? "PASS" : "FAIL");

        RecordResult(t13_success, "Test 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore", details);
    }
}

void Test14() {
    // -------------------------------------------------------------------------
    // TEST 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync
    // -------------------------------------------------------------------------
    {
        // 1. Single-Instance Mutex guard test
        HANDLE hMutex1 = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_TestMutex");
        bool mutex1_ok = (hMutex1 != nullptr);

        HANDLE hMutex2 = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_TestMutex");
        bool mutex2_detected = (hMutex2 != nullptr && GetLastError() == ERROR_ALREADY_EXISTS);

        if (hMutex2) CloseHandle(hMutex2);
        if (hMutex1) CloseHandle(hMutex1);

        // 2. Non-blocking MMAgent dry-run check
        bool mem_comp_dry_ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(true, true /* dry_run */);

        // 3. Safe Steam RunningAppID query
        uint32_t running_app_id = vegaflow::SteamEngineOptimizer::GetRunningSteamAppId();
        (void)running_app_id; // Safe query without crash or throw
        bool steam_query_ok = true;

        bool t14_success = mutex1_ok && mutex2_detected && mem_comp_dry_ok && steam_query_ok;
        std::string details = "Mutex 1: " + std::string(mutex1_ok ? "OK" : "FAIL") + ", "
            + "Mutex 2 ERROR_ALREADY_EXISTS: " + std::string(mutex2_detected ? "PASS" : "FAIL") + ", "
            + "MMAgent Dry-Run: " + std::string(mem_comp_dry_ok ? "PASS" : "FAIL") + ", "
            + "RunningAppID Query: " + std::to_string(running_app_id);

        RecordResult(t14_success, "Test 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync", details);
    }
}

void Test24() {
    // -------------------------------------------------------------------------
    // TEST 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)
    // -------------------------------------------------------------------------
    {
        std::error_code ec;
        std::filesystem::path temp_base = std::filesystem::temp_directory_path();
        std::filesystem::path test_dir = temp_base / "vegaflow_test_shadow_opt";
        std::filesystem::path ac_dir = temp_base / "vegaflow_test_shadow_genshinimpact";

        // Preventive cleanup of previous runs
        std::filesystem::remove_all(test_dir, ec);
        std::filesystem::remove_all(ac_dir, ec);

        std::filesystem::create_directories(test_dir, ec);

        std::filesystem::path ini_path = test_dir / "Engine.ini";
        std::filesystem::path bak_path = test_dir / "Engine.ini.bak";

        // 1. Create an initial Engine.ini with other sections and keys
        std::string initial_ini = 
            "[Core.System]\n"
            "Paths=../../../Engine/Content\n"
            "\n"
            "[SystemSettings]\n"
            "r.ScreenPercentage=100\n"
            "r.TextureStreaming=1\n";
        {
            std::ofstream out(ini_path, std::ios::out | std::ios::binary);
            out.write(initial_ini.data(), initial_ini.size());
        }

        vegaflow::ApuShadowConfig config{};
        config.max_cascades = 2;
        config.distance_scale = 0.7f;
        config.radius_threshold = 0.04f;
        config.half_res_ao = true;
        config.volumetric_fog = 0;

        // 2. Dry-Run simulation of ApplyApuShadowConfig
        bool dry_apply_ok = vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ini_path, config, true /* dryRun */);
        bool dry_no_bak = !std::filesystem::exists(bak_path, ec);
        std::string post_dry_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            post_dry_content = ss.str();
        }
        bool dry_unchanged = (post_dry_content == initial_ini);

        // 3. Real application and configuration query
        bool apply_real_ok = vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ini_path, config, false /* dryRun */);
        bool bak_created = std::filesystem::exists(bak_path, ec);

        vegaflow::ApuShadowConfig queried{};
        bool query_ok = vegaflow::SteamEngineOptimizer::QueryApuShadowConfig(ini_path, queried);

        bool values_match = (queried.max_cascades == 2) &&
                            (std::abs(queried.distance_scale - 0.7f) < 0.001f) &&
                            (std::abs(queried.radius_threshold - 0.04f) < 0.001f) &&
                            (queried.half_res_ao == true) &&
                            (queried.volumetric_fog == 0);

        std::string applied_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            applied_content = ss.str();
        }
        bool preserved_other = (applied_content.find("Paths=../../../Engine/Content") != std::string::npos) &&
                               (applied_content.find("r.ScreenPercentage=100") != std::string::npos) &&
                               (applied_content.find("r.TextureStreaming=1") != std::string::npos);

        // 4. Hard SandboxGuard block against kernel-level Anti-Cheat (Genshin, ZZZ, HoYoKProtect)
        std::filesystem::create_directories(ac_dir, ec);
        {
            std::ofstream ac_file(ac_dir / "GenshinImpact.exe", std::ios::out | std::ios::binary);
            ac_file << "dummy_genshin_binary";
            std::ofstream hoyo_file(ac_dir / "HoYoKProtect.sys", std::ios::out | std::ios::binary);
            hoyo_file << "dummy_hoyokprotect";
        }
        std::filesystem::path ac_ini = ac_dir / "Engine.ini";
        {
            std::ofstream out(ac_ini, std::ios::out | std::ios::binary);
            out << "[SystemSettings]\n";
        }

        bool ac_apply_blocked = !vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ac_ini, config, false);
        bool ac_dry_blocked = !vegaflow::SteamEngineOptimizer::ApplyApuShadowConfig(ac_ini, config, true);
        bool ac_restore_blocked = !vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ac_ini, false);
        bool ac_shield_ok = ac_apply_blocked && ac_dry_blocked && ac_restore_blocked;

        // Immediately clean up the anti-cheat directory to isolate the temporary environment
        std::filesystem::remove_all(ac_dir, ec);

        // 5. Dry-Run simulation of RestoreApuShadowConfig
        bool dry_restore_ok = vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ini_path, true /* dryRun */);
        bool bak_still_exists = std::filesystem::exists(bak_path, ec);

        // 6. Real clean restoration from .bak
        bool restore_real_ok = vegaflow::SteamEngineOptimizer::RestoreApuShadowConfig(ini_path, false /* dryRun */);
        bool bak_deleted = !std::filesystem::exists(bak_path, ec);

        std::string restored_content;
        {
            std::ifstream in(ini_path, std::ios::in | std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            restored_content = ss.str();
        }
        bool restored_clean = (restored_content.find("r.Shadow.CSM.MaxCascades") == std::string::npos) &&
                              (restored_content.find("r.Shadow.DistanceScale") == std::string::npos) &&
                              (restored_content.find("Paths=../../../Engine/Content") != std::string::npos);

        // 7. Cleanup of the isolated test directories
        std::filesystem::remove_all(test_dir, ec);
        std::filesystem::remove_all(ac_dir, ec);

        bool t24_success = dry_apply_ok && dry_no_bak && dry_unchanged &&
                           apply_real_ok && bak_created && query_ok && values_match &&
                           preserved_other && ac_shield_ok && dry_restore_ok &&
                           bak_still_exists && restore_real_ok && bak_deleted &&
                           restored_clean;

        std::string details = "Dry-Run: " + std::string((dry_apply_ok && dry_no_bak && dry_unchanged) ? "PASS" : "FAIL") + ", "
            + "Apply & Query (CSM=2, Dist=0.7, Rad=0.04, AO=Half, Fog=0): " + std::string((apply_real_ok && query_ok && values_match && preserved_other) ? "PASS" : "FAIL") + ", "
            + "Kernel Anti-Cheat Shield (Genshin/HoYoKProtect): " + std::string(ac_shield_ok ? "PASS (Locked)" : "FAIL") + ", "
            + "Clean Restoration (.bak): " + std::string((restore_real_ok && bak_deleted && restored_clean) ? "PASS" : "FAIL");

        RecordResult(t24_success, "Test 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)", details);
    }
}

} // namespace vftest
