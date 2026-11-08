#pragma once
// Default mode: the background daemon loop (or a single cycle with --once). Returns the process exit code.

#include "windows/privileges.hpp"
#include "app/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/fsr/fsr_scaler.hpp"
#include "optimize/memory_manager.hpp"
#include "safety/frame_telemetry.hpp"

namespace vegaflow::app {

int RunDaemon(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    vegaflow::FsrSpatialScaler& fsr_scaler,
    const vegaflow::PrivilegeStatus& priv,
    bool once_mode);

} // namespace vegaflow::app
