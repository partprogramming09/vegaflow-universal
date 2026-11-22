// E2.7: the window's wiring of the phase G extras, tested through the session-level flows the window calls (no <windows.h>):
// close with changes in force, the panic shortcut, the welcome mark, the game pad, the restart notice and the diagnosis panel.

#include "session/config_screen.hpp"
#include "session/gui_flow.hpp"
#include "session/welcome.hpp"
#include "../test_support.hpp"
#include "fake_file_system.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;
using std::chrono::milliseconds;

gs::HistoryScreenModel History(size_t active) {
    gs::HistoryScreenModel h;
    for (size_t i = 0; i < active; ++i) {
        gs::HistoryRow r;
        r.id = i + 1;
        r.state = "Activo";
        h.rows.push_back(r);
    }
    return h;
}

} // namespace

// E2.7
void Test115() {
    std::string q;
    // 1: no changes in force
    {
        gs::CloseFlow f;
        RecordResult(f.OnClose(History(0), &q) == gs::CloseStep::Close && !f.dialog_open(), "Test 115.1: closing with 0 changes closes without asking");
    }
    // 2: two changes, "Dejarlos"
    {
        gs::CloseFlow f;
        const gs::HistoryScreenModel h = History(2);
        int resets = 0;
        const bool shown = f.OnClose(h, &q) == gs::CloseStep::ShowDialog && f.dialog_open() && !q.empty();
        const bool close = f.OnChoice(gs::CloseChoice::KeepAndClose, [&] { ++resets; });
        RecordResult(shown && close && resets == 0 && h.rows.size() == 2 && h.rows[0].state == "Activo" && !f.dialog_open(),
                     "Test 115.2: two changes and 'Dejarlos' close, nothing is reset and the history is intact");
    }
    // 3: two changes, "Devolver": ResetAll runs, then it closes
    {
        gs::CloseFlow f;
        std::vector<std::string> order;
        f.OnClose(History(2), &q);
        const bool close = f.OnChoice(gs::CloseChoice::RestoreAndClose, [&] { order.push_back("reset"); });
        order.push_back(close ? "close" : "stay");
        RecordResult(order == std::vector<std::string>{"reset", "close"}, "Test 115.3: 'Devolver' resets everything and then closes");
    }
    // 3b: cancel keeps the window and lets the next close ask again
    {
        gs::CloseFlow f;
        f.OnClose(History(1), &q);
        const bool close = f.OnChoice(gs::CloseChoice::Cancel, [] {});
        RecordResult(!close && f.OnClose(History(1), &q) == gs::CloseStep::ShowDialog, "Test 115.3b: cancel stays open and the next close asks again");
    }
    // 10: a second close while the dialog is open
    {
        gs::CloseFlow f;
        const auto a = f.OnClose(History(2), &q);
        const auto b = f.OnClose(History(2), &q);
        const auto c = f.OnClose(History(0), &q);
        RecordResult(a == gs::CloseStep::ShowDialog && b == gs::CloseStep::Ignore && c == gs::CloseStep::Ignore, "Test 115.10: a second close with the dialog open opens no second dialog");
    }

    // 4: the shortcut is taken by another program
    {
        const gs::PanicHotkey h = gs::DefaultPanicHotkey();
        const std::string taken = gs::PanicHotkeyNotice(h, false);
        RecordResult(!taken.empty() && taken.find(h.label) != std::string::npos && gs::PanicHotkeyNotice(h, true).empty(),
                     "Test 115.4: a taken shortcut gives a visible notice naming it; a registered one gives none", taken);
        gs::PanicHotkey unsafe = h;
        unsafe.alt = unsafe.shift = false;
        RecordResult(gs::PlanPanicHotkey(unsafe).vk == 0 && !gs::PanicHotkeyNotice(unsafe, false).empty(), "Test 115.4b: an unsafe shortcut is never registered");
    }
    // 5: pressed twice in a row
    {
        gs::PanicGuard g;
        const auto t0 = std::chrono::steady_clock::time_point{} + std::chrono::seconds(100);
        const bool first = g.TryBegin(t0);
        const bool while_running = g.TryBegin(t0 + milliseconds(10));
        g.End(t0 + milliseconds(500));
        const bool queued_second = g.TryBegin(t0 + milliseconds(600)); // the repeat that waited in the queue
        g.End(t0 + milliseconds(700));
        const bool much_later = g.TryBegin(t0 + milliseconds(5000));
        RecordResult(first && !while_running && !queued_second && much_later, "Test 115.5: two presses in a row start one restore, a later press starts another");
    }
    // 11: held down
    {
        const gs::PanicHotkeyPlan p = gs::PlanPanicHotkey(gs::DefaultPanicHotkey());
        const unsigned want = gs::kHotModControl | gs::kHotModAlt | gs::kHotModShift | gs::kHotModNoRepeat;
        RecordResult(p.modifiers == want && p.vk == 'R', "Test 115.11: the shortcut is registered with MOD_NOREPEAT (Windows then sends one WM_HOTKEY per press)");
    }

    // 6: no xinput1_4.dll
    {
        gs::PadNavigator nav;
        bool ok = true;
        ok = ok && gs::PollPad(nav, nullptr) == gs::PadAction::None;
        ok = ok && gs::PollPad(nav, [](uint16_t*) { return false; }) == gs::PadAction::None;
        const auto down = [](uint16_t* b) { *b = gs::kPadDpadDown; return true; };
        const auto a1 = gs::PollPad(nav, down);
        const auto a2 = gs::PollPad(nav, down); // held: one action
        RecordResult(ok && a1 == gs::PadAction::FocusNext && a2 == gs::PadAction::None, "Test 115.6: without the library or a pad nothing happens; with one a held button acts once");
    }

    // 7: second opening
    {
        FakeFileSystem fs;
        const std::wstring mark = L"C:\\vf\\welcome.seen";
        const bool first = gs::ShouldShowWelcome(fs, mark);
        const bool wrote = gs::MarkWelcomeSeen(fs, mark);
        RecordResult(first && wrote && !gs::ShouldShowWelcome(fs, mark), "Test 115.7: the welcome shows once; a second opening has none");
    }

    // 8: a row read at start says so
    {
        gs::ConfigRow later;
        later.applies_live = false;
        gs::ConfigRow live;
        RecordResult(!gs::RestartNotice(later).empty() && gs::RestartNotice(live).empty(), "Test 115.8: a row with applies_live=false shows the restart notice, one with true does not", gs::RestartNotice(later));
    }

    // 9: diagnosis without administrator: what could be read is shown as it is; nothing at all gets a sentence
    {
        const std::string partial = "Elevated: no\nSystem drive: could not be read\nShader cache: 1.2 GiB\n";
        RecordResult(gs::DoctorPanelText(partial) == partial && !gs::DoctorPanelText("").empty(), "Test 115.9: the diagnosis panel shows what was read, and says so when nothing was");
    }
}

} // namespace vftest
