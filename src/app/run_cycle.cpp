#include "app/run_cycle.hpp"

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
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "session/game_session.hpp"
#include "session/reports.hpp"
#include "ports/text_convert.hpp"
#include "windows/device_probe.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "windows/system_counters.hpp"

namespace vegaflow::app {

namespace {

class WindowsGameSessionEnv final : public vegaflow::session::GameSessionEnvironment {
public:
    WindowsGameSessionEnv(
        vegaflow::FileSystem& fs,
        vegaflow::CpuOrchestrator& cpu,
        vegaflow::MemoryManager& mem,
        vegaflow::RegistryPort& reg,
        vegaflow::PowerPort& power,
        std::function<void(const std::string&)>* audit_cb)
        : fs_(fs), cpu_(cpu), mem_(mem), reg_(reg), power_(power), audit_cb_(audit_cb) {}

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const vegaflow::ProcessPolicyConfig& policy) override {
        return vegaflow::ProcessDetector::DetectActiveGame(policy);
    }

    bool IsWindowValid(HWND hwnd) override {
        return vegaflow::IsWindowHandleValid(hwnd);
    }

    uint32_t WindowThreadId(HWND hwnd) override {
        return vegaflow::WindowThreadId(hwnd);
    }

    bool IsProcessAlive(uint32_t pid) override {
        return vegaflow::ProcessDetector::IsProcessAlive(pid);
    }

    std::vector<vegaflow::BackgroundProcessTarget> ClassifyBackgroundProcesses(
        uint32_t game_pid,
        const vegaflow::ProcessPolicyConfig& policy) override {
        return vegaflow::ProcessDetector::ClassifyBackgroundProcesses(game_pid, policy);
    }

    void QueryLiveMemory(uint64_t& out_local_vram_budget_bytes, uint64_t& out_local_vram_usage_bytes, bool& out_vram_valid) override {
        live_mem_ = mem_.SampleTelemetry();
        out_local_vram_budget_bytes = live_mem_.vram.local_budget_bytes;
        out_local_vram_usage_bytes = vegaflow::GpuUsageBytes(live_mem_.vram);
        out_vram_valid = live_mem_.vram.valid;
    }

    void SetDynamicPrioritySeparation(uint32_t val) override {
        vegaflow::KernelSchedulerTweaks(reg_).SetDynamicPrioritySeparation(val, false);
    }

    void SetGameDvrCaptureDisabled(bool disable) override {
        vegaflow::DisplayTweaks(reg_).SetGameDvrCaptureDisabled(disable, false);
    }

    void SetApuLaptopPowerProfile(bool optimize, const vegaflow::ApuOptimizationPolicy& static_policy) override {
        vegaflow::PowerModeTweaks(power_).SetApuLaptopPowerProfile(optimize, static_policy, false);
    }

    void ApplyDxgiFlipModelUpgrade(const std::wstring& exe_path) override {
        vegaflow::DxgiFlipTweaks(reg_).ApplyFlipModelUpgrade(exe_path, false);
    }

    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring& dir, uint32_t app_id, const std::string& name) override {
        return vegaflow::SteamEngineOptimizer::InspectGameDirectory(dir, app_id, name, &reg_);
    }

    void ApplyEngineUmaPreset(
        const vegaflow::SteamGameEntry& game,
        const vegaflow::ApuOptimizationPolicy& static_policy,
        uint32_t pool_mb) override {
        vegaflow::SteamEngineOptimizer(fs_, &reg_).ApplyEngineUmaPreset(game, static_policy, pool_mb, false);
    }

    void ApplyCpuOptimizations(
        uint32_t game_pid,
        uint64_t game_creation_time,
        uint32_t presenter_thread_id,
        bool is_dx12,
        const std::vector<ULONG>& game_sets,
        const std::vector<ULONG>& bg_sets,
        const std::vector<vegaflow::BackgroundProcessTarget>& bg_targets,
        uint32_t& out_throttled) override {
        auto cpu_res = cpu_.ApplyOptimizations(
            game_pid, game_creation_time, presenter_thread_id, is_dx12, game_sets, bg_sets, bg_targets, false);
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

    void SaveSessionReport(const std::string& exe_utf8, const std::string& api_name, uint32_t pid) override {
        SaveSessionReportToFile(exe_utf8, api_name, pid, "logs/session_reports.log");
    }

private:
    vegaflow::FileSystem& fs_;
    vegaflow::CpuOrchestrator& cpu_;
    vegaflow::MemoryManager& mem_;
    vegaflow::RegistryPort& reg_;
    vegaflow::PowerPort& power_;
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
    bool& game_active,
    uint32_t& active_game_pid,
    std::wstring& active_game_exe,
    std::string& active_game_api,
    HWND& active_game_hwnd,
    std::wstring* active_game_path,
    uint32_t* out_noisy_bg_count,
    std::function<void(const std::string&)>* audit_cb,
    bool check_adapters) {

    mem_mgr.SetTargetLuid(hw.primary_gpu.luid);
    vegaflow::WindowsDeviceProbe device_probe;
    vegaflow::WindowsRegistryPort reg;
    vegaflow::WindowsPowerPort power;
    WindowsGameSessionEnv env(file_system, cpu_orchestrator, mem_mgr, reg, power, audit_cb);

    vegaflow::session::GameSessionState state{};
    state.game_active = game_active;
    state.active_game_pid = active_game_pid;
    state.active_game_exe = active_game_exe;
    state.active_game_path = active_game_path ? *active_game_path : L"";
    state.active_game_api = active_game_api;
    state.active_game_hwnd = active_game_hwnd;

    auto emit_audit = [&](const std::string& msg) {
        std::cout << msg << "\n";
        if (audit_cb && *audit_cb) {
            (*audit_cb)(msg);
        }
    };

    vegaflow::session::RunGameSessionCycle(config, device_probe, env, hw, state, out_noisy_bg_count, emit_audit, check_adapters);
    // The cycle may have refreshed the hardware profile (eGPU plugged in or out): follow its GPU from the next sample on.
    mem_mgr.SetTargetLuid(hw.primary_gpu.luid);

    game_active = state.game_active;
    active_game_pid = state.active_game_pid;
    active_game_exe = state.active_game_exe;
    active_game_api = state.active_game_api;
    active_game_hwnd = state.active_game_hwnd;
    if (active_game_path) {
        *active_game_path = state.active_game_path;
    }
}

} // namespace vegaflow::app
