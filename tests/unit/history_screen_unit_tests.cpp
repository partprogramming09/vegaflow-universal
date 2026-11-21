// Unit tests: the model of the "Historial" screen (phase G): rows from the change journal, newest first, with the value
// before and after, a state word, and a per-row undo that only acts on changes still active.

#include "session/history_screen.hpp"
#include "windows/change_journal.hpp"
#include "fake_change_journal.hpp"
#include "fake_file_system.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

namespace {

vegaflow::JournalEntry Entry(vegaflow::JournalTargetKind kind, const std::string& target, const std::string& reason) {
    vegaflow::JournalEntry e;
    e.kind = kind;
    e.target = target;
    e.reason = reason;
    e.before_value = "antes";
    e.after_value = "despues";
    e.manual_fix_hint = "arreglo a mano de " + target;
    return e;
}

} // namespace

void Test64() {
    using namespace vegaflow;
    const std::wstring path = L"C:\\VegaFlow\\config\\change_journal.vfj";

    // 1. An empty journal gives no rows and a sentence for the empty state.
    FakeFileSystem fs;
    FakeWallClock clock;
    FileChangeJournal journal(fs, clock, path);
    journal.Load();
    const auto empty = session::BuildHistoryScreen(journal.Entries());
    RecordResult(empty.rows.empty() && !empty.empty_text.empty(), "Test 64.1: an empty journal shows no rows and an empty-state sentence");

    // 2. Newest first; audit events and restore points are not "changes" and stay out.
    journal.BeginSession();
    const uint64_t a = journal.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\A", "primero"));
    journal.MarkCommitted(a);
    journal.RecordAuditEvent("Windows Restore Point", "Omitido", "detalle");
    const uint64_t b = journal.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\B", "segundo"));
    journal.MarkCommitted(b);
    const auto two = session::BuildHistoryScreen(journal.Entries());
    RecordResult(two.rows.size() == 2 && two.rows[0].id == b && two.rows[1].id == a && two.rows[0].what == "segundo" &&
                     two.rows[0].before == "antes" && two.rows[0].after == "despues",
                 "Test 64.2: rows are newest first, carry before and after, and leave out audit events");

    // 3. State words and who can be undone: active yes; undone, failed no (failed shows the manual hint).
    const uint64_t c = journal.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\C", "tercero"));
    journal.MarkCommitted(c);
    journal.MarkReverted(c);
    const uint64_t d = journal.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\D", "cuarto"));
    journal.MarkFailed(d, "no se pudo");
    const auto four = session::BuildHistoryScreen(journal.Entries());
    const auto find = [&](uint64_t id) -> const session::HistoryRow* {
        for (const auto& r : four.rows) {
            if (r.id == id) return &r;
        }
        return nullptr;
    };
    const auto* ra = find(a);
    const auto* rc = find(c);
    const auto* rd = find(d);
    RecordResult(ra && rc && rd && ra->state == "Activo" && ra->can_undo && rc->state == "Deshecho" && !rc->can_undo &&
                     rd->state == "Falló" && !rd->can_undo && rd->manual_hint.find("arreglo a mano") != std::string::npos,
                 "Test 64.3: active rows can be undone; undone and failed rows cannot, and a failed row carries the manual fix");

    // 4. The row limit keeps the newest rows.
    const auto limited = session::BuildHistoryScreen(journal.Entries(), 2);
    RecordResult(limited.rows.size() == 2 && limited.rows[0].id == d && limited.rows[1].id == c, "Test 64.4: the row limit keeps the newest rows");

    // 5. Undo one change: reverts it, marks it reverted, touches nothing else.
    FakeSystemUndo undo;
    const auto ok = session::UndoOneChange(journal, undo, a);
    const auto after_undo = session::BuildHistoryScreen(journal.Entries());
    bool a_undone = false;
    bool b_active = false;
    for (const auto& r : after_undo.rows) {
        if (r.id == a) a_undone = r.state == "Deshecho";
        if (r.id == b) b_active = r.state == "Activo";
    }
    RecordResult(ok.ok && undo.reverted_ids == std::vector<uint64_t>{a} && a_undone && b_active,
                 "Test 64.5: undoing one row reverts only that change and marks it undone");

    // 6. A second undo of the same row, or of an unknown id, does nothing.
    FakeSystemUndo undo_again;
    const auto again = session::UndoOneChange(journal, undo_again, a);
    const auto unknown = session::UndoOneChange(journal, undo_again, 9999);
    RecordResult(!again.ok && !unknown.ok && undo_again.reverted_ids.empty() && !again.message.empty() && !unknown.message.empty(),
                 "Test 64.6: undoing an already undone or unknown change does nothing and says why");

    // 7. A failing revert keeps the row active and reports the reason and the manual fix.
    FakeSystemUndo undo_fail;
    undo_fail.fail_targets.insert("HKLM\\B");
    const auto bad = session::UndoOneChange(journal, undo_fail, b);
    bool b_still_active = false;
    for (const auto& r : session::BuildHistoryScreen(journal.Entries()).rows) {
        if (r.id == b) b_still_active = r.state == "Activo";
    }
    RecordResult(!bad.ok && b_still_active && bad.message.find("Permiso denegado") != std::string::npos &&
                     bad.message.find("arreglo a mano") != std::string::npos,
                 "Test 64.7: a failed undo keeps the row active and explains the failure and the manual fix");

    // 8. A power plan or process change has no single undo (its state lives in memory; restore-all resets it), and an
    //    old change cannot be undone while a newer active one touches the same target.
    FileChangeJournal j2(fs, clock, L"C:\\VegaFlow\\config\\second.vfj");
    j2.Load();
    j2.BeginSession();
    const uint64_t plan = j2.RecordPending(Entry(JournalTargetKind::PowerPlan, "plan", "plan de energia"));
    j2.MarkCommitted(plan);
    const uint64_t old_reg = j2.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\X", "viejo"));
    j2.MarkCommitted(old_reg);
    const uint64_t new_reg = j2.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\X", "nuevo"));
    j2.MarkCommitted(new_reg);
    const auto m2 = session::BuildHistoryScreen(j2.Entries());
    const auto row_of = [&](uint64_t id) -> const session::HistoryRow* {
        for (const auto& r : m2.rows) {
            if (r.id == id) return &r;
        }
        return nullptr;
    };
    FakeSystemUndo undo_blocked;
    const auto r1 = session::UndoOneChange(j2, undo_blocked, plan);
    const auto r2 = session::UndoOneChange(j2, undo_blocked, old_reg);
    RecordResult(row_of(plan) && !row_of(plan)->can_undo && row_of(old_reg) && !row_of(old_reg)->can_undo && row_of(new_reg) &&
                     row_of(new_reg)->can_undo && !r1.ok && !r2.ok && undo_blocked.reverted_ids.empty() &&
                     !row_of(plan)->manual_hint.empty() && !row_of(old_reg)->manual_hint.empty(),
                 "Test 64.8: plan changes and out-of-order undo are blocked, with the reason shown");

    // 9. dry_run reports what would be undone and changes nothing.
    FakeSystemUndo undo_dry;
    const auto dry = session::UndoOneChange(j2, undo_dry, new_reg, true);
    RecordResult(dry.ok && undo_dry.reverted_ids.empty() && dry.message.find("[DRY-RUN]") != std::string::npos &&
                     session::BuildHistoryScreen(j2.Entries()).rows[0].state == "Activo",
                 "Test 64.9: a dry run undoes nothing and leaves the row active");
}

