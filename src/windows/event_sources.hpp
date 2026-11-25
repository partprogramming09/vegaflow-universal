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

    // F4.1: for a caller that owns its own message loop (the window) instead of calling WaitForWake: every event also
    // posts `msg` to `hwnd`. Call it on the creating thread, once, before the loop runs.
    void NotifyWindow(HWND hwnd, UINT msg);
    // Called by the device callback, on a system thread.
    void SignalDevice();

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
    std::atomic<HWND> notify_hwnd_{nullptr};
    std::atomic<UINT> notify_msg_{0};
    std::atomic<unsigned> pending_{0};  // bit per WakeReason, set by callbacks, read on the loop thread
    bool active_ = false;
};

} // namespace vegaflow
