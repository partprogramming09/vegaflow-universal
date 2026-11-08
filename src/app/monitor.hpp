#pragma once
// --monitor: real-time console dashboard.

#include "app/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/memory_manager.hpp"
#include "safety/frame_telemetry.hpp"

namespace vegaflow::app {

void RunMonitorDashboard(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    bool once_mode);

} // namespace vegaflow::app
