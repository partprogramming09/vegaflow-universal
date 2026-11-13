#include "session/run_cycle.hpp"

#include <windows.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "safety/frame_telemetry.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "session/game_session.hpp"
#include "session/text_convert.hpp"
#include "windows/device_probe.hpp"

namespace vegaflow::app {

namespace {

class WindowsGameSessionEnv final : public vegaflow::session::GameSessionEnvironment {
public:
    WindowsGameSessionEnv(
        vegaflow::FileSystem& fs,
        vegaflow::CpuOrchestrator& cpu,
        vegaflow::MemoryManager& mem,
        vegaflow::FrameTelemetryMonitor& telem,
        std::function<void(const std::string&)>* audit_cb)
        : fs_(fs), cpu_(cpu), mem_(mem), telem_(telem), audit_cb_(audit_cb) {}

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const std::vector<std::wstring>& custom_exes) override {
        return vegaflow::ProcessDetector::DetectActiveGame(custom_exes);
    }

    bool IsWindowValid(HWND hwnd) override {
        return IsWindow(hwnd) != FALSE;
    }

    bool IsProcessAlive(uint32_t pid) override {
        return vegaflow::ProcessDetector::IsProcessAlive(pid);
    }

    std::vector<uint32_t> FindBackgroundNoisyProcesses(uint32_t game_pid) override {
        return vegaflow::ProcessDetector::FindBackgroundNoisyProcesses(game_pid);
    }

    void QueryLiveMemory(uint64_t& out_local_vram_budget_bytes, uint64_t& out_local_vram_usage_bytes, bool& out_vram_valid) override {
        live_mem_ = mem_.SampleTelemetry();
        out_local_vram_budget_bytes = live_mem_.vram.local_budget_bytes;
        out_local_vram_usage_bytes = live_mem_.vram.local_usage_bytes;
        out_vram_valid = live_mem_.vram.valid;
    }

    bool EnableHighPrecisionTimer(uint32_t ns100) override {
        if (vegaflow::GraphicsOptimizer::EnableHighPrecisionTimer(ns100)) {
            std::cout << "[OPT] High-precision timer enabled (" << (ns100 / 10000.0) << " ms)\n";
            return true;
        }
        return false;
    }

    bool RestoreDefaultTimer() override {
        bool ok = vegaflow::GraphicsOptimizer::RestoreDefaultTimer();
        if (ok) {
            std::cout << "[OPT] Default timer resolution restored.\n";
        }
        return ok;
    }

    void SetDynamicPrioritySeparation(uint32_t val) override {
        vegaflow::GraphicsOptimizer::SetDynamicPrioritySeparation(val, false);
    }

    void SetGameDvrAndFseOptimized(bool optimize) override {
        vegaflow::GraphicsOptimizer::SetGameDvrAndFseOptimized(optimize, false);
    }

    void SetApuLaptopPowerProfile(bool optimize, const vegaflow::ApuOptimizationPolicy& static_policy) override {
        vegaflow::GraphicsOptimizer::SetApuLaptopPowerProfile(optimize, static_policy, false);
    }

    void ApplyDxgiFlipModelUpgrade(const std::wstring& exe_path) override {
        vegaflow::GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(exe_path, false);
    }

    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring& dir, uint32_t app_id, const std::string& name) override {
        return vegaflow::SteamEngineOptimizer::InspectGameDirectory(dir, app_id, name);
    }

    void ApplyEngineUmaPreset(
        const vegaflow::SteamGameEntry& game,
        const vegaflow::ApuOptimizationPolicy& static_policy,
        uint32_t pool_mb) override {
        vegaflow::SteamEngineOptimizer(fs_).ApplyEngineUmaPreset(game, static_policy, pool_mb, false);
    }

    void ApplyCpuOptimizations(
        uint32_t game_pid,
        uint32_t presenter_thread_id,
        bool is_dx12,
        const std::vector<ULONG>& game_sets,
        const std::vector<ULONG>& bg_sets,
        const std::vector<uint32_t>& bg_pids,
        uint32_t& out_throttled) override {
        auto cpu_res = cpu_.ApplyOptimizations(
            game_pid, presenter_thread_id, is_dx12, game_sets, bg_sets, bg_pids, false);
        out_throttled = cpu_res.background_processes_throttled;
    }

