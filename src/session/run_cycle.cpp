#include "session/run_cycle.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
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
#include <filesystem>
#include "session/text_convert.hpp"

namespace vegaflow::app {

void RunSingleIteration(
    const AppConfig& config,
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

    using namespace vegaflow;

    std::optional<DetectedGameProcess> game_opt;
    if (game_active && active_game_pid != 0 &&
        active_game_hwnd != nullptr && IsWindow(active_game_hwnd) &&
        ProcessDetector::IsProcessAlive(active_game_pid)) {
        DetectedGameProcess cached_game{};
        cached_game.pid = active_game_pid;
        cached_game.exe_name = active_game_exe;
        cached_game.full_path = active_game_path ? *active_game_path : L"";
        if (active_game_api == "DirectX 12") cached_game.api = RenderApi::DirectX12;
        else if (active_game_api == "Vulkan") cached_game.api = RenderApi::Vulkan;
        else if (active_game_api == "DirectX 9/10") cached_game.api = RenderApi::DirectX9_10;
        else cached_game.api = RenderApi::DirectX11;
        cached_game.main_window = active_game_hwnd;
        cached_game.presenter_thread_id = static_cast<uint32_t>(GetWindowThreadProcessId(active_game_hwnd, nullptr));
        game_opt = cached_game;
    } else {
        game_opt = ProcessDetector::DetectActiveGame(config.custom_game_exes);
    }

    if (game_opt.has_value()) {
        const auto& game = *game_opt;
        active_game_hwnd = game.main_window;
        active_game_exe = game.exe_name;
        active_game_api = ProcessDetector::RenderApiToString(game.api);
        if (active_game_path) {
            *active_game_path = game.full_path;
        }

        bool is_new_game = (!game_active || active_game_pid != game.pid);

        auto live_mem = mem_mgr.SampleTelemetry();
        ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw, live_mem.vram.local_budget_bytes);

        if (is_new_game) {
            if (game_active && active_game_pid != 0) {
                std::cout << telemetry_monitor.FormatSessionReport(WideToUtf8(active_game_exe)) << "\n";
                telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
                telemetry_monitor.StopMonitoring();
            }

            std::cout << "[INFO] Game detected: " << WideToUtf8(game.exe_name)
                      << " (PID: " << game.pid << ") using " << active_game_api << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)("Juego detectado: " + WideToUtf8(game.exe_name) + " (PID: " + std::to_string(game.pid) + ") [" + active_game_api + "]");
            }

            if (!timer_boosted) {
                if (GraphicsOptimizer::EnableHighPrecisionTimer(config.timer_resolution_ns100)) {
                    timer_boosted = true;
                    std::cout << "[OPT] High-precision timer enabled (" << (config.timer_resolution_ns100 / 10000.0) << " ms)\n";
                }
            }

            bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
            GraphicsOptimizer::SetDynamicPrioritySeparation(is_dx12 ? 0x26 : 0x28, false);

            GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
            GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);

            // =================================================================
            // SMART PER-GAME AUTO-PROFILING (R&D)
            // =================================================================
            std::filesystem::path pExe(game.full_path);
            auto inspected = SteamEngineOptimizer::InspectGameDirectory(
                pExe.parent_path().wstring(),
                0,
                WideToUtf8(game.exe_name));
            if (inspected.primary_exe_path.empty()) {
                inspected.primary_exe_path = game.full_path;
            }

            auto emit_profile_msg = [&](const std::string& msg) {
                std::cout << msg << "\n";
                if (audit_cb && *audit_cb) {
                    (*audit_cb)(msg);
                }
            };

            if (inspected.has_anticheat) {
                // Profile 1: Anti-Cheat Protected
                emit_profile_msg("[SECURITY] Anti-Cheat detectado (HoYoKProtect/EAC/BE). Perfil Pasivo Seguro activado: DXGI Flip Model + Aislamiento CPU (0 archivos modificados).");
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
            } else if (inspected.engine == GameEngineType::UnrealEngine4_5) {
                // Profile 2: Unreal Engine without Anti-Cheat
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
                uint32_t pool_mb = policy.safe_vram_pool_mb;
                std::string async_comp_str = (!policy.disable_dx12_async_compute) ? "ON" : "OFF";
                std::string preset_msg = "[ENGINE] Unreal Engine detectado. Preset UMA (" + policy.guard_mode_name +
                    ") aplicado (Pool " + std::to_string(pool_mb) + " MB, LimitVRAM=0, AsyncCompute=" + async_comp_str + ").";
                if (!inspected.engine_preset_applied) {
                    SteamEngineOptimizer::ApplyEngineUmaPreset(inspected, pool_mb, false);
                    emit_profile_msg(preset_msg);
                } else {
                    emit_profile_msg(preset_msg);
                }
            } else {
                // Profile 3: Generic DX11/DX12 games
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
                emit_profile_msg("[OPTIMIZE] Juego nativo detectado. DXGI Flip Model y prioridad de renderizador aplicados.");
            }

            telemetry_monitor.StartMonitoring(game.pid);
            game_active = true;
            active_game_pid = game.pid;
        }

        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(game.pid);
        if (out_noisy_bg_count) {
            *out_noisy_bg_count = static_cast<uint32_t>(bg_pids.size());
        }
        bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);

        const std::vector<ULONG> no_sets;
        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            game.pid,
            game.presenter_thread_id,
            is_dx12,
            config.restrict_game_cpu_sets ? hw.cpu_topology.game_optimal_cpu_set_ids : no_sets,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            false);

        if (is_new_game) {
            std::string eco_msg = "[CPU] EcoQoS activado en procesos secundarios (" +
                std::to_string(cpu_res.background_processes_throttled) + " procesos en modo eficiencia térmica para ahorro APU).";
            std::cout << eco_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(eco_msg);
            }

            std::string io_msg = "[I/O NT] Jerarquía I/O activada: IoPriorityHigh en juego (PID " +
                std::to_string(game.pid) + ") y IoPriorityVeryLow en procesos de fondo.";
            std::cout << io_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(io_msg);
            }
        }

        auto mem_report = mem_mgr.EvaluateAndOptimize(game.pid, bg_pids, false);
        if (mem_report.standby_purged || mem_report.background_processes_trimmed > 0) {
            std::cout << "[OPT] Memory optimized: " << mem_report.reason << "\n";
        }

        auto uma_guard = mem_mgr.EvaluateUmaPressureGuard(live_mem, policy, bg_pids, false);
        if (uma_guard.guard_triggered) {
            std::string guard_msg = "[UMA GUARD] " + uma_guard.active_reason + " (Intervención #" + std::to_string(uma_guard.total_interventions) + ")";
            std::cout << guard_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(guard_msg);
            }
        }
    } else {
        // Fallback: If previously tracked game is still alive in background/minimized, preserve session
        if (game_active) {
            if (active_game_pid != 0 && ProcessDetector::IsProcessAlive(active_game_pid)) {
                // Game process is still alive (minimized or Alt-Tabbed); do not discard session yet
            } else {
                std::cout << "[INFO] Game closed (PID: " << active_game_pid << "). Restoring system state...\n";
                std::cout << telemetry_monitor.FormatSessionReport(WideToUtf8(active_game_exe)) << "\n";
                telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
                telemetry_monitor.StopMonitoring();

                cpu_orchestrator.RestoreAll(false);
                GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
                if (timer_boosted && !config.timer_forced_manual) {
                    GraphicsOptimizer::RestoreDefaultTimer();
                    timer_boosted = false;
                    std::cout << "[OPT] Default timer resolution restored.\n";
                }
                game_active = false;
                active_game_pid = 0;
                active_game_exe.clear();
                active_game_api.clear();
                active_game_hwnd = nullptr;
                if (active_game_path) {
                    active_game_path->clear();
                }
            }
        }
    }
}

} // namespace vegaflow::app
