#pragma once
// --gui: native Win32/GDI dashboard with system tray. Returns the process exit code.

#include "app/config.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "optimize/fsr/fsr_scaler.hpp"
#include "optimize/memory_manager.hpp"
#include "safety/frame_telemetry.hpp"

namespace vegaflow::app {

int RunGuiMode(
    AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    vegaflow::FsrSpatialScaler& fsr_scaler);

} // namespace vegaflow::app
