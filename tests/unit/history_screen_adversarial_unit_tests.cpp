// Adversarial unit tests: the "Historial" model, the per-row undo and "Volver todo" with big journals, empty fields, undo
// out of order, undo repeated, an undo that fails, a journal that cannot be saved, and the model agreeing with the action.

#include "session/history_screen.hpp"
#include "session/restore.hpp"
#include "windows/change_journal.hpp"
#include "fake_change_journal.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using vegaflow::FileChangeJournal;
using vegaflow::JournalEntry;
using vegaflow::JournalEntryStatus;
using vegaflow::JournalTargetKind;

void Attack(const std::string& name, const std::function<std::string()>& body) {
    try {
        const std::string why = body();
        RecordResult(why.empty(), name, why);
    } catch (const std::exception& e) {
        RecordResult(false, name, std::string("threw: ") + e.what());
    } catch (...) {
        RecordResult(false, name, "threw an unknown exception");
    }
}

JournalEntry Entry(JournalTargetKind kind, const std::string& target, const std::string& reason = "motivo") {
    JournalEntry e;
    e.kind = kind;
    e.target = target;
    e.reason = reason;
    e.before_value = "antes";
    e.after_value = "despues";
    return e;
}

struct Rig {
    FakeFileSystem fs;
    FakeWallClock clock;
    std::unique_ptr<FileChangeJournal> journal;
    FakeSystemUndo undo;
    Rig() {
        journal = std::make_unique<FileChangeJournal>(fs, clock, L"C:\\VegaFlow\\config\\adv.vfj");
        journal->Load();
        journal->BeginSession();
    }
    uint64_t Active(JournalTargetKind kind, const std::string& target, const std::string& reason = "motivo") {
        const uint64_t id = journal->RecordPending(Entry(kind, target, reason));
        journal->MarkCommitted(id);
        return id;
    }
    const JournalEntry* Find(uint64_t id) const {
        for (const auto& e : journal->Entries()) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }
};

const gs::HistoryRow* RowOf(const gs::HistoryScreenModel& m, uint64_t id) {
    for (const auto& r : m.rows) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

} // namespace

