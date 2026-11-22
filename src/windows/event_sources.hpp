#pragma once
// EventSource port on Windows (E2.4): foreground-window hook, display-adapter arrival/removal and AC/DC power
// notifications. Create it on the thread that calls WaitForWake: the hook and the message-only window need that
// thread's message queue.

#include <windows.h>

#include <atomic>

#include "ports/event_source.hpp"

namespace vegaflow {

class WindowsEventSource final : public EventSource {
public:
    // stop_event: signalled by Ctrl+C or the named Global\VegaFlowStop event.
    explicit WindowsEventSource(HANDLE stop_event);
    ~WindowsEventSource() override;
    WindowsEventSource(const WindowsEventSource&) = delete;
    WindowsEventSource& operator=(const WindowsEventSource&) = delete;

    WakeReason WaitForWake(uint32_t timeout_ms) override;
    bool EventsActive() const override { return active_; }

    // Called by the native callbacks (public so the file-local trampolines can reach it).
    void Post(WakeReason r);

private:
    void Release();
    void Pump();
    bool TakePending(WakeReason& out);

    HANDLE stop_event_;
    HANDLE device_event_ = nullptr;  // auto-reset, signalled by the CM callback
    HWINEVENTHOOK foreground_hook_ = nullptr;
    HWND window_ = nullptr;
    HPOWERNOTIFY power_notify_ = nullptr;
    void* cm_notify_ = nullptr;      // HCMNOTIFICATION, kept opaque so this header needs no cfgmgr32.h
    std::atomic<unsigned> pending_{0};  // bit per WakeReason, set by callbacks, read on the loop thread
    bool active_ = false;
};

} // namespace vegaflow
