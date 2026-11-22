// Unit test: turning memory compression on again only closes the journal entries when it lands on the state the user had.
// Original off: on -> off -> on must keep the entries open, or the revert can no longer turn it off again.

#include "ports/change_journal.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test119() {
    using vegaflow::EnablingClosesMemoryCompressionEntries;
    // Original on: turn off, then on again = back where the user was; entries close.
    const bool orig_on_closes = EnablingClosesMemoryCompressionEntries(true, true);
    // Original off: on -> off -> on again does NOT reach the original; entries stay open.
    const bool orig_off_stays_open = !EnablingClosesMemoryCompressionEntries(true, false);
    // Disabling never closes entries (it records one more).
    const bool disable_stays_open = !EnablingClosesMemoryCompressionEntries(false, true) &&
                                    !EnablingClosesMemoryCompressionEntries(false, false);

    RecordResult(orig_on_closes && orig_off_stays_open && disable_stays_open,
                 "Test 119: memory compression entries close only when the original state is reached",
                 std::string("OrigOnCloses=") + (orig_on_closes ? "1" : "0") + ", OrigOffOpen=" + (orig_off_stays_open ? "1" : "0") +
                     ", DisableOpen=" + (disable_stays_open ? "1" : "0"));
}

} // namespace vftest
