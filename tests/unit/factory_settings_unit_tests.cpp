// Unit test for E2.2: the four settings that used to apply themselves are opt-in keys of config.json (off in a factory
// file), and the compression row is inverted ("Apagar la compresión de memoria") with a RAM-based advice from Rust.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "bridge/rust_bridge.h"
#include "session/config.hpp"
#include "session/game_session.hpp"
#include "session/settings_screen.hpp"
#include "fake_device_probe.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <optional>
#include <string>
#include <vector>

namespace vftest {

namespace {

// Records what a game session asks of the system; everything else is inert.
class RecordingEnv final : public vegaflow::session::GameSessionEnvironment {
public:
    std::optional<vegaflow::DetectedGameProcess> active_game;
    std::vector<uint32_t> priority_values;
    int dvr_calls = 0;
    int power_calls = 0;
    int preset_calls = 0;
    bool unreal = false; // the game folder holds an Unreal Engine game

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const vegaflow::ProcessPolicyConfig&) override { return active_game; }
    bool IsWindowValid(HWND) override { return true; }
    uint32_t WindowThreadId(HWND) override { return 0; }
    bool IsProcessAlive(uint32_t) override { return active_game.has_value(); }
    std::vector<vegaflow::BackgroundProcessTarget> ClassifyBackgroundProcesses(uint32_t, const vegaflow::ProcessPolicyConfig&) override { return {}; }
    void QueryLiveMemory(uint64_t& b, uint64_t& u, bool& v) override { b = 0; u = 0; v = false; }
    void SetDynamicPrioritySeparation(uint32_t val) override { priority_values.push_back(val); }
    void SetGameDvrCaptureDisabled(bool) override { ++dvr_calls; }
    void SetApuLaptopPowerProfile(bool optimize, const vegaflow::ApuOptimizationPolicy&) override { power_calls += optimize ? 1 : 0; }
    void ApplyDxgiFlipModelUpgrade(const std::wstring&) override {}
    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring&, uint32_t, const std::string&) override {
        vegaflow::SteamGameEntry e{};
        if (unreal) e.engine = vegaflow::GameEngineType::UnrealEngine4_5;
        return e;
    }
    void ApplyEngineUmaPreset(const vegaflow::SteamGameEntry&, const vegaflow::ApuOptimizationPolicy&, uint32_t) override { ++preset_calls; }
    void ApplyCpuOptimizations(uint32_t, uint64_t, uint32_t, bool, const std::vector<ULONG>&, const std::vector<ULONG>&,
                               const std::vector<vegaflow::BackgroundProcessTarget>&, uint32_t& out_throttled) override { out_throttled = 0; }
    void RestoreCpuAll() override {}
    void EvaluateMemory(uint32_t, const std::vector<uint32_t>&, bool) override {}
    void EvaluateUmaGuard(const vegaflow::ApuOptimizationPolicy&, const std::vector<uint32_t>&) override {}
    void SaveSessionReport(const std::string&, const std::string&, uint32_t) override {}
};

// One new game on a Vega-class APU laptop: what the session asked of the system with this config.
RecordingEnv RunOneGame(const vegaflow::app::AppConfig& cfg, vegaflow::RenderApi api, bool unreal = false) {
    FakeDeviceProbe probe;
    probe.snapshot.dxcore_available = true;
    probe.snapshot.ram_probe_ok = true;
    probe.snapshot.total_physical_ram_bytes = 16ULL * 1024 * 1024 * 1024;
    probe.snapshot.cpu_probe_ok = true;
    probe.snapshot.cpu_topology.physical_cores = 6;
    probe.snapshot.power_status = {true, true, true, false, 100};
    probe.snapshot.system_arch = vegaflow::SystemArch::X64;
    probe.snapshot.adapter_signature = 1;
    vegaflow::GpuProfile igpu{};
    igpu.vendor_id = 0x1002;
    igpu.device_id = 0x1636;
    igpu.vendor = vegaflow::GpuVendor::AMD;
    igpu.dxcore_available = true;
    igpu.dxcore_integrated = true;
    igpu.description = L"AMD Radeon(TM) Graphics";
    probe.snapshot.gpus.push_back(igpu);
    auto hw = vegaflow::session::BuildHardwareProfileFromSnapshot(probe.ProbeSystem(false), false, 0);
    RecordingEnv env;
    env.unreal = unreal;
    vegaflow::DetectedGameProcess dg{};
    dg.pid = 4242;
    dg.exe_name = L"Game.exe";
    dg.full_path = L"C:\\Games\\Game\\Game.exe";
    dg.api = api;
    dg.main_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234));
    env.active_game = dg;
    vegaflow::session::GameSessionState state{};
    uint32_t noisy = 0;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state, &noisy);
    return env;
}

