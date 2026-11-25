// F4.1 unit tests: how often the window polls while it is visible, hidden in the tray, with or without a game.
// No Windows calls (session/gui_cadence is pure). Called by tests/test_main.cpp; see tests/test_support.hpp.

#include "session/gui_cadence.hpp"
#include "session/wake_loop.hpp"
#include "../test_support.hpp"

namespace vftest {

void Test143() {
    using namespace vegaflow::session;
    const auto plan = [](bool visible, bool game, bool events) { return PlanGuiCadence({visible, game, events}); };

    { // visible: the 1500 ms refresh and the pad tick, with or without a game and with or without events
        bool ok = true;
        for (bool game : {false, true})
            for (bool events : {false, true}) {
                const GuiCadence c = plan(true, game, events);
                ok = ok && c.telemetry_ms == kPollFallbackMs && c.pad_active;
            }
        RecordResult(ok, "Test 143.1: visible window polls every 1500 ms and keeps the pad tick");
    }
    { // hidden, no game, events working: only the safety net, no pad tick
        const GuiCadence c = plan(false, false, true);
        RecordResult(c.telemetry_ms == kIdleSafetyNetMs && kIdleSafetyNetMs == 30000 && !c.pad_active,
                     "Test 143.2: hidden without a game -> 30 s safety net, no pad tick", std::to_string(c.telemetry_ms) + " ms");
    }
    { // hidden with a game: the daemon part still detects it closing, so 1500 ms, but no pad tick
        const GuiCadence c = plan(false, true, true);
        RecordResult(c.telemetry_ms == kPollFallbackMs && !c.pad_active, "Test 143.3: hidden with a game keeps 1500 ms, no pad tick");
    }
    { // hidden, hooks failed: the old polling, because nothing else would notice a game
        const GuiCadence c = plan(false, false, false);
        RecordResult(c.telemetry_ms == kPollFallbackMs && !c.pad_active, "Test 143.4: hidden without events -> 1500 ms polling");
    }
    { // the game closes while the window is hidden: the next plan drops to the safety net
        const GuiCadence during = plan(false, true, true);
        const GuiCadence after = plan(false, false, true);
        RecordResult(during.telemetry_ms == kPollFallbackMs && after.telemetry_ms == kIdleSafetyNetMs,
                     "Test 143.5: game closed while hidden -> cadence falls to the safety net");
    }
    { // showing the window polls at once; hiding or staying the same does not
        RecordResult(PollWhenVisibilityChanges(false, true) && !PollWhenVisibilityChanges(true, false) &&
                         !PollWhenVisibilityChanges(true, true) && !PollWhenVisibilityChanges(false, false),
                     "Test 143.6: only hidden -> visible polls at once");
    }
    { // a wake event polls only when the 1500 ms timer is not already doing it
        RecordResult(WakeNeedsPoll(false) && !WakeNeedsPoll(true), "Test 143.7: events poll a hidden window, a visible one has its timer");
    }
}

} // namespace vftest
