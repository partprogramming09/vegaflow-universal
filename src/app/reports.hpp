#pragma once
// Console reports: --status, --purge-now and --dry-run.

#include <cstdint>

#include "app/config.hpp"

namespace vegaflow::app {

uint32_t CountEcoQoSProcesses();
uint32_t CountDirectFlipConfiguredGames();

void PrintStatusReport();
void ExecutePurgeNow(bool dry_run);
void ExecuteDryRun(const AppConfig& config);

} // namespace vegaflow::app
