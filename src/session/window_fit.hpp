#pragma once
// F4.7: the size of the main window against the usable screen area (screen minus taskbar). A fixed minimum taller than the
// area (175 % scale on 1080p) pushed the status bar under the taskbar. No Windows calls, so tests can drive it.

namespace vegaflow::session {

struct WindowFitInput {
    int desired_width = 0;   // design size, frame included
    int desired_height = 0;
    int min_width = 0;       // smallest size the person may drag the window to
    int min_height = 0;
    int work_width = 0;      // usable area of the monitor; 0 or negative when the query failed
    int work_height = 0;
};

struct WindowFit {
    int width = 0;
    int height = 0;
    int min_width = 0;
    int min_height = 0;
};

// Size and minimum never exceed the work area; an invalid area keeps the design values.
WindowFit FitWindowToWorkArea(const WindowFitInput& in);

// Vertical pitch of the sidebar tab buttons: `available` px shared by `entries` buttons, never above `max_step` (the design
// pitch) nor below `min_step`; an invalid `available` keeps `max_step`. Lets the eight tabs and the block under them fit a
// short window instead of overlapping.
int SidebarTabStep(int available, int entries, int max_step, int min_step);

} // namespace vegaflow::session
