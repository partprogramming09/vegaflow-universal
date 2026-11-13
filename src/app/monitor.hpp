#pragma once
// --monitor: real-time console dashboard.

#include "session/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/memory_manager.hpp"

namespace vegaflow::app {

void RunMonitorDashboard(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    bool once_mode);

} // namespace vegaflow::app
