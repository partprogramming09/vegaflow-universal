// F4.7 unit tests: the window size allowed by the usable screen area (no Windows calls; session/window_fit is pure).
// Called by tests/test_main.cpp; see tests/test_support.hpp.

#include "session/window_fit.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

void Test150() {
    using namespace vegaflow::session;
    const auto str = [](const WindowFit& f) {
        return std::to_string(f.width) + "x" + std::to_string(f.height) + " min " + std::to_string(f.min_width) + "x" + std::to_string(f.min_height);
    };

    { // work area larger than the design size: nothing changes
        const WindowFit f = FitWindowToWorkArea({1136, 779, 960, 640, 1920, 1032});
        RecordResult(f.width == 1136 && f.height == 779 && f.min_width == 960 && f.min_height == 640,
                     "Test 150.1: roomy work area keeps the design and minimum sizes", str(f));
    }
    { // 175 % on 1080p: the unaware window sees about 1097 x 590; the size AND the minimum must shrink to it
        const WindowFit f = FitWindowToWorkArea({1136, 779, 960, 640, 1097, 590});
        RecordResult(f.width == 1097 && f.height == 590 && f.min_width == 960 && f.min_height == 590,
                     "Test 150.2: small work area limits the size and the minimum height", str(f));
    }
    { // work area smaller than the minimum in both directions
        const WindowFit f = FitWindowToWorkArea({1136, 779, 960, 640, 800, 500});
        RecordResult(f.width == 800 && f.height == 500 && f.min_width == 800 && f.min_height == 500,
                     "Test 150.3: minimum never exceeds the work area", str(f));
    }
    { // work area exactly the design size
        const WindowFit f = FitWindowToWorkArea({1136, 779, 960, 640, 1136, 779});
        RecordResult(f.width == 1136 && f.height == 779 && f.min_width == 960 && f.min_height == 640,
                     "Test 150.4: work area equal to the design size changes nothing", str(f));
    }
    { // zero, negative or missing area (query failed): keep the design, never a zero-size window
        bool ok = true;
        for (const WindowFit& f : {FitWindowToWorkArea({1136, 779, 960, 640, 0, 0}), FitWindowToWorkArea({1136, 779, 960, 640, -5, 590}),
                                   FitWindowToWorkArea({1136, 779, 960, 640, 1097, 0})})
            ok = ok && f.width == 1136 && f.height == 779 && f.min_width == 960 && f.min_height == 640;
        RecordResult(ok, "Test 150.5: invalid work area keeps the design sizes");
    }
    { // a design size smaller than the minimum is raised to it, then limited by the work area
        const WindowFit f = FitWindowToWorkArea({500, 300, 960, 640, 1920, 1032});
        RecordResult(f.width == 960 && f.height == 640, "Test 150.6: design below the minimum is raised to it", str(f));
    }
    { // tab pitch: roomy window keeps the design step, a short one shares the room, a tiny or invalid one is bounded
        const int roomy = SidebarTabStep(477, 8, 44, 30);
        const int shared = SidebarTabStep(276, 8, 44, 30);
        const int tiny = SidebarTabStep(100, 8, 44, 30);
        const int zero = SidebarTabStep(0, 8, 44, 30);
        const int none = SidebarTabStep(300, 0, 44, 30);
        RecordResult(roomy == 44 && shared == 34 && tiny == 30 && zero == 44 && none == 44, "Test 150.7: sidebar tab step shrinks to fit and stays within 30..44",
                     std::to_string(roomy) + "," + std::to_string(shared) + "," + std::to_string(tiny) + "," + std::to_string(zero) + "," + std::to_string(none));
    }
}

} // namespace vftest
