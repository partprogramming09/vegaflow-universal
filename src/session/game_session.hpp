#pragma once

// Per-game session orchestration and hardware profile conversion / hot-plug refresh (phase 3 & phase 5).

#include "model/hardware.hpp"
#include "model/game.hpp"
#include "ports/change_journal.hpp"
#include "ports/device_probe.hpp"
#include "ports/file_system.hpp"
#include "session/config.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vegaflow {

class CpuOrchestrator;
struct SessionMemoryTelemetry;
struct ApuShadowConfig;
struct EngineOptimizationReport;

} // namespace vegaflow

namespace vegaflow::session {

struct GameSessionState {
    bool game_active = false;
    uint32_t active_game_pid = 0;
    uint64_t active_game_creation_time = 0;
    std::wstring active_game_exe;
    std::wstring active_game_path;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool active_game_anticheat = false;
};

// Pure conversion: resolves primary GPU, connection, arch_gen, and APU tier via Rust device_profile.rs
// from a RawDeviceSnapshot (used by unit tests with FakeDeviceProbe and by live refresh).
SystemHardwareProfile BuildHardwareProfileFromSnapshot(
    const RawDeviceSnapshot& snap,
    bool experimental_egpu_streaming = false,
    uint8_t user_gpu_pref = 0);

// Checks whether DXCore/DXGI adapter list or per-game GPU preference changed since last cycle.
// When changed, re-probes hardware via `probe` and rebuilds `hw` (returning true).
bool RefreshHardwareProfileIfChanged(
    DeviceProbe& probe,
    SystemHardwareProfile& hw,
    uint8_t user_gpu_pref = 0,
    bool probe_signature = true);  // E2.4: false skips the signature read (the device event does it instead)

// Injectable session environment so RunGameSessionCycle can be tested with fake processes, registry, and power plans.
struct GameSessionEnvironment {
    virtual ~GameSessionEnvironment() = default;

    virtual std::optional<DetectedGameProcess> DetectActiveGame(const ProcessPolicyConfig& policy) = 0;
    virtual bool IsWindowValid(HWND hwnd) = 0;
    virtual bool IsProcessAlive(uint32_t pid) = 0;
    virtual uint32_t WindowThreadId(HWND hwnd) = 0; // the thread that owns the game window (its presenter)
    // F5.2: creation time (FILETIME) of a process; 0 = unknown. A windowless target is only trusted from the cache
    // when this matches the one saved, so a reused PID is never taken for the target.
    virtual uint64_t ProcessCreationTime(uint32_t /*pid*/) { return 0; }
    // F5.2: only the foreground window's process, if it inspects as a game. A cached windowless target is never allowed
    // to hide a game the person just brought to the front.
    virtual std::optional<DetectedGameProcess> DetectForegroundGame(const ProcessPolicyConfig& /*policy*/) { return std::nullopt; }
    virtual std::vector<BackgroundProcessTarget> ClassifyBackgroundProcesses(
        uint32_t game_pid,
        const ProcessPolicyConfig& policy) = 0;

    virtual void QueryLiveMemory(uint64_t& out_local_vram_budget_bytes, uint64_t& out_local_vram_usage_bytes, bool& out_vram_valid) = 0;

    virtual void SetDynamicPrioritySeparation(uint32_t val) = 0;
    virtual void SetGameDvrCaptureDisabled(bool disable) = 0;
    virtual void SetApuLaptopPowerProfile(bool optimize, const ApuOptimizationPolicy& static_policy) = 0;
    virtual void ApplyDxgiFlipModelUpgrade(const std::wstring& exe_path) = 0;

    virtual SteamGameEntry InspectGameDirectory(const std::wstring& dir, uint32_t app_id, const std::string& name) = 0;
    virtual void ApplyEngineUmaPreset(const SteamGameEntry& game, const ApuOptimizationPolicy& static_policy, uint32_t pool_mb) = 0;

    virtual void ApplyCpuOptimizations(
        uint32_t game_pid,
        uint64_t game_creation_time,
        uint32_t presenter_thread_id,
        bool is_dx12,
        const std::vector<ULONG>& game_sets,
        const std::vector<ULONG>& bg_sets,
        const std::vector<BackgroundProcessTarget>& bg_targets,
        uint32_t& out_throttled) = 0;
    virtual void RestoreCpuAll() = 0;

    virtual void EvaluateMemory(uint32_t game_pid, const std::vector<uint32_t>& bg_pids, bool allow_ram_purge) = 0;
    virtual void EvaluateUmaGuard(const ApuOptimizationPolicy& live_policy, const std::vector<uint32_t>& bg_pids) = 0;

    virtual void SaveSessionReport(const std::string& exe_utf8, const std::string& api_name, uint32_t pid) = 0;
};

void RunGameSessionCycle(
    const app::AppConfig& config,
    DeviceProbe& device_probe,
    GameSessionEnvironment& env,
    SystemHardwareProfile& hw,
    GameSessionState& state,
    uint32_t* out_noisy_bg_count = nullptr,
    const std::function<void(const std::string&)>& audit_cb = {},
    bool check_adapters = true);  // E2.4: false = no adapter-signature read this cycle

} // namespace vegaflow::session
