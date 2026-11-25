#pragma once
// F4.1: how often the window polls. Visible, it refreshes every 1500 ms; hidden in the tray with no game it sleeps on the
// daemon's own events plus a 30 s safety net. No Windows calls, so tests can drive it.

#include <cstdint>

namespace vegaflow::session {

struct GuiCadenceInput {
    bool visible = true;       // shown and not minimised
    bool game_active = false;
    bool events_active = true; // false when a hook could not be registered: polling like before E2.4
};

struct GuiCadence {
    uint32_t telemetry_ms = 0;
    bool pad_active = false;   // the 50 ms game-pad tick only makes sense while the window can be pressed
};

GuiCadence PlanGuiCadence(const GuiCadenceInput& in);

// Showing the window must not wait for the next tick.
bool PollWhenVisibilityChanges(bool was_visible, bool visible);

// A hidden window polls on every wake event (the caller merges a burst); a visible one already has its timer.
bool WakeNeedsPoll(bool visible);

} // namespace vegaflow::session
