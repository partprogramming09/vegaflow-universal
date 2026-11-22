#pragma once
// Port: what wakes the daemon loop. The real one (windows/event_sources) waits on window-focus, display-adapter
// and power events; tests script a fake that moves a fake clock instead of sleeping.

#include <cstdint>

namespace vegaflow {

enum class WakeReason { Stop, Foreground, Device, Power, Timer };

class EventSource {
public:
    virtual ~EventSource() = default;
    // Blocks up to timeout_ms and says why it returned; Timer means the timeout passed with no event.
    virtual WakeReason WaitForWake(uint32_t timeout_ms) = 0;
    // False when a hook could not be registered: the loop then polls like before E2.4.
    virtual bool EventsActive() const = 0;
};

} // namespace vegaflow
