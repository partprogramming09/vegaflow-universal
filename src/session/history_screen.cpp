#include "session/history_screen.hpp"

#include "session/restore.hpp"

namespace vegaflow::session {

namespace {

const char* StateWord(JournalEntryStatus st) {
    switch (st) {
    case JournalEntryStatus::Pending:
    case JournalEntryStatus::Committed: return "Activo";
    case JournalEntryStatus::Reverted: return "Deshecho";
    case JournalEntryStatus::Failed: return "Falló";
    }
    return "Activo";
}

const JournalEntry* FindById(const std::vector<JournalEntry>& entries, uint64_t id) {
    for (const auto& e : entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::string ManualHint(const JournalEntry& e) {
    return e.manual_fix_hint.empty() ? "Devuelve " + e.target + " a su valor original (" + e.before_value + ") a mano." : e.manual_fix_hint;
}

// Why this entry cannot be undone on its own, or "" when it can. `entries` is the whole journal, oldest first.
std::string UndoBlockReason(const std::vector<JournalEntry>& entries, const JournalEntry& e) {
    switch (e.kind) {
    case JournalTargetKind::RegistryValue:
    case JournalTargetKind::GameConfigFile:
    case JournalTargetKind::DxgiGamePreference: break;
    default: return "Se devuelve con «Devolver todo a como estaba».";
    }
    for (const auto& n : entries) {
        const bool active = n.status == JournalEntryStatus::Pending || n.status == JournalEntryStatus::Committed;
        if (n.id > e.id && active && n.kind == e.kind && n.target == e.target) {
            return "Antes hay que deshacer el cambio más reciente de lo mismo.";
        }
    }
    return "";
}

} // namespace

HistoryScreenModel BuildHistoryScreen(const std::vector<JournalEntry>& entries, size_t max_rows) {
    HistoryScreenModel model;
    model.empty_text = "Todavía no hay cambios. Cuando VegaFlow cambie algo en tu equipo, aparecerá aquí con el valor de antes.";
    for (auto it = entries.rbegin(); it != entries.rend() && model.rows.size() < max_rows; ++it) {
        if (!IsRevertibleKind(it->kind)) continue; // audit events and restore points are not changes
        HistoryRow row;
        row.id = it->id;
        row.when = it->timestamp;
        row.what = it->reason.empty() ? it->target : it->reason;
        if (row.what.empty()) row.what = "Cambio sin descripción";
        row.before = it->before_value;
        row.after = it->after_value;
        row.state = StateWord(it->status);
        row.can_undo = it->status == JournalEntryStatus::Pending || it->status == JournalEntryStatus::Committed;
        if (it->status == JournalEntryStatus::Failed) {
            row.manual_hint = ManualHint(*it);
        } else if (row.can_undo) {
            row.manual_hint = UndoBlockReason(entries, *it);
            row.can_undo = row.manual_hint.empty();
        }
        model.rows.push_back(std::move(row));
    }
    return model;
}

UndoOneResult UndoOneChange(ChangeJournal& journal, SystemUndoPort& undo, uint64_t id, bool dry_run) {
    UndoOneResult res;
    const JournalEntry* found = FindById(journal.Entries(), id);
    if (!found || !IsRevertibleKind(found->kind)) {
        res.message = "No se encontró ese cambio en el historial.";
        return res;
    }
    if (found->status == JournalEntryStatus::Reverted) {
        res.message = "Ese cambio ya estaba deshecho.";
        return res;
    }
    if (found->status == JournalEntryStatus::Failed) {
        res.message = "Ese cambio no se aplicó del todo y no hay nada que deshacer. " + ManualHint(*found);
        return res;
    }
    const std::string blocked = UndoBlockReason(journal.Entries(), *found);
    if (!blocked.empty()) {
        res.message = blocked;
        return res;
    }
    const JournalEntry entry = *found; // the journal may reallocate its list while marking
    if (dry_run) {
        res.ok = true;
        res.message = "[DRY-RUN] Se desharía: " + (entry.reason.empty() ? entry.target : entry.reason);
        return res;
    }
    std::string err;
    if (!undo.RevertEntry(entry, &err)) {
        res.message = (err.empty() ? std::string("No se pudo deshacer") : err) + ". " + ManualHint(entry);
        return res;
    }
    const bool saved = journal.MarkReverted(entry.id);
    res.ok = true;
    res.message = "Deshecho: " + (entry.reason.empty() ? entry.target : entry.reason);
    if (!saved) res.message += ". El historial no se pudo guardar: al reiniciar este cambio puede volver a aparecer como activo.";
    return res;
}

} // namespace vegaflow::session