void Test82() {
    // Test 82: attacks on the Historial model, the undo and "Volver todo"
    Attack("Test 82.1: a row limit of 0 gives no rows and the empty sentence; a huge limit gives every change", [&]() -> std::string {
        Rig r;
        for (int i = 0; i < 5; ++i) r.Active(JournalTargetKind::RegistryValue, "HKLM\\K" + std::to_string(i));
        const auto none = gs::BuildHistoryScreen(r.journal->Entries(), 0);
        if (!none.rows.empty()) return "rows with limit 0";
        if (none.empty_text.empty()) return "empty_text empty";
        const auto all = gs::BuildHistoryScreen(r.journal->Entries(), SIZE_MAX);
        return all.rows.size() == 5 ? "" : "rows " + std::to_string(all.rows.size());
    });

    Attack("Test 82.2: 400 changes with the default limit give exactly the newest 100, newest first, with no audit event among them", [&]() -> std::string {
        Rig r;
        std::vector<uint64_t> ids;
        for (int i = 0; i < 400; ++i) {
            ids.push_back(r.Active(JournalTargetKind::RegistryValue, "HKLM\\K" + std::to_string(i % 40)));
            if (i % 50 == 0) r.journal->RecordAuditEvent("evento", "motivo", "detalle");
        }
        const auto m = gs::BuildHistoryScreen(r.journal->Entries());
        if (m.rows.size() != 100) return "rows " + std::to_string(m.rows.size());
        for (size_t i = 0; i < m.rows.size(); ++i) {
            if (m.rows[i].id != ids[ids.size() - 1 - i]) return "row " + std::to_string(i) + " is not the " + std::to_string(i + 1) + "th newest";
        }
        return "";
    });

    Attack("Test 82.3: a change with no reason and no target still shows some text for what changed", [&]() -> std::string {
        Rig r;
        const uint64_t id = r.Active(JournalTargetKind::RegistryValue, "", "");
        if (id == 0) return "blind: the journal refused the entry";
        const auto m = gs::BuildHistoryScreen(r.journal->Entries());
        const auto* row = RowOf(m, id);
        if (!row) return "row missing";
        if (row->what.empty()) return "row.what is empty (a blank line on screen)";
        if (row->when.empty()) return "row.when is empty";
        return "";
    });

    Attack("Test 82.4: undo goes newest to oldest on one target: the old one is blocked until the new one is undone, then it works", [&]() -> std::string {
        Rig r;
        const uint64_t a = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X", "uno");
        const uint64_t b = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X", "dos");
        const uint64_t c = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X", "tres");
        auto m = gs::BuildHistoryScreen(r.journal->Entries());
        if (RowOf(m, a)->can_undo || RowOf(m, b)->can_undo || !RowOf(m, c)->can_undo) return "can_undo wrong at the start";
        if (gs::UndoOneChange(*r.journal, r.undo, a).ok) return "undid the oldest first";
        if (!gs::UndoOneChange(*r.journal, r.undo, c).ok) return "could not undo the newest";
        m = gs::BuildHistoryScreen(r.journal->Entries());
        if (RowOf(m, a)->can_undo || !RowOf(m, b)->can_undo) return "can_undo wrong after undoing the newest";
        if (!gs::UndoOneChange(*r.journal, r.undo, b).ok || !gs::UndoOneChange(*r.journal, r.undo, a).ok) return "chain did not unwind";
        if (r.undo.reverted_ids != std::vector<uint64_t>{c, b, a}) return "reverted in the wrong order";
        return "";
    });

    Attack("Test 82.5: a newer entry that was undone or failed does not block an older one; another kind with the same target does not either", [&]() -> std::string {
        Rig r;
        const uint64_t old_reg = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        const uint64_t newer_reverted = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        r.journal->MarkReverted(newer_reverted);
        const uint64_t newer_failed = r.journal->RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\X"));
        r.journal->MarkFailed(newer_failed, "no se pudo");
        const uint64_t other_kind = r.Active(JournalTargetKind::GameConfigFile, "HKLM\\X");
        (void)other_kind;
        const auto m = gs::BuildHistoryScreen(r.journal->Entries());
        if (!RowOf(m, old_reg)->can_undo) return "old entry blocked by a reverted, a failed or another-kind entry: " + RowOf(m, old_reg)->manual_hint;
        if (!gs::UndoOneChange(*r.journal, r.undo, old_reg).ok) return "undo refused";
        return "";
    });

    Attack("Test 82.6: ids that are not changes (0, an audit event, a restore point, an unknown one) are not found and nothing is reverted", [&]() -> std::string {
        Rig r;
        r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        const uint64_t audit = r.journal->RecordAuditEvent("evento", "motivo", "detalle");
        const uint64_t rp = r.journal->RecordPending(Entry(JournalTargetKind::SystemRestorePoint, "punto"));
        for (const uint64_t id : {uint64_t{0}, audit, rp, uint64_t{999999}, UINT64_MAX}) {
            const auto res = gs::UndoOneChange(*r.journal, r.undo, id);
            if (res.ok || res.message.empty()) return "id " + std::to_string(id) + " ok=" + (res.ok ? "true" : "false") + " message '" + res.message + "'";
        }
        return r.undo.reverted_ids.empty() ? "" : "something was reverted";
    });

    Attack("Test 82.7: a failed entry cannot be undone and the answer carries the manual fix", [&]() -> std::string {
        Rig r;
        JournalEntry e = Entry(JournalTargetKind::RegistryValue, "HKLM\\F");
        e.manual_fix_hint = "arreglo manual F";
        const uint64_t id = r.journal->RecordPending(e);
        r.journal->MarkFailed(id, "falló");
        const auto res = gs::UndoOneChange(*r.journal, r.undo, id);
        if (res.ok || !r.undo.reverted_ids.empty()) return "undid a failed entry";
        if (res.message.find("arreglo manual F") == std::string::npos) return "no manual fix in '" + res.message + "'";
        const auto m = gs::BuildHistoryScreen(r.journal->Entries());
        if (RowOf(m, id)->can_undo || RowOf(m, id)->manual_hint.find("arreglo manual F") == std::string::npos) return "row for a failed entry";
        return "";
    });

    Attack("Test 82.8: an undo that fails keeps the row active and untouched; the retry works once; a third click reverts nothing more", [&]() -> std::string {
        Rig r;
        const uint64_t id = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        r.undo.fail_targets.insert("HKLM\\X");
        const auto bad = gs::UndoOneChange(*r.journal, r.undo, id);
        if (bad.ok || r.Find(id)->status == JournalEntryStatus::Reverted) return "failed undo marked as done";
        if (bad.message.empty()) return "empty message";
        r.undo.fail_targets.clear();
        if (!gs::UndoOneChange(*r.journal, r.undo, id).ok) return "retry refused";
        const auto again = gs::UndoOneChange(*r.journal, r.undo, id);
        if (again.ok || r.undo.reverted_ids.size() != 1) return "third click reverted again";
        return "";
    });

    Attack("Test 82.9: a dry run never reverts or marks anything, for any entry", [&]() -> std::string {
        Rig r;
        const uint64_t a = r.Active(JournalTargetKind::RegistryValue, "HKLM\\A");
        const uint64_t plan = r.Active(JournalTargetKind::PowerPlan, "plan");
        const size_t writes = r.fs.writes.size() + r.fs.replaces.size();
        for (const uint64_t id : {a, plan, uint64_t{0}, uint64_t{777}}) gs::UndoOneChange(*r.journal, r.undo, id, true);
        if (!r.undo.reverted_ids.empty()) return "dry run reverted";
        if (r.Find(a)->status == JournalEntryStatus::Reverted) return "dry run marked the entry";
        if (r.fs.writes.size() + r.fs.replaces.size() != writes) return "dry run wrote the journal";
        return "";
    });

    Attack("Test 82.10: if the journal cannot be saved after a good undo, a restart would show the change as active again, so the answer must say so", [&]() -> std::string {
        Rig r;
        const uint64_t id = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        r.fs.fail_write = true;
        r.fs.fail_replace = true;
        const auto res = gs::UndoOneChange(*r.journal, r.undo, id);
        r.fs.fail_write = false;
        r.fs.fail_replace = false;
        FileChangeJournal restarted(r.fs, r.clock, L"C:\\VegaFlow\\config\\adv.vfj");
        restarted.Load();
        bool active_after_restart = false;
        for (const auto& e : restarted.Entries()) {
            if (e.id == id) active_after_restart = e.status != JournalEntryStatus::Reverted;
        }
        if (active_after_restart && res.ok && res.message.find("historial") == std::string::npos) return "reverted, not persisted, and the message says '" + res.message + "'";
        return "";
    });

    Attack("Test 82.11: property: a row offers Deshacer exactly when UndoOneChange would do it (random journals, fixed seed)", [&]() -> std::string {
        uint64_t x = 0xD1B54A32D192ED03ULL;
        const auto next = [&]() {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        };
        const JournalTargetKind kinds[] = {JournalTargetKind::RegistryValue, JournalTargetKind::PowerPlan, JournalTargetKind::MemoryCompression, JournalTargetKind::ProcessState,
                                           JournalTargetKind::DxgiGamePreference, JournalTargetKind::GameConfigFile, JournalTargetKind::SystemRestorePoint};
        for (int round = 0; round < 60; ++round) {
            Rig r;
            const int n = 1 + static_cast<int>(next() % 25);
            for (int i = 0; i < n; ++i) {
                const uint64_t id = r.journal->RecordPending(Entry(kinds[next() % 7], "T" + std::to_string(next() % 4)));
                switch (next() % 4) {
                    case 1: r.journal->MarkCommitted(id); break;
                    case 2: r.journal->MarkCommitted(id); r.journal->MarkReverted(id); break;
                    case 3: r.journal->MarkFailed(id, "x"); break;
                    default: break;
                }
            }
            const auto m = gs::BuildHistoryScreen(r.journal->Entries(), SIZE_MAX);
            for (const auto& row : m.rows) {
                FakeSystemUndo probe;
                const auto dry = gs::UndoOneChange(*r.journal, probe, row.id, true);
                if (row.can_undo != dry.ok) return "round " + std::to_string(round) + ", id " + std::to_string(row.id) + ": can_undo=" + (row.can_undo ? "true" : "false") + " but dry-run ok=" + (dry.ok ? "true" : "false") + " ('" + dry.message + "')";
                if (!row.can_undo && row.state == "Activo" && row.manual_hint.empty()) return "round " + std::to_string(round) + ": an active row with no Deshacer and no reason, id " + std::to_string(row.id);
                if (row.state.empty()) return "empty state";
            }
        }
        return "";
    });

    Attack("Test 82.12: 'Volver todo' with failures: reverts the rest, names each failure with its fix, and a second call retries only the failures", [&]() -> std::string {
        Rig r;
        for (int i = 0; i < 5; ++i) r.Active(JournalTargetKind::RegistryValue, "HKLM\\K" + std::to_string(i));
        r.Active(JournalTargetKind::PowerPlan, "plan");
        r.undo.fail_targets = {"HKLM\\K1", "HKLM\\K3"};
        const auto first = gs::RestoreAllRecordedChanges(*r.journal, r.undo);
        if (first.total_count != 6 || first.reverted_count != 4 || first.failed_count != 2 || first.all_succeeded) return "first report " + std::to_string(first.reverted_count) + "/" + std::to_string(first.total_count);
        if (first.summary_message.find("HKLM\\K1") == std::string::npos || first.summary_message.find("HKLM\\K3") == std::string::npos) return "failures not named: " + first.summary_message;
        if (gs::RestoreAllMessage(first, 9, 9).empty()) return "empty message";
        const size_t reverted_before = r.undo.reverted_ids.size();
        r.undo.fail_targets.clear();
        const auto second = gs::RestoreAllRecordedChanges(*r.journal, r.undo);
        if (second.total_count != 2 || second.reverted_count != 2 || !second.all_succeeded) return "second report " + std::to_string(second.reverted_count) + "/" + std::to_string(second.total_count);
        if (r.undo.reverted_ids.size() != reverted_before + 2) return "second call reverted entries that were already reverted";
        const auto third = gs::RestoreAllRecordedChanges(*r.journal, r.undo);
        if (third.total_count != 0 || gs::RestoreAllMessage(third, 0, 0).find("0 de 0") == std::string::npos) return "third call: " + third.summary_message;
        return "";
    });

    Attack("Test 82.13: 'Volver todo' as a dry run reverts and marks nothing; on an empty journal it says 0 of 0", [&]() -> std::string {
        Rig r;
        const auto empty = gs::RestoreAllRecordedChanges(*r.journal, r.undo);
        if (empty.total_count != 0 || empty.summary_message.empty() || empty.summary_message.find("0 de 0") == std::string::npos) return "empty journal: '" + empty.summary_message + "'";
        const uint64_t id = r.Active(JournalTargetKind::RegistryValue, "HKLM\\X");
        const auto dry = gs::RestoreAllRecordedChanges(*r.journal, r.undo, true);
        if (!r.undo.reverted_ids.empty() || r.Find(id)->status == JournalEntryStatus::Reverted) return "dry run changed something";
        return dry.summary_message.find("[DRY-RUN]") != std::string::npos ? "" : "no DRY-RUN tag: " + dry.summary_message;
    });

    Attack("Test 82.14: 'Volver todo' on 400 entries reverts each once, newest first", [&]() -> std::string {
        Rig r;
        std::vector<uint64_t> ids;
        for (int i = 0; i < 400; ++i) ids.push_back(r.Active(JournalTargetKind::RegistryValue, "HKLM\\K" + std::to_string(i)));
        const auto rep = gs::RestoreAllRecordedChanges(*r.journal, r.undo);
        if (rep.reverted_count != 400 || r.undo.reverted_ids.size() != 400) return "reverted " + std::to_string(r.undo.reverted_ids.size());
        std::vector<uint64_t> expected(ids.rbegin(), ids.rend());
        return r.undo.reverted_ids == expected ? "" : "not newest first";
    });
}

} // namespace vftest
