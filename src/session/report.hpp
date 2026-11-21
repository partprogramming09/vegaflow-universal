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

// For text pasted in public (`--doctor`): hides the Windows user name in paths (Users\<usuario>) and replaces the computer
// and domain names, in any letter case, with <equipo> and <dominio>. Unlike BuildReport it keeps line breaks and does not
// cut anything. An empty name is ignored.
std::string RedactReport(const std::string& text, const std::string& computer_name, const std::string& domain_name);

} // namespace vegaflow::session
