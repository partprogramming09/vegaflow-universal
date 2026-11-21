#pragma once
// Console reports: --status, --purge-now and --dry-run.

#include <cstdint>
#include <string>

#include "ports/memory_probe.hpp"
#include "session/config.hpp"

namespace vegaflow::app {

bool SaveSessionReportToFile(
    const std::string& game_name,
    const std::string& api_name = "",
    uint32_t pid = 0,
    const std::string& file_path = "logs/session_reports.log");

void PrintStatusReport();
void ExecutePurgeNow(bool dry_run);
void ExecuteDryRun(const AppConfig& config);

// The [KPI] line: the daemon's private working set against the 15 MB budget, "passed" only when it is under it.
std::string MemoryKpiLine(uint64_t private_ws_bytes);

// The [GPU] line of --status and --monitor: global shared/dedicated GPU usage (PDH), DXGI LOCAL and NON_LOCAL budgets,
// and whether PDH answered (vram.valid; false means the UMA guard decides on RAM alone).
std::string GpuMemoryLine(const VideoMemoryTelemetry& vram);

} // namespace vegaflow::app
