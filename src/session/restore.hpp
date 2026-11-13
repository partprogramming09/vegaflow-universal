#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ports/change_journal.hpp"

namespace vegaflow::session {

bool IsRevertibleKind(JournalTargetKind kind);

StartupJournalEvaluation EvaluateStartupJournal(const ChangeJournal& journal);

RestoreReport RestoreAllRecordedChanges(
    ChangeJournal& journal,
    SystemUndoPort& undo,
    bool dry_run = false);

std::vector<std::string> FormatJournalHistory(const ChangeJournal& journal, size_t max_items = 200);

bool ShouldOfferRestorePoint(const ChangeJournal& journal);

bool ExecuteRestorePointOffer(
    ChangeJournal& journal,
    SystemUndoPort& undo,
    bool user_accepted,
    const std::string& description,
    std::string* out_message = nullptr);

} // namespace vegaflow::session
