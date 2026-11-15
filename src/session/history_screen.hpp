#pragma once
// Model of the "Historial" screen: what VegaFlow changed, with the value before and after, and a per-row undo.
// Built from the change journal; no <windows.h>, so it is tested with the fake journal and fake undo port.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "ports/change_journal.hpp"

namespace vegaflow::session {

struct HistoryRow {
    uint64_t id = 0;
    std::string when;
    std::string what;
    std::string before;
    std::string after;
    std::string state;       // "Activo", "Deshecho" or "Falló"
    bool can_undo = false;   // only active changes
    std::string manual_hint; // why a row has no Deshacer button, or how to fix it by hand (failed rows)
};

struct HistoryScreenModel {
    std::vector<HistoryRow> rows; // newest first
    std::string empty_text;       // shown when there are no rows
};

HistoryScreenModel BuildHistoryScreen(const std::vector<JournalEntry>& entries, size_t max_rows = 100);

struct UndoOneResult {
    bool ok = false;
    std::string message;
};

// Reverts one active change by journal id and marks it reverted. Does nothing for an unknown, undone or failed id, for
// kinds that keep state in memory (those go back with the restore-all button), or while a newer active change touches the
// same target (undoing the old one first would leave the newer value in place).
UndoOneResult UndoOneChange(ChangeJournal& journal, SystemUndoPort& undo, uint64_t id, bool dry_run = false);

} // namespace vegaflow::session
