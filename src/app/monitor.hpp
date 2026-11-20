#pragma once
// --monitor: real-time console dashboard.

#include <string>

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
    bool once_mode,
    bool safe_mode = false,
    const std::string& safe_reason = {});

} // namespace vegaflow::app