    void RestoreCpuAll() override {
        std::cout << "[INFO] Game closed. Restoring system state...\n";
        cpu_.RestoreAll(false);
    }

    void EvaluateMemory(uint32_t game_pid, const std::vector<uint32_t>& bg_pids, bool allow_ram_purge) override {
        if (!allow_ram_purge) return;
        auto mem_report = mem_.EvaluateAndOptimize(game_pid, bg_pids, false);
        if (mem_report.standby_purged || mem_report.background_processes_trimmed > 0) {
            std::cout << "[OPT] Memory optimized: " << mem_report.reason << "\n";
        }
    }

    void EvaluateUmaGuard(const vegaflow::ApuOptimizationPolicy& live_policy, const std::vector<uint32_t>& bg_pids) override {
        auto uma_guard = mem_.EvaluateUmaPressureGuard(live_mem_, live_policy, bg_pids, false);
        if (uma_guard.guard_triggered) {
            std::string guard_msg = "[UMA GUARD] " + uma_guard.active_reason +
                " (Intervenci\u00f3n #" + std::to_string(uma_guard.total_interventions) + ")";
            std::cout << guard_msg << "\n";
            if (audit_cb_ && *audit_cb_) {
                (*audit_cb_)(guard_msg);
            }
        }
    }

    void StartTelemetry(uint32_t pid) override {
        telem_.StartMonitoring(pid);
    }

    void StopAndSaveTelemetry(const std::string& exe_utf8) override {
        std::cout << telem_.FormatSessionReport(exe_utf8) << "\n";
        telem_.SaveSessionReportToFile(exe_utf8, "logs/session_reports.log");
        telem_.StopMonitoring();
    }

private:
    vegaflow::FileSystem& fs_;
    vegaflow::CpuOrchestrator& cpu_;
    vegaflow::MemoryManager& mem_;
    vegaflow::FrameTelemetryMonitor& telem_;
    std::function<void(const std::string&)>* audit_cb_ = nullptr;
    vegaflow::SystemMemoryTelemetry live_mem_{};
};

} // namespace

void RunSingleIteration(
    const AppConfig& config,
    vegaflow::FileSystem& file_system,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    bool& game_active,
    uint32_t& active_game_pid,
    std::wstring& active_game_exe,
    std::string& active_game_api,
    HWND& active_game_hwnd,
    bool& timer_boosted,
    std::wstring* active_game_path,
    uint32_t* out_noisy_bg_count,
    std::function<void(const std::string&)>* audit_cb) {

    vegaflow::WindowsDeviceProbe device_probe;
    WindowsGameSessionEnv env(file_system, cpu_orchestrator, mem_mgr, telemetry_monitor, audit_cb);

    vegaflow::session::GameSessionState state{};
    state.game_active = game_active;
    state.active_game_pid = active_game_pid;
    state.active_game_exe = active_game_exe;
    state.active_game_path = active_game_path ? *active_game_path : L"";
    state.active_game_api = active_game_api;
    state.active_game_hwnd = active_game_hwnd;
    state.timer_boosted = timer_boosted;

    auto emit_audit = [&](const std::string& msg) {
        std::cout << msg << "\n";
        if (audit_cb && *audit_cb) {
            (*audit_cb)(msg);
        }
    };

    vegaflow::session::RunGameSessionCycle(config, device_probe, env, hw, state, out_noisy_bg_count, emit_audit);

    game_active = state.game_active;
    active_game_pid = state.active_game_pid;
    active_game_exe = state.active_game_exe;
    active_game_api = state.active_game_api;
    active_game_hwnd = state.active_game_hwnd;
    timer_boosted = state.timer_boosted;
    if (active_game_path) {
        *active_game_path = state.active_game_path;
    }
}

} // namespace vegaflow::app
