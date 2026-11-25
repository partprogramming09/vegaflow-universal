#pragma once
// Console commands that look at the whole machine: --status, --purge-now and --dry-run. They build the concrete detectors
// and optimizers, so they live in app/ (the session log and the report lines they print are in session/reports.hpp).

#include "session/config.hpp"

namespace vegaflow::app {

void PrintStatusReport();
void ExecutePurgeNow(bool dry_run);
void ExecuteDryRun(const AppConfig& config);

} // namespace vegaflow::app
