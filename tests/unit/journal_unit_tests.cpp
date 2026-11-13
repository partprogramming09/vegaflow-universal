// Unit tests: persistent change journal, atomic Replace, crash detection, Safe Mode, and recovery messages (phase 2b).
// Called by tests/test_main.cpp; see tests/test_support.hpp.

#include <string>
#include <vector>

#include "session/restore.hpp"
#include "windows/change_journal.hpp"
#include "fake_file_system.hpp"
#include "fake_change_journal.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

std::string Pass(bool b) { return b ? "PASS" : "FAIL"; }

} // namespace

void Test49() {
    using namespace vegaflow;
    const std::wstring jpath = L"C:\\VegaFlow\\config\\change_journal.vfj";

    // 1. Fresh missing journal -> clean, not corrupt, not safe mode
    FakeFileSystem fs;
    FakeWallClock clock;
    FileChangeJournal j1(fs, clock, jpath);
    const bool load_missing = j1.Load();
    const auto eval_fresh = session::EvaluateStartupJournal(j1);
    const bool fresh_ok = load_missing && !eval_fresh.corrupt && !eval_fresh.previous_unclean_exit &&
                          !eval_fresh.safe_mode && eval_fresh.unclosed_or_unreverted_count == 0;

    // 2. Atomic write BEFORE change (Pending) and AFTER (Committed), then simulated crash #1
    const bool began = j1.BeginSession();
    JournalEntry reg_e;
    reg_e.kind = JournalTargetKind::RegistryValue;
    reg_e.target = "HKCU\\Software\\Test\\MPO";
    reg_e.reason = "Disable MPO";
    reg_e.before_value = "0";
    reg_e.after_value = "5";
    reg_e.manual_fix_hint = "reg add HKCU\\Software\\Test /v MPO /t REG_DWORD /d 0 /f";
    const size_t replaces_before = fs.replaces.size();
    const uint64_t id1 = j1.RecordPending(reg_e);
    const bool pending_persisted_before = (id1 != 0) && (fs.replaces.size() == replaces_before + 1) &&
                                          fs.writes.empty() &&
                                          (fs.files[jpath].find("PENDING") != std::string::npos);

    JournalEntry proc_e;
    proc_e.kind = JournalTargetKind::ProcessState;
    proc_e.target = "PID:4242";
    proc_e.reason = "Boost game";
    proc_e.before_value = "Normal";
    proc_e.after_value = "High";
    proc_e.manual_fix_hint = "Reinicia el juego PID 4242";
    const uint64_t id2 = j1.RecordPending(proc_e);
    const bool committed_ok = j1.MarkCommitted(id2);
    // Notice id1 stayed Pending and id2 is Committed; EndSessionClean() is NOT called -> simulated crash #1!

    // 3. Reload after crash #1: offers restore, streak == 1, safe_mode == false
    FileChangeJournal j_after_crash1(fs, clock, jpath);
    const bool loaded_c1 = j_after_crash1.Load();
    const auto eval_c1 = session::EvaluateStartupJournal(j_after_crash1);
    const bool crash1_detected = began && pending_persisted_before && committed_ok && loaded_c1 &&
                                 !eval_c1.corrupt && eval_c1.previous_unclean_exit &&
                                 eval_c1.effective_unclean_streak == 1 && !eval_c1.safe_mode &&
                                 eval_c1.unclosed_or_unreverted_count == 2;

    // 4. RestoreAllRecordedChanges reverts both (2 of 2) and clears recovery state
    FakeSystemUndo undo_ok;
    const auto rep_ok = session::RestoreAllRecordedChanges(j_after_crash1, undo_ok, false);
    const bool restore_2_of_2 = rep_ok.all_succeeded && rep_ok.reverted_count == 2 &&
                                rep_ok.total_count == 2 &&
                                rep_ok.summary_message == "Se devolvieron 2 de 2." &&
                                !j_after_crash1.IsSafeModeTriggered() &&
                                j_after_crash1.EffectiveUncleanStreak() == 0;

    // 5. Partial failure reports "N no se pudieron devolver..., y así se arreglan a mano:" + manual hint
    j_after_crash1.BeginSession();
    const uint64_t p1 = j_after_crash1.RecordPending(reg_e);
    j_after_crash1.MarkCommitted(p1);
    j_after_crash1.RecordPending(proc_e);
    FakeSystemUndo undo_partial;
    undo_partial.fail_targets.insert("HKCU\\Software\\Test\\MPO");
    const auto rep_partial = session::RestoreAllRecordedChanges(j_after_crash1, undo_partial, false);
    const bool partial_msg_ok = !rep_partial.all_succeeded && rep_partial.failed_count == 1 &&
                                rep_partial.reverted_count == 1 && rep_partial.total_count == 2 &&
                                rep_partial.summary_message.find("1 no se pudieron devolver") != std::string::npos &&
                                rep_partial.summary_message.find("y as\u00ed se arreglan a mano:") != std::string::npos &&
                                rep_partial.summary_message.find("reg add HKCU\\Software\\Test /v MPO") != std::string::npos;

    // 6. Two consecutive unclean exits trigger Safe Mode
    FakeFileSystem fs_sm;
    FileChangeJournal s1(fs_sm, clock, jpath);
    s1.Load();
    s1.BeginSession(); // crash #1 (never EndSessionClean)

    FileChangeJournal s2(fs_sm, clock, jpath);
    s2.Load();
    s2.BeginSession(); // crash #2 (never EndSessionClean)

    FileChangeJournal s3(fs_sm, clock, jpath);
    s3.Load();
    const auto eval_sm = session::EvaluateStartupJournal(s3);
    const bool safe_mode_triggered = eval_sm.previous_unclean_exit && eval_sm.effective_unclean_streak >= 2 &&
                                     eval_sm.safe_mode && s3.IsSafeModeTriggered();

    // 7. Corrupt journal never throws and reports corrupt cleanly
    FakeFileSystem fs_corrupt;
    fs_corrupt.files[jpath] = "CORRUPT_HEADER_GARBAGE\nENTRY\tbroken\n";
    FileChangeJournal jc(fs_corrupt, clock, jpath);
    const bool corrupt_load_ret = jc.Load();
    const auto eval_corrupt = session::EvaluateStartupJournal(jc);
    const bool corrupt_ok = !corrupt_load_ret && eval_corrupt.corrupt && !eval_corrupt.corrupt_reason.empty();

    // 8. Optional restore point prompt offered once, persisted, and recorded in history
    FakeFileSystem fs_rp;
    FileChangeJournal jrp(fs_rp, clock, jpath);
    jrp.Load();
    jrp.BeginSession();
    const bool should_offer_before = session::ShouldOfferRestorePoint(jrp);
    std::string rp_msg;
    const bool rp_ran = session::ExecuteRestorePointOffer(jrp, undo_ok, true, "Antes de registro", &rp_msg);
    const bool should_offer_after = session::ShouldOfferRestorePoint(jrp);
    const auto hist = session::FormatJournalHistory(jrp);
    const bool rp_ok = should_offer_before && rp_ran && !should_offer_after &&
                       undo_ok.restore_point_calls == 1 && !hist.empty() &&
                       hist.back().find("Windows Restore Point") != std::string::npos;

    const bool all = fresh_ok && crash1_detected && restore_2_of_2 && partial_msg_ok &&
                     safe_mode_triggered && corrupt_ok && rp_ok;
    RecordResult(all,
                 "Test 49: Change journal atomic writes, crash recovery, Safe Mode, and corrupt handling",
                 "fresh: " + Pass(fresh_ok) + ", crash1: " + Pass(crash1_detected) +
                     ", restore 2/2: " + Pass(restore_2_of_2) + ", partial msg: " + Pass(partial_msg_ok) +
                     ", safe mode: " + Pass(safe_mode_triggered) + ", corrupt: " + Pass(corrupt_ok) +
                     ", restore point: " + Pass(rp_ok));
}

} // namespace vftest
