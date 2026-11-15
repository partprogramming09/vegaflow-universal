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

// The sentence after "Volver todo al valor de Windows": the journal's own summary when it had entries, else how many saved
// registry originals were put back, else that nothing was changed.
std::string RestoreAllMessage(const RestoreReport& journal_report, size_t registry_reverted, size_t registry_total);

std::vector<std::string> FormatJournalHistory(const ChangeJournal& journal, size_t max_items = 200);

bool ShouldOfferRestorePoint(const ChangeJournal& journal);

bool ExecuteRestorePointOffer(
    ChangeJournal& journal,
    SystemUndoPort& undo,
    bool user_accepted,
    const std::string& description,
    std::string* out_message = nullptr);

} // namespace vegaflow::session
