#include "session/gui_cadence.hpp"

#include "session/wake_loop.hpp"

namespace vegaflow::session {

GuiCadence PlanGuiCadence(const GuiCadenceInput& in) {
    GuiCadence c;
    c.pad_active = in.visible;
    // A game keeps the 1500 ms poll (the daemon part must see it close); the safety net is only for the idle tray.
    c.telemetry_ms = (!in.visible && !in.game_active && in.events_active) ? kIdleSafetyNetMs : kPollFallbackMs;
    return c;
}

bool PollWhenVisibilityChanges(bool was_visible, bool visible) { return !was_visible && visible; }

bool WakeNeedsPoll(bool visible) { return !visible; }

} // namespace vegaflow::session
