#pragma once
// What to tell the person when VegaFlow is about to close while it still has changes active on their PC (no <windows.h>).

#include <string>

#include "session/history_screen.hpp"

namespace vegaflow::session {

struct ExitNotice {
    bool ask = false;   // false: nothing active, close without a question
    size_t active = 0;  // changes still in force
    std::string text;   // the question, when `ask`
};

// Closing VegaFlow does not undo anything; the question offers to do it first. Only rows in force count.
ExitNotice BuildExitNotice(const HistoryScreenModel& history);

} // namespace vegaflow::session
