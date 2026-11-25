// Unit tests (F4.3): the Juegos buttons reach game files through the GameFiles port, so a fake that only records the
// calls is enough to check what they ask for. Runs on the host: no Steam, DXVK or Windows involved.

#include "ports/game_files.hpp"
#include "session/game_actions.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::GameEngineType;
using vegaflow::SteamGameEntry;

class FakeGameFiles final : public vegaflow::GameFiles {
public:
    bool apply_ok = true;
    bool restore_ok = true;
    bool dxvk_ok = true;
    std::vector<std::string> calls;
    uint32_t last_pool_mb = 0;
    bool any_dry_run = false;

    vegaflow::EngineOptimizationReport ApplyEngineUmaPreset(const SteamGameEntry& game, const vegaflow::ApuOptimizationPolicy&,
                                                            uint32_t vram_pool_mb, bool dry_run) override {
        calls.push_back("apply:" + std::to_string(game.app_id));
        last_pool_mb = vram_pool_mb;
        any_dry_run = any_dry_run || dry_run;
        vegaflow::EngineOptimizationReport r;
        r.success = apply_ok;
        r.status_message = apply_ok ? "applied" : "apply failed";
        return r;
    }
    vegaflow::EngineOptimizationReport RestoreEngineOriginalConfig(const SteamGameEntry& game, bool dry_run) override {
        calls.push_back("restore:" + std::to_string(game.app_id));
        any_dry_run = any_dry_run || dry_run;
        vegaflow::EngineOptimizationReport r;
        r.success = restore_ok;
        r.status_message = restore_ok ? "restored" : "restore failed";
        return r;
    }
    bool RemoveDxvk(const std::wstring&, bool dry_run, std::string* out_message) override {
        calls.push_back("dxvk-remove");
        any_dry_run = any_dry_run || dry_run;
        if (out_message) *out_message = dxvk_ok ? "dxvk removed" : "dxvk failed";
        return dxvk_ok;
    }
};

SteamGameEntry Game(uint32_t id, bool anticheat = false, bool preset = false, bool dxvk = false) {
    SteamGameEntry g;
    g.app_id = id;
    g.name = "Game" + std::to_string(id);
    g.engine = GameEngineType::Unity;
    g.install_dir = L"C:\\Steam\\steamapps\\common\\Game" + std::to_wstring(id);
    g.primary_exe_path = g.install_dir + L"\\Game.exe";
    g.has_anticheat = anticheat;
    g.engine_preset_applied = preset;
    g.dxvk_installed = dxvk;
    return g;
}

} // namespace

void Test145() {
    FakeFileSystem fs;
    FakeGameFiles files;
    vegaflow::app::ConfigStore store(fs, L"C:\\VegaFlow\\config.json", true);
    vegaflow::app::AppConfig config = store.Load();
    std::vector<SteamGameEntry> games = {Game(1), Game(2, true), Game(3, false, true), Game(4, false, false, true)};
    vegaflow::ApuOptimizationPolicy policy{};
    policy.safe_vram_pool_mb = 2048;
    std::vector<std::string> audit;
    gs::GameActions act(gs::GameActionsDeps{files, config, store, games, policy,
                                            []() { return std::vector<SteamGameEntry>{}; },
                                            [](const SteamGameEntry& g) { return g; },
                                            [&](const std::string& s) { audit.push_back(s); }});

    // 1. Apply asks the port for the policy's safe pool, for real (never dry_run), and a success sets both flags.
    const std::string msg = act.Apply(1);
    RecordResult(files.calls == std::vector<std::string>{"apply:1"} && files.last_pool_mb == 2048 && !files.any_dry_run && msg == "applied" &&
                     games[0].engine_preset_applied && games[0].flip_model_applied,
                 "Test 145.1: Apply asks the GameFiles port for the policy's pool, not as a dry run, and a success sets the flags", msg);

    // 2. A failure leaves the flags alone and says what the port said.
    files.apply_ok = false;
    const std::string bad = act.Apply(2);
    RecordResult(bad == "apply failed" && !games[1].engine_preset_applied, "Test 145.2: a failed Apply sets no flag and reports the port's sentence", bad);

    // 3. Restore clears the profile flag only on success.
    files.restore_ok = false;
    act.Restore(3);
    const bool kept = games[2].engine_preset_applied;
    files.restore_ok = true;
    act.Restore(3);
    RecordResult(kept && !games[2].engine_preset_applied, "Test 145.3: Restore clears the profile flag only when the port restored the file");

    // 4. Excluding a game with an applied profile gives the original file back first; an anti-cheat game never reaches the port.
    files.calls.clear();
    games[2].engine_preset_applied = true;
    const std::string ex = act.Exclude(3);
    const bool restored_first = files.calls == std::vector<std::string>{"restore:3"} && !games[2].engine_preset_applied;
    files.calls.clear();
    act.ToggleDxvk(2);
    RecordResult(restored_first && files.calls.empty() && ex.find("Juego excluido") != std::string::npos,
                 "Test 145.4: Exclude restores the applied profile through the port; DXVK on an anti-cheat game never reaches it", ex);

    // 5. DXVK removal goes through the port only for a game that has it, and clears the flag on success.
    files.calls.clear();
    act.ToggleDxvk(1); // nothing to remove
    const bool none = files.calls.empty();
    const std::string rm = act.ToggleDxvk(4);
    RecordResult(none && files.calls == std::vector<std::string>{"dxvk-remove"} && rm == "dxvk removed" && !games[3].dxvk_installed && !files.any_dry_run,
                 "Test 145.5: RemoveDxvk is asked only for a game that has DXVK, and a success clears its flag", rm);
}

} // namespace vftest