void Test100() {
    // E1.6: the journal revision moves only when the journal changes, so the GUI can skip rebuilding the Historial model.
    using namespace vegaflow;
    FakeFileSystem fs;
    FakeWallClock clock;
    FileChangeJournal journal(fs, clock, L"C:\\VegaFlow\\config\\change_journal.vfj");
    journal.Load();
    journal.BeginSession();
    const uint64_t r0 = journal.Revision();
    (void)session::BuildHistoryScreen(journal.Entries());
    const bool reads_keep = journal.Revision() == r0;
    const uint64_t id = journal.RecordPending(Entry(JournalTargetKind::RegistryValue, "HKLM\\A", "uno"));
    const uint64_t r1 = journal.Revision();
    journal.MarkCommitted(id);
    const uint64_t r2 = journal.Revision();
    const bool writes_move = r1 != r0 && r2 != r1;
    const bool unknown_id_keeps = !journal.MarkCommitted(9999) && journal.Revision() == r2;
    RecordResult(reads_keep && writes_move && unknown_id_keeps, "Test 100: journal Revision() moves only on changes",
                 std::string("reads keep: ") + (reads_keep ? "PASS" : "FAIL") + ", writes move: " + (writes_move ? "PASS" : "FAIL") +
                 ", unknown id keeps: " + (unknown_id_keeps ? "PASS" : "FAIL"));
}

} // namespace vftest