#pragma once
// The daemon's waiting rules (E2.4): wake on events, one iteration per wake, a timer only as a heartbeat.

#include <cstdint>
#include <functional>
#include <string>

#include "ports/clock.hpp"
#include "ports/event_source.hpp"

namespace vegaflow::session {

constexpr uint32_t kIdleSafetyNetMs = 30000;  // no game: only a net in case an event is lost
constexpr uint32_t kMinWakeGapMs = 250;       // Alt+Tab storms collapse into one iteration per gap
constexpr uint32_t kPollFallbackMs = 1500;    // when a hook failed: the old polling

struct WakeSet {
    bool foreground = false;
    bool device = false;
    bool power = false;
};

struct WakeLoopHooks {
    std::function<bool()> should_stop;  // e.g. Ctrl+C set g_running to false
    std::function<bool()> game_active;
    // check_adapters: read the adapter signature in this iteration (device event, safety net, or polling mode).
    std::function<void(const WakeSet&, bool check_adapters)> iterate;
    std::function<void()> cleanup;      // runs exactly once when the loop ends
    std::function<void(const std::string&)> log;
};

void RunWakeLoop(EventSource& events, const Clock& clock, uint32_t poll_interval_ms, const WakeLoopHooks& hooks);

// Message for the session-0 banner, or nullptr when the session has a desktop.
const char* NoDesktopWarning(uint32_t session_id);

} // namespace vegaflow::session
