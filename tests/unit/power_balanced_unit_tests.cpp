// Unit test (F1.2 review): the manual "Balanced" with no copy of the original plan journals the plan it finds before
// writing, writes nothing when it cannot read it, and writes nothing when the plan already is Balanced.

#include <windows.h>

#include "optimize/graphics/graphics_optimizer.hpp"
#include "windows/change_journal.hpp"
#include "fake_change_journal.hpp"
#include "fake_file_system.hpp"
#include "fake_registry_power.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test134() {
    namespace v = vegaflow;
    v::ApuOptimizationPolicy pol{};
    FakeFileSystem fs;
    FakeWallClock clock;
    v::FileChangeJournal journal(fs, clock, L"C:\\VegaFlow\\config\\change_journal.vfj");
    journal.Load();
    journal.BeginSession();
    v::ChangeJournal* previous = v::GetActiveChangeJournal();
    v::SetActiveChangeJournal(&journal);

    auto plan_entries = [&] {
        size_t n = 0;
        for (const auto& e : journal.Entries()) if (e.kind == v::JournalTargetKind::PowerPlan) ++n;
        return n;
    };

    // 1. Unreadable plan: nothing is written and nothing is journaled.
    FakePowerPort unreadable;
    unreadable.current = v::ApuPowerPlanValues{ { 0, 4, 50, 0, 0, 100, 25 }, { 0, 4, 50, 0, 0, 100, 25 } };
    unreadable.read_ok = false;
    const bool refused = !v::PowerModeTweaks(unreadable).SetApuLaptopPowerProfile(false, pol, false) && unreadable.apply_count == 0 && plan_entries() == 0;

    // 2. The plan already holds Balanced's values: no write, answer yes.
    FakePowerPort already;
    const bool already_ok = v::PowerModeTweaks(already).SetApuLaptopPowerProfile(false, pol, false) && already.apply_count == 0 && plan_entries() == 0;

    // 3. Another plan state, no copy: the journal gets the previous values before the first write.
    FakePowerPort other;
    const v::ApuPowerPlanValues before{ { 0, 4, 50, 0, 0, 100, 25 }, { 0, 4, 50, 0, 0, 100, 25 } };
    other.current = before;
    bool journaled_first = false;
    other.on_apply = [&] {
        for (const auto& e : journal.Entries()) {
            if (e.kind == v::JournalTargetKind::PowerPlan && e.ac_values == before.ac && e.dc_values == before.dc) journaled_first = true;
        }
    };
    const bool written = v::PowerModeTweaks(other).SetApuLaptopPowerProfile(false, pol, false) && other.apply_count == 1 && other.current.ac[6] == 50;
    bool kept = false;
    for (const auto& e : journal.Entries()) {
        if (e.kind == v::JournalTargetKind::PowerPlan && e.status == v::JournalEntryStatus::Committed && e.ac_values == before.ac) kept = true;
    }

    v::SetActiveChangeJournal(previous);
    RecordResult(refused && already_ok && journaled_first && written && kept,
                 "Test 134: the manual Balanced journals the plan it finds before writing, and writes nothing if it cannot read or nothing differs (F1.2 review)",
                 std::string("UnreadableRefused=") + (refused ? "1" : "0") + ", AlreadyBalanced=" + (already_ok ? "1" : "0") +
                     ", JournaledFirst=" + (journaled_first ? "1" : "0") + ", Written=" + (written ? "1" : "0") + ", EntryKept=" + (kept ? "1" : "0"));
}

} // namespace vftest
