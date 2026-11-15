#pragma once
// Default mode: the background daemon loop (or a single cycle with --once). Returns the process exit code.

#include "windows/privileges.hpp"
#include "session/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/memory_manager.hpp"

namespace vegaflow::app {

int RunDaemon(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    const vegaflow::PrivilegeStatus& priv,
    bool once_mode,
    bool safe_mode = false);

} // namespace vegaflow::app
