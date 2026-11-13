#pragma once
// Console reports: --status, --purge-now and --dry-run.

#include <cstdint>
#include <string>

#include "session/config.hpp"

namespace vegaflow::app {

uint32_t CountEcoQoSProcesses();
uint32_t CountDirectFlipConfiguredGames();
bool SaveSessionReportToFile(
    const std::string& game_name,
    const std::string& api_name = "",
    uint32_t pid = 0,
    const std::string& file_path = "logs/session_reports.log");

void PrintStatusReport();
void ExecutePurgeNow(bool dry_run);
void ExecuteDryRun(const AppConfig& config);

} // namespace vegaflow::app
