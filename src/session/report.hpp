#pragma once
// The text the "Copiar informe" button puts on the clipboard: what VegaFlow knows about the machine, the memory and the
// latest changes, so a person can paste it where they ask for help. Pure text; the view does the clipboard.

#include <string>

#include "session/history_screen.hpp"
#include "session/home_screen.hpp"

namespace vegaflow::session {

struct ReportInputs {
    std::string version;
    HomeScreenModel home;
    HistoryScreenModel history;
    std::string status; // last notice shown in the status bar
};

// CRLF lines. Each field is cut at 200 bytes (on a character boundary, "..." added) and has its line breaks turned into
// spaces; at most the 30 newest changes are listed, with the total.
std::string BuildReport(const ReportInputs& in);

} // namespace vegaflow::session
