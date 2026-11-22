// Unit test (F1.5): game files are never written without a copy of the user's version, a foreign Engine.ini.bak is not
// VegaFlow's copy, and an empty config the user really had is not mistaken for "the file did not exist".

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "optimize/steam/steam_engine_optimizer.hpp"
#include "windows/change_journal.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test126() {
    using namespace vegaflow;
    const std::wstring cfg_dir = L"C:\\Games\\SampleGame\\Saved\\Config\\WindowsNoEditor";
    const std::wstring ini = cfg_dir + L"\\Engine.ini";
    const std::wstring bak = ini + L".vegaflow.bak";
    const std::string user_ini = "[Core]\nA=1\n";
    SteamGameEntry game{};
    game.engine = GameEngineType::UnrealEngine4_5;
    game.name = "Sample UE Game";

    // The restore below starts by copying the live file to .vegaflow.bak (after Apply's own copy was erased).
    auto copied_first = [&](FakeFileSystem& fs) {
        for (size_t i = fs.copies.size(); i-- > 0;) {
            if (fs.copies[i] == ini + L" -> " + bak) return true;
        }
        return false;
    };

    // 1. Engine.ini with VegaFlow's block but the .vegaflow.bak is gone: the restore copies first, so a failed write
    //    leaves the file as it was; once it can write, the block goes and the temporary copy too.
    bool engine_ok = false;
    std::string engine_detail;
    {
        FakeFileSystem fs;
        fs.files[ini] = user_ini;
        SteamEngineOptimizer opt(fs);
        const bool applied = opt.ApplyEngineUmaPreset(game, DetectedPolicy(), 4096, false, cfg_dir).success;
        const std::string with_block = fs.files[ini];
        fs.files.erase(bak);
        fs.copies.clear();
        fs.fail_write = true;
        const auto failed = opt.RestoreEngineOriginalConfig(game, false, cfg_dir);
        // The copy was made before the write, the file is as it was, and no stale copy is left behind.
        const bool copy_kept = copied_first(fs) && fs.files[ini] == with_block && !fs.Exists(bak);
        fs.fail_write = false;
        const auto done = opt.RestoreEngineOriginalConfig(game, false, cfg_dir);
        engine_ok = applied && !failed.success && copy_kept && done.success &&
                    fs.files[ini].find("VegaFlowUMA_Start") == std::string::npos && !fs.Exists(bak);
        engine_detail = std::string("applied=") + (applied ? "1" : "0") + " failedWrite=" + (failed.success ? "ok" : "refused") +
                        " copyKept=" + (copy_kept ? "1" : "0") + " done=" + (done.success ? "1" : "0");
    }

    // 2. Same for the APU shadow keys.
    bool shadow_ok = false;
    std::string shadow_detail;
    {
        FakeFileSystem fs;
        fs.files[ini] = "[SystemSettings]\nUser=1\n";
        SteamEngineOptimizer opt(fs);
        ApuShadowConfig cfg{};
        cfg.max_cascades = 2;
        cfg.distance_scale = 0.7f;
        cfg.radius_threshold = 0.04f;
        const bool applied = opt.ApplyApuShadowConfig(ini, cfg, false);
        const std::string with_keys = fs.files[ini];
        fs.files.erase(bak);
        fs.copies.clear();
        fs.fail_write = true;
        const bool failed = opt.RestoreApuShadowConfig(ini, false);
        const bool copy_kept = copied_first(fs) && fs.files[ini] == with_keys && !fs.Exists(bak);
        fs.fail_write = false;
        const bool done = opt.RestoreApuShadowConfig(ini, false);
        shadow_ok = applied && !failed && copy_kept && done && fs.files[ini].find("MaxCascades") == std::string::npos && !fs.Exists(bak);
        shadow_detail = std::string("applied=") + (applied ? "1" : "0") + " failedWrite=" + (failed ? "ok" : "refused") +
                        " copyKept=" + (copy_kept ? "1" : "0") + " done=" + (done ? "1" : "0");
    }

    // 3. A generic Engine.ini.bak (another tool's) is not put over the live ini; one VegaFlow marked as "absent" still is honored.
    bool foreign_ok = false;
    {
        FakeFileSystem fs;
        const std::string live = "[SystemSettings]\nr.Shadow.CSM.MaxCascades=2\nUser=1\n";
        fs.files[ini] = live;
        fs.files[ini + L".bak"] = "foreign tool backup\n";
        SteamEngineOptimizer opt(fs);
        opt.RestoreApuShadowConfig(ini, false);
        const bool foreign_not_applied = fs.files[ini] != "foreign tool backup\n" && fs.files[ini].find("User=1") != std::string::npos &&
                                         fs.files[ini + L".bak"] == "foreign tool backup\n";
        FakeFileSystem fs2;
        fs2.files[ini] = "[SystemSettings]\nr.Shadow.CSM.MaxCascades=2\n";
        fs2.files[ini + L".bak"] = "; VegaFlow Empty Backup\n";
        const bool marked_ok = SteamEngineOptimizer(fs2).RestoreApuShadowConfig(ini, false) && !fs2.Exists(ini) && !fs2.Exists(ini + L".bak");
        foreign_ok = foreign_not_applied && marked_ok;
    }

    // 4. Crash recovery: an empty config the user really had comes back empty; only the "did not exist" mark deletes the file.
    bool empty_ok = false;
    {
        FakeFileSystem fs;
        fs.files[ini] = "[SystemSettings]\nr.Shadow.CSM.MaxCascades=2\n";
        fs.files[bak] = "";
        WindowsSystemUndo undo(fs);
        JournalEntry e;
        e.kind = JournalTargetKind::GameConfigFile;
        e.file_path = ini;
        e.backup_path = bak;
        const bool reverted = undo.RevertEntry(e, nullptr);
        const bool keeps_empty_file = reverted && fs.files.count(ini) && fs.files[ini].empty() && !fs.Exists(bak);

        FakeFileSystem fs2;
        fs2.files[ini] = "created by VegaFlow\n";
        fs2.files[bak] = "; VegaFlow Empty Backup\n";
        JournalEntry e2 = e;
        const bool absent_removed = WindowsSystemUndo(fs2).RevertEntry(e2, nullptr) && !fs2.Exists(ini) && !fs2.Exists(bak);
        empty_ok = keeps_empty_file && absent_removed;
    }

    RecordResult(engine_ok && shadow_ok && foreign_ok && empty_ok,
                 "Test 126: game files never change without a copy; foreign .bak and real empty configs are respected (F1.5)",
                 "Engine=" + std::string(engine_ok ? "OK" : "FAIL") + " (" + engine_detail + "), Shadow=" + (shadow_ok ? "OK" : "FAIL") + " (" +
                     shadow_detail + "), ForeignBak=" + (foreign_ok ? "OK" : "FAIL") + ", EmptyConfig=" + (empty_ok ? "OK" : "FAIL"));
}

} // namespace vftest
