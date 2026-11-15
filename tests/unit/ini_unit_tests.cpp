// Unit tests: game INI & Unity boot.config editing (phase 2) through FakeFileSystem and tests/vectors/ini/.
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "optimize/steam/steam_engine_optimizer.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

std::string ReadVectorBytes(const std::string& rel) {
    std::ifstream in("tests/vectors/ini/" + rel, std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string Pass(bool b) { return b ? "PASS" : "FAIL"; }

} // namespace

void Test48() {
    using namespace vegaflow;

    const std::wstring cfg_dir = L"C:\\Games\\SampleGame\\Saved\\Config\\WindowsNoEditor";
    const std::wstring ini_path = cfg_dir + L"\\Engine.ini";
    const std::wstring bak_path = ini_path + L".vegaflow.bak";

    SteamGameEntry ue_game{};
    ue_game.engine = GameEngineType::UnrealEngine4_5;
    ue_game.name = "Sample UE Game";

    // 1. All valid INI vectors round-trip byte-for-byte and are idempotent on double apply.
    bool vectors_ok = true;
    std::string vec_fail;
    for (const char* name : {"utf8_lf.ini", "utf8_bom_crlf.ini", "utf16le_bom_crlf.ini", "mixed_eol.ini", "already_applied.ini"}) {
        const std::string orig = ReadVectorBytes(name);
        if (orig.empty()) {
            vectors_ok = false;
            vec_fail += std::string(" [missing:") + name + "]";
            continue;
        }
        FakeFileSystem fs;
        fs.files[ini_path] = orig;
        SteamEngineOptimizer opt(fs);

        const auto r1 = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        const std::string after1 = fs.files[ini_path];
        const auto r2 = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        const std::string after2 = fs.files[ini_path];
        const auto rr = opt.RestoreEngineOriginalConfig(ue_game, false, cfg_dir);

        const bool roundtrip = r1.success && r2.success && rr.success &&
                               (after1 == after2) &&
                               (fs.files[ini_path] == orig) &&
                               !fs.Exists(bak_path);
        if (!roundtrip) {
            vectors_ok = false;
            vec_fail += std::string(" [") + name + "]";
        }
    }

    // 2. Empty backup case: Engine.ini did not exist before apply -> restore removes it and .vegaflow.bak.
    bool empty_bak_ok = false;
    {
        FakeFileSystem fs;
        SteamEngineOptimizer opt(fs);
        const auto r1 = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        const bool created = r1.success && r1.backup_created && fs.Exists(ini_path) && fs.Exists(bak_path);
        const auto rr = opt.RestoreEngineOriginalConfig(ue_game, false, cfg_dir);
        empty_bak_ok = created && rr.success && !fs.Exists(ini_path) && !fs.Exists(bak_path);
    }

    // 3. Missing closing marker: must fail and preserve user content after [VegaFlowUMA_Start].
    bool missing_marker_ok = false;
    {
        const std::string broken = ReadVectorBytes("missing_end_marker.ini");
        FakeFileSystem fs;
        fs.files[ini_path] = broken;
        SteamEngineOptimizer opt(fs);
        const auto r = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        missing_marker_ok = !broken.empty() && !r.success && (fs.files[ini_path] == broken);
    }

    // 4. Backup copy failure on Apply: must stop and leave original Engine.ini untouched.
    bool fail_backup_ok = false;
    {
        const std::string orig = ReadVectorBytes("utf8_lf.ini");
        FakeFileSystem fs;
        fs.files[ini_path] = orig;
        fs.fail_copy = true;
        SteamEngineOptimizer opt(fs);
        const auto r = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        fail_backup_ok = !r.success && !r.backup_created && (fs.files[ini_path] == orig) && !fs.Exists(bak_path);
    }

    // 5. Restore copy failure or byte-for-byte mismatch: must fail and keep .vegaflow.bak.
    bool fail_restore_keeps_bak_ok = false;
    {
        const std::string orig = ReadVectorBytes("utf8_lf.ini");
        FakeFileSystem fs;
        fs.files[ini_path] = orig;
        SteamEngineOptimizer opt(fs);
        const auto r1 = opt.ApplyEngineUmaPreset(ue_game, DetectedPolicy(), 4096, false, cfg_dir);
        fs.corrupt_copy_target = true;
        const auto rr_corrupt = opt.RestoreEngineOriginalConfig(ue_game, false, cfg_dir);
        const bool kept_on_corrupt = r1.success && !rr_corrupt.success && fs.Exists(bak_path);

        fs.corrupt_copy_target = false;
        fs.fail_copy = true;
        const auto rr_copy_fail = opt.RestoreEngineOriginalConfig(ue_game, false, cfg_dir);
        const bool kept_on_copy_fail = !rr_copy_fail.success && fs.Exists(bak_path);
        fail_restore_keeps_bak_ok = kept_on_corrupt && kept_on_copy_fail;
    }

    // 6. Unity boot.config: spaces around '=', duplicate keys collapsed, idempotent, byte-for-byte restore.
    bool unity_ok = false;
    {
        const std::string u_orig = ReadVectorBytes("unity_spaces_dups.config");
        const std::wstring u_dir = L"C:\\Games\\UnityGame\\Sample_Data";
        const std::wstring u_boot = u_dir + L"\\boot.config";
        const std::wstring u_bak = u_boot + L".vegaflow.bak";

        SteamGameEntry unity_game{};
        unity_game.engine = GameEngineType::Unity;
        unity_game.name = "Sample Unity Game";

        FakeFileSystem fs;
        fs.files[u_boot] = u_orig;
        SteamEngineOptimizer opt(fs);
        const auto r1 = opt.ApplyEngineUmaPreset(unity_game, DetectedPolicy(), 4096, false, u_dir);
        const std::string after1 = fs.files[u_boot];
        const auto r2 = opt.ApplyEngineUmaPreset(unity_game, DetectedPolicy(), 4096, false, u_dir);
        const std::string after2 = fs.files[u_boot];

        const size_t first_pos = after1.find("gfx-enable-gfx-jobs=1");
        const bool single_key = (first_pos != std::string::npos) &&
                                (after1.find("gfx-enable-gfx-jobs", first_pos + 1) == std::string::npos);
        const auto rr = opt.RestoreEngineOriginalConfig(unity_game, false, u_dir);
        unity_ok = !u_orig.empty() && r1.success && r2.success && rr.success &&
                   single_key && (after1 == after2) && (fs.files[u_boot] == u_orig) && !fs.Exists(u_bak);
    }

    // 7. ApuShadowConfig with UTF-8 BOM at start: no duplicate [SystemSettings], BOM kept, .vegaflow.bak restore.
    bool shadow_bom_ok = false;
    {
        const std::string s_orig = ReadVectorBytes("shadow_bom.ini");
        FakeFileSystem fs;
        fs.files[ini_path] = s_orig;
        SteamEngineOptimizer opt(fs);

        ApuShadowConfig cfg{};
        cfg.max_cascades = 2;
        cfg.distance_scale = 0.7f;
        cfg.radius_threshold = 0.04f;
        cfg.half_res_ao = true;
        cfg.volumetric_fog = 0;

        const bool applied = opt.ApplyApuShadowConfig(ini_path, cfg, false);
        const std::string& modified = fs.files[ini_path];
        const size_t sec1 = modified.find("[SystemSettings]");
        const bool single_sec = (sec1 != std::string::npos) &&
                                (modified.find("[SystemSettings]", sec1 + 1) == std::string::npos);
        const bool has_bom = modified.size() >= 3 &&
                             static_cast<unsigned char>(modified[0]) == 0xEF &&
                             static_cast<unsigned char>(modified[1]) == 0xBB &&
                             static_cast<unsigned char>(modified[2]) == 0xBF;
        ApuShadowConfig queried{};
        const bool query_ok = opt.QueryApuShadowConfig(ini_path, queried) && queried.max_cascades == 2;
        const bool restored = opt.RestoreApuShadowConfig(ini_path, false);
        shadow_bom_ok = !s_orig.empty() && applied && single_sec && has_bom && query_ok &&
                        restored && (fs.files[ini_path] == s_orig) && !fs.Exists(bak_path);
    }

    const bool ok = vectors_ok && empty_bak_ok && missing_marker_ok && fail_backup_ok &&
                    fail_restore_keeps_bak_ok && unity_ok && shadow_bom_ok;
    const std::string details =
        "vectors: " + Pass(vectors_ok) + vec_fail +
        ", empty-bak: " + Pass(empty_bak_ok) +
        ", missing-marker: " + Pass(missing_marker_ok) +
        ", fail-backup: " + Pass(fail_backup_ok) +
        ", restore-verify: " + Pass(fail_restore_keeps_bak_ok) +
        ", unity-spaces-dups: " + Pass(unity_ok) +
        ", shadow-bom: " + Pass(shadow_bom_ok);
    RecordResult(ok, "Test 48: INI & Unity editor preserves encoding, BOM, EOL, markers, and verified backups", details);
}

} // namespace vftest
