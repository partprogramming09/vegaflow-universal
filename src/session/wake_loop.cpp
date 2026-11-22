#include "session/wake_loop.hpp"

#include <algorithm>
#include <chrono>

namespace vegaflow::session {

namespace {

void Merge(WakeSet& set, WakeReason r) {
    if (r == WakeReason::Foreground) set.foreground = true;
    if (r == WakeReason::Device) set.device = true;
    if (r == WakeReason::Power) set.power = true;
}

} // namespace

void RunWakeLoop(EventSource& events, const Clock& clock, uint32_t poll_interval_ms, const WakeLoopHooks& hooks) {
    using namespace std::chrono;
    const bool event_mode = events.EventsActive();
    if (!event_mode && hooks.log) {
        hooks.log("[WARN] Event hooks unavailable: polling every " + std::to_string(kPollFallbackMs) + " ms.");
    }
    bool has_last = false;
    steady_clock::time_point last{};

    while (!hooks.should_stop()) {
        const bool game = hooks.game_active();
        const uint32_t timeout = !event_mode ? kPollFallbackMs
                               : game        ? std::max(poll_interval_ms, kMinWakeGapMs)
                                             : kIdleSafetyNetMs;
        const WakeReason reason = events.WaitForWake(timeout);
        if (reason == WakeReason::Stop) break;
        WakeSet set;
        Merge(set, reason);
        const bool timer_only = reason == WakeReason::Timer;

        bool stopped = false;
        if (event_mode && has_last) {
            // Events inside the gap are merged, not lost: the one iteration at the end of the gap sees them all.
            const auto deadline = last + milliseconds(kMinWakeGapMs);
            for (auto now = clock.Now(); now < deadline; now = clock.Now()) {
                const auto left = std::max<int64_t>(1, duration_cast<milliseconds>(deadline - now).count());
                const WakeReason r = events.WaitForWake(static_cast<uint32_t>(left));
                if (r == WakeReason::Stop) { stopped = true; break; }
                Merge(set, r);
            }
        }
        if (stopped) break;

        const bool check = !event_mode || set.device || (timer_only && !game);
        hooks.iterate(set, check);
        last = clock.Now();
        has_last = true;
    }
    if (hooks.cleanup) hooks.cleanup();
}

const char* NoDesktopWarning(uint32_t session_id) {
    return session_id == 0
        ? "[WARN] Running in Windows session 0 (no desktop): game detection will not see any window."
        : nullptr;
}

} // namespace vegaflow::session
