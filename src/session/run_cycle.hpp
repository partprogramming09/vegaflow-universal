#pragma once
// One evaluation cycle of the daemon: detect the game, apply or release the optimizations.

#include <cstdint>
#include <functional>
#include <string>

#include "ports/file_system.hpp"
#include "session/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/memory_manager.hpp"
#include "safety/frame_telemetry.hpp"

namespace vegaflow::app {

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
    std::wstring* active_game_path = nullptr,
    uint32_t* out_noisy_bg_count = nullptr,
    std::function<void(const std::string&)>* audit_cb = nullptr);

} // namespace vegaflow::app
