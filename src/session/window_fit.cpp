#include "session/window_fit.hpp"

#include <algorithm>

namespace vegaflow::session {

WindowFit FitWindowToWorkArea(const WindowFitInput& in) {
    WindowFit out;
    out.min_width = in.min_width;
    out.min_height = in.min_height;
    out.width = std::max(in.desired_width, in.min_width);
    out.height = std::max(in.desired_height, in.min_height);
    if (in.work_width <= 0 || in.work_height <= 0) return out;
    out.min_width = std::min(out.min_width, in.work_width);
    out.min_height = std::min(out.min_height, in.work_height);
    out.width = std::min(out.width, in.work_width);
    out.height = std::min(out.height, in.work_height);
    return out;
}

int SidebarTabStep(int available, int entries, int max_step, int min_step) {
    if (available <= 0 || entries <= 0) return max_step;
    return std::clamp(available / entries, std::min(min_step, max_step), max_step);
}

} // namespace vegaflow::session