const wchar_t* const kPath = L"C:\\vf\\config.json";

} // namespace

void Test111() {
    using vegaflow::app::AppConfig;
    using vegaflow::app::ConfigStore;

    // 1 (plan #1). A config.json without the keys: the four are off and a detected game changes nothing.
    FakeFileSystem fs_old;
    fs_old.files[kPath] = "{\n  \"version\": 1,\n  \"poll_interval_ms\": 2000\n}\n";
    ConfigStore store_old(fs_old, kPath, true);
    const AppConfig old_cfg = store_old.Load();
    const bool four_off = !old_cfg.auto_vram_reporting && !old_cfg.auto_priority_separation && !old_cfg.auto_game_dvr_off &&
                          !old_cfg.auto_power_profile;
    const RecordingEnv quiet = RunOneGame(old_cfg, vegaflow::RenderApi::DirectX12);
    const bool nothing_applied = quiet.priority_values.empty() && quiet.dvr_calls == 0 && quiet.power_calls == 0;
    RecordResult(four_off && nothing_applied && AppConfig{}.auto_power_profile == false,
                 "Test 111.1: a config.json without the new keys has the four off and a detected game changes nothing");

    // 2 (plan #2). Each opt-in applies only its own setting: 0x26 with DirectX 12, 0x28 with DirectX 11.
    AppConfig prio;
    prio.auto_priority_separation = true;
    const RecordingEnv p12 = RunOneGame(prio, vegaflow::RenderApi::DirectX12);
    const RecordingEnv p11 = RunOneGame(prio, vegaflow::RenderApi::DirectX11);
    AppConfig dvr;
    dvr.auto_game_dvr_off = true;
    const RecordingEnv d = RunOneGame(dvr, vegaflow::RenderApi::DirectX12);
    AppConfig power;
    power.auto_power_profile = true;
    const RecordingEnv w = RunOneGame(power, vegaflow::RenderApi::DirectX12);
    RecordResult(p12.priority_values == std::vector<uint32_t>{0x26} && p11.priority_values == std::vector<uint32_t>{0x28} &&
                     p12.dvr_calls == 0 && p12.power_calls == 0 && d.dvr_calls == 1 && d.priority_values.empty() &&
                     d.power_calls == 0 && w.power_calls == 1 && w.priority_values.empty() && w.dvr_calls == 0,
                 "Test 111.2: auto_priority_separation writes 0x26/0x28 as before, and each opt-in applies only its own setting");

    // 3 (plan #3). A key of the wrong type is rejected by the existing validator: it stays false and blocks saving.
    FakeFileSystem fs_bad;
    fs_bad.files[kPath] = "{\n  \"version\": 1,\n  \"auto_game_dvr_off\": \"yes\"\n}\n";
    ConfigStore store_bad(fs_bad, kPath, true);
    const AppConfig bad_cfg = store_bad.Load();
    RecordResult(!bad_cfg.auto_game_dvr_off && (store_bad.Report().status & VF_CONFIG_KEY_REJECTED) != 0 &&
                     (store_bad.Report().rejected_keys & VF_CONFIG_KEY_AUTO_GAME_DVR_OFF) != 0 && !store_bad.Report().CanSave(),
                 "Test 111.3: auto_game_dvr_off = \"yes\" is rejected, stays false and blocks saving");

    // 4 + 12 (plan #4, #12). Saving twice the same opt-in gives the same file, no duplicate keys, unknown keys kept.
    FakeFileSystem fs_save;
    fs_save.files[kPath] = "{\n  \"version\": 1,\n  \"my_note\": \"keep me\"\n}\n";
    ConfigStore store_save(fs_save, kPath, true);
    AppConfig edit = store_save.Load();
    edit.auto_priority_separation = true;
    const bool first = store_save.Save(edit);
    const std::string after_first = fs_save.files[kPath];
    ConfigStore store_again(fs_save, kPath, true);
    AppConfig again = store_again.Load();
    const bool second = store_again.Save(again);
    const std::string after_second = fs_save.files[kPath];
    size_t keys = 0;
    for (size_t at = after_second.find("\"auto_priority_separation\""); at != std::string::npos;
         at = after_second.find("\"auto_priority_separation\"", at + 1)) ++keys;
    ConfigStore store_back(fs_save, kPath, true);
    const AppConfig back = store_back.Load();
    RecordResult(first && second && after_first == after_second && keys == 1 && after_second.find("keep me") != std::string::npos &&
                     back.auto_priority_separation && !back.auto_game_dvr_off,
                 "Test 111.4: saving the same opt-in twice gives an identical config.json, one key, unknown keys kept");

    // 5 (plan #5). No catalog row is applied by default (the old 61.10 pinned four).
    bool none_default = true;
    for (const auto& s : vegaflow::SettingsCatalog()) none_default = none_default && !s.applied_by_default;
    RecordResult(none_default, "Test 111.5: no catalog row is applied by default");

    // 6 (plan #6). Windows ships with compression on: the inverted row shows off; with it off by VegaFlow it shows on.
    vegaflow::SettingsScreenInputs in;
    in.device = {vegaflow::GpuVendor::AMD, vegaflow::GpuConnection::Integrated, true};
    in.show_experimental = true;
    in.tweaks.memory_compression_enabled = true;
    auto find_row = [](const vegaflow::SettingsScreenModel& m) -> const vegaflow::SettingRow* {
        for (const auto& f : m.families)
            for (const auto& r : f.rows)
                if (std::string(r.spec->id) == "memory_compression_off") return &r;
        return nullptr;
    };
    const auto windows_default = vegaflow::BuildSettingsScreen(in);
    in.tweaks.memory_compression_enabled = false;
    const auto vegaflow_turned_off = vegaflow::BuildSettingsScreen(in);
    const auto* r1 = find_row(windows_default);
    const auto* r2 = find_row(vegaflow_turned_off);
    RecordResult(r1 && r2 && !r1->on && r2->on && std::string(r1->spec->name) == "Apagar la compresión de memoria" &&
                     vegaflow::FindSetting("memory_compression") == nullptr,
                 "Test 111.6: with compression active (Windows default) the memory_compression_off row is off; off by VegaFlow it is on");

    // 7, 8, 11 (plan). The RAM rule through the bridge matches the vectors shared with Rust.
    int rows = 0;
    std::string mismatch;
    ForEachTsvRow("tests/vectors/memory_compression.tsv", [&](const std::vector<std::string>& c, const std::string& line) {
        ++rows;
        if (c.size() != 4) { mismatch += "bad row: " + line + "; "; return; }
        const uint8_t got = vf_rust_memory_compression_off_advice(std::stoull(c[1]), c[2] == "1");
        const uint8_t want = c[3] == "allowed" ? 0 : c[3] == "discouraged" ? 1 : 2;
        if (got != want) mismatch += c[0] + "; ";
    });
    RecordResult(rows >= 10 && mismatch.empty() && vf_rust_memory_compression_off_advice(16384, true) == 1 &&
                     vf_rust_memory_compression_off_advice(16385, true) == 0 && vf_rust_memory_compression_off_advice(0, true) == 2 &&
                     vf_rust_memory_compression_off_advice(UINT64_MAX, true) == 0,
                 "Test 111.7: the compression advice (16384 discouraged, 16385 allowed, 0 unknown, u64::MAX allowed) matches the shared vectors", mismatch);

    // 9 (plan #9). A failed RAM probe is unknown and the row asks first; discouraged asks too; allowed does not.
    auto row_for = [&](vegaflow::CompressionAdvice a) {
        vegaflow::SettingsScreenInputs i = in;
        i.compression_advice = a;
        const auto m = vegaflow::BuildSettingsScreen(i);
        return *find_row(m);
    };
    const auto unknown = row_for(vegaflow::CompressionAdvice::Unknown);
    const auto discouraged = row_for(vegaflow::CompressionAdvice::Discouraged);
    const auto allowed = row_for(vegaflow::CompressionAdvice::Allowed);
    const bool probe_failed_unknown = vf_rust_memory_compression_off_advice(8192, false) == 2;
    RecordResult(probe_failed_unknown && unknown.needs_confirm && !unknown.discouraged && discouraged.needs_confirm &&
                     discouraged.discouraged && !allowed.needs_confirm && !allowed.discouraged,
                 "Test 111.9: a failed RAM probe is unknown and the row asks for confirmation; discouraged asks and is flagged; allowed does not");

    // F2.3. auto_engine_presets: a file without the key is off and an Unreal game gets no preset; true applies it once;
    // a value of another type is rejected, stays off and blocks saving.
    FakeFileSystem fs_none;
    fs_none.files[kPath] = "{\n  \"version\": 1\n}\n";
    ConfigStore store_none(fs_none, kPath, true);
    const AppConfig none_cfg = store_none.Load();
    FakeFileSystem fs_on;
    fs_on.files[kPath] = "{\n  \"version\": 1,\n  \"auto_engine_presets\": true\n}\n";
    ConfigStore store_on(fs_on, kPath, true);
    const AppConfig on_cfg = store_on.Load();
    FakeFileSystem fs_str;
    fs_str.files[kPath] = "{\n  \"version\": 1,\n  \"auto_engine_presets\": \"true\"\n}\n";
    ConfigStore store_str(fs_str, kPath, true);
    const AppConfig str_cfg = store_str.Load();
    const RecordingEnv ue_off = RunOneGame(none_cfg, vegaflow::RenderApi::DirectX12, true);
    const RecordingEnv ue_on = RunOneGame(on_cfg, vegaflow::RenderApi::DirectX12, true);
    const RecordingEnv ue_str = RunOneGame(str_cfg, vegaflow::RenderApi::DirectX12, true);
    RecordResult(!none_cfg.auto_engine_presets && on_cfg.auto_engine_presets && ue_off.preset_calls == 0 && ue_on.preset_calls == 1 &&
                     ue_str.preset_calls == 0 && !str_cfg.auto_engine_presets &&
                     (store_str.Report().rejected_keys & VF_CONFIG_KEY_AUTO_ENGINE_PRESETS) != 0 && !store_str.Report().CanSave() &&
                     store_str.Report().Warning().find("auto_engine_presets") != std::string::npos,
                 "Test 111.10: auto_engine_presets absent = no Unreal preset, true = one preset, \"true\" = rejected with a warning and off");

    // F2.3. The Ajustes row for the key: experimental, shown on/off from the config, and it does not promise a restore it cannot do.
    const vegaflow::SettingSpec* preset_spec = vegaflow::FindSetting("engine_presets");
    vegaflow::SettingsScreenInputs pin;
    pin.device = {vegaflow::GpuVendor::AMD, vegaflow::GpuConnection::Integrated, true};
    pin.show_experimental = true;
    auto preset_row = [&](bool experimental, bool cfg_on, const vegaflow::SettingRow** out) {
        pin.show_experimental = experimental;
        pin.auto_engine_presets = cfg_on;
        static vegaflow::SettingsScreenModel model;
        model = vegaflow::BuildSettingsScreen(pin);
        *out = nullptr;
        for (const auto& f : model.families)
            for (const auto& r : f.rows)
                if (std::string(r.spec->id) == "engine_presets") *out = &r;
    };
    const vegaflow::SettingRow* p_off = nullptr;
    preset_row(true, false, &p_off);
    const bool off_ok = p_off && !p_off->on && p_off->preview.find("Engine.ini") != std::string::npos &&
                        p_off->preview.find("devuelve el valor que tenía") == std::string::npos;
    const vegaflow::SettingRow* p_on = nullptr;
    preset_row(true, true, &p_on);
    const bool on_ok = p_on && p_on->on;
    const vegaflow::SettingRow* p_hidden = nullptr;
    preset_row(false, true, &p_hidden);
    RecordResult(preset_spec && preset_spec->experimental && preset_spec->user_switch && preset_spec->config_key &&
                     !preset_spec->applied_by_default && off_ok && on_ok && p_hidden == nullptr,
                 "Test 111.11: the engine_presets row is experimental, mirrors the config key and its text does not promise a registry restore");

    // 10 (plan #10). In safe mode nothing is applied: the startup and session code stays behind `!safe_mode`, unchanged.
    // Checked on the laptop (daemon started with --safe-mode); there is no seam to run RunDaemon here.
}

} // namespace vftest
