#include "session/restore.hpp"

#include <sstream>

namespace vegaflow::session {

namespace {

const char* StatusLabel(JournalEntryStatus st) {
    switch (st) {
        case JournalEntryStatus::Pending: return "PENDIENTE";
        case JournalEntryStatus::Committed: return "APLICADO";
        case JournalEntryStatus::Reverted: return "DEVUELTO";
        case JournalEntryStatus::Failed: return "FALLO";
    }
    return "PENDIENTE";
}

} // namespace

bool IsRevertibleKind(JournalTargetKind kind) {
    switch (kind) {
        case JournalTargetKind::RegistryValue:
        case JournalTargetKind::PowerPlan:
        case JournalTargetKind::MemoryCompression:
        case JournalTargetKind::ProcessState:
        case JournalTargetKind::DxgiGamePreference:
        case JournalTargetKind::GameConfigFile:
            return true;
        case JournalTargetKind::SystemRestorePoint:
        case JournalTargetKind::AuditEvent:
            return false;
    }
    return false;
}

StartupJournalEvaluation EvaluateStartupJournal(const ChangeJournal& journal) {
    StartupJournalEvaluation eval{};
    const JournalHeader& hdr = journal.Header();
    if (hdr.corrupt) {
        eval.corrupt = true;
        eval.corrupt_reason = hdr.corrupt_reason.empty()
            ? "El archivo del diario de cambios tiene un formato inválido."
            : hdr.corrupt_reason;
        return eval;
    }

    eval.previous_unclean_exit = journal.DetectedPreviousUncleanExit();
    eval.effective_unclean_streak = journal.EffectiveUncleanStreak();
    eval.safe_mode = journal.IsSafeModeTriggered();

    for (const auto& entry : journal.Entries()) {
        if (!IsRevertibleKind(entry.kind)) continue;
        if (entry.status == JournalEntryStatus::Pending || entry.status == JournalEntryStatus::Committed) {
            eval.pending_recovery_entries.push_back(entry);
        }
    }
    eval.unclosed_or_unreverted_count = eval.pending_recovery_entries.size();
    return eval;
}

RestoreReport RestoreAllRecordedChanges(
    ChangeJournal& journal,
    SystemUndoPort& undo,
    bool dry_run) {
    RestoreReport report{};
    const auto entries_snapshot = journal.Entries();

    std::vector<JournalEntry> to_revert;
    for (auto it = entries_snapshot.rbegin(); it != entries_snapshot.rend(); ++it) {
        if (!IsRevertibleKind(it->kind)) continue;
        if (it->status == JournalEntryStatus::Pending || it->status == JournalEntryStatus::Committed) {
            to_revert.push_back(*it);
        }
    }

    report.total_count = to_revert.size();
    if (report.total_count == 0) {
        report.all_succeeded = true;
        report.summary_message = "Se devolvieron 0 de 0 (no había cambios pendientes en el diario).";
        if (!dry_run && !journal.Header().corrupt &&
            (journal.DetectedPreviousUncleanExit() || journal.Header().active_session || journal.IsSafeModeTriggered())) {
            journal.ResetRecoveryState();
        }
        return report;
    }

    if (dry_run) {
        report.reverted_count = report.total_count;
        report.failed_count = 0;
        report.all_succeeded = true;
        report.summary_message = "[DRY-RUN] Se devolverían " + std::to_string(report.total_count) +
                                 " de " + std::to_string(report.total_count) + ".";
        return report;
    }

    for (const auto& e : to_revert) {
        std::string err;
        if (undo.RevertEntry(e, &err)) {
            journal.MarkReverted(e.id);
            report.reverted_count++;
        } else {
            report.failed_count++;
            RestoreItemFailure fail;
            fail.entry_id = e.id;
            fail.target = e.target;
            fail.reason = err.empty() ? "No se pudo restaurar automáticamente" : err;
            fail.manual_fix_hint = e.manual_fix_hint.empty()
                ? ("Restaura manualmente " + e.target + " a su valor original (" + e.before_value + ").")
                : e.manual_fix_hint;
            report.failures.push_back(std::move(fail));
        }
    }

    report.all_succeeded = (report.failed_count == 0);
    if (report.all_succeeded) {
        journal.ResetRecoveryState();
        report.summary_message = "Se devolvieron " + std::to_string(report.reverted_count) +
                                 " de " + std::to_string(report.total_count) + ".";
    } else {
        std::ostringstream os;
        os << report.failed_count << " no se pudieron devolver (se devolvieron "
           << report.reverted_count << " de " << report.total_count
           << "), y así se arreglan a mano:";
        for (const auto& f : report.failures) {
            os << "\n  - " << f.target << " (" << f.reason << "): " << f.manual_fix_hint;
        }
        report.summary_message = os.str();
    }
    return report;
}

std::vector<std::string> FormatJournalHistory(const ChangeJournal& journal, size_t max_items) {
    const auto& all = journal.Entries();
    std::vector<std::string> out;
    if (all.empty() || max_items == 0) return out;
    const size_t start = (all.size() > max_items) ? (all.size() - max_items) : 0;
    out.reserve(all.size() - start);
    for (size_t i = start; i < all.size(); ++i) {
        const auto& e = all[i];
        std::string line = "[" + (e.timestamp.empty() ? std::string("00:00:00") : e.timestamp) + "] [" +
                           StatusLabel(e.status) + "] " + e.target;
        if (!e.reason.empty()) {
            line += " — " + e.reason;
        }
        if (!e.before_value.empty() || !e.after_value.empty()) {
            line += " (" + (e.before_value.empty() ? "-" : e.before_value) + " -> " +
                    (e.after_value.empty() ? "-" : e.after_value) + ")";
        }
        out.push_back(std::move(line));
    }
    return out;
}

bool ShouldOfferRestorePoint(const ChangeJournal& journal) {
    return !journal.Header().corrupt && !journal.Header().restore_point_prompted;
}

bool ExecuteRestorePointOffer(
    ChangeJournal& journal,
    SystemUndoPort& undo,
    bool user_accepted,
    const std::string& description,
    std::string* out_message) {
    journal.SetRestorePointPrompted(true);
    if (!user_accepted) {
        if (out_message) *out_message = "Creación de punto de restauración omitida por el usuario.";
        journal.RecordAuditEvent("Windows Restore Point", "Omitido por el usuario", "El diario sigue protegiendo cada cambio.");
        return true;
    }
    std::string msg;
    const bool ok = undo.CreateRestorePoint(description, &msg);
    if (out_message) *out_message = msg;
    journal.RecordAuditEvent(
        "Windows Restore Point",
        ok ? "Punto de restauración creado antes de cambios del registro" : "No se pudo crear punto de restauración",
        msg);
    return ok;
}

std::string RestoreAllMessage(const RestoreReport& journal_report, size_t registry_reverted, size_t registry_total) {
    if (journal_report.total_count > 0) {
        if (registry_reverted >= registry_total) return journal_report.summary_message;
        return journal_report.summary_message + " Del registro se devolvieron " + std::to_string(registry_reverted) + " de " +
               std::to_string(registry_total) + ".";
    }
    if (registry_total > 0) return "Se devolvieron " + std::to_string(registry_reverted) + " de " + std::to_string(registry_total) + ".";
    return "Se devolvieron 0 de 0 (ajustes en valores originales de Windows).";
}

bool RestoreAllSucceeded(const RestoreReport& journal_report, size_t registry_reverted, size_t registry_total) {
    return journal_report.all_succeeded && journal_report.failed_count == 0 && registry_reverted >= registry_total;
}

} // namespace vegaflow::session
