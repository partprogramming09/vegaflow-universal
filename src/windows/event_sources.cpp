#include "windows/event_sources.hpp"

#include <cfgmgr32.h>

#include <algorithm>
#include <iostream>

namespace vegaflow {

namespace {

constexpr unsigned Bit(WakeReason r) { return 1u << static_cast<unsigned>(r); }

// Defined here so linking needs no uuid library.
const GUID kDisplayAdapterInterface = {0x5b45201d, 0xf2f2, 0x4f3b, {0x85, 0xbb, 0x30, 0xff, 0x1f, 0x95, 0x35, 0x99}};
const GUID kAcDcPowerSource = {0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};

WindowsEventSource* g_source = nullptr;  // one per process: WinEvent hooks carry no context pointer

void CALLBACK ForegroundProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_source) g_source->Post(WakeReason::Foreground);
}

LRESULT CALLBACK PowerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // A message-only window gets no PBT_APMPOWERSTATUSCHANGE broadcast, only the setting it registered for.
    if (msg == WM_POWERBROADCAST && wp == PBT_POWERSETTINGCHANGE && lp != 0 && g_source) {
        const auto* s = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
        if (IsEqualGUID(s->PowerSetting, kAcDcPowerSource)) g_source->Post(WakeReason::Power);
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Runs on a system thread: it only signals the event, the loop thread does the reading.
DWORD CALLBACK DeviceCallback(HCMNOTIFICATION, PVOID ctx, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA, DWORD) {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
        SetEvent(static_cast<HANDLE>(ctx));
    }
    return ERROR_SUCCESS;
}

constexpr wchar_t kWindowClass[] = L"VegaFlowPowerSink";

} // namespace

WindowsEventSource::WindowsEventSource(HANDLE stop_event) : stop_event_(stop_event) {
    g_source = this;
    device_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    foreground_hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundProc, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!foreground_hook_) {
        std::cerr << "[WARN] SetWinEventHook failed (Error: " << GetLastError() << ")\n";
        Release();
        return;
    }

    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = kDisplayAdapterInterface;
    HCMNOTIFICATION cm = nullptr;
    const CONFIGRET cr = device_event_ ? CM_Register_Notification(&filter, device_event_, DeviceCallback, &cm) : CR_FAILURE;
    if (cr != CR_SUCCESS) {
        std::cerr << "[WARN] CM_Register_Notification failed (CONFIGRET: " << cr << ")\n";
        Release();
        return;
    }
    cm_notify_ = cm;

    WNDCLASSW wc{};
    wc.lpfnWndProc = PowerWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);  // fails harmlessly if already registered
    window_ = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (window_) power_notify_ = RegisterPowerSettingNotification(window_, &kAcDcPowerSource, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!power_notify_) {
        std::cerr << "[WARN] Power notification registration failed (Error: " << GetLastError() << ")\n";
        Release();
        return;
    }
    active_ = true;
}

WindowsEventSource::~WindowsEventSource() { Release(); }

void WindowsEventSource::Release() {
    active_ = false;
    if (power_notify_) { UnregisterPowerSettingNotification(power_notify_); power_notify_ = nullptr; }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
    if (cm_notify_) { CM_Unregister_Notification(static_cast<HCMNOTIFICATION>(cm_notify_)); cm_notify_ = nullptr; }
    if (foreground_hook_) { UnhookWinEvent(foreground_hook_); foreground_hook_ = nullptr; }
    if (device_event_) { CloseHandle(device_event_); device_event_ = nullptr; }
    if (g_source == this) g_source = nullptr;
}

void WindowsEventSource::Post(WakeReason r) { pending_.fetch_or(Bit(r)); }

bool WindowsEventSource::TakePending(WakeReason& out) {
    for (WakeReason r : {WakeReason::Device, WakeReason::Power, WakeReason::Foreground}) {
        if (pending_.fetch_and(~Bit(r)) & Bit(r)) { out = r; return true; }
    }
    return false;
}

void WindowsEventSource::Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {  // drain the whole queue before waiting again
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

WakeReason WindowsEventSource::WaitForWake(uint32_t timeout_ms) {
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        WakeReason r;
        if (TakePending(r)) return r;
        const ULONGLONG now = GetTickCount64();
        const DWORD left = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
        const HANDLE handles[2] = {stop_event_, device_event_};
        const DWORD count = device_event_ ? 2 : 1;
        const DWORD res = MsgWaitForMultipleObjectsEx(count, handles, left, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (res == WAIT_OBJECT_0) return WakeReason::Stop;
        if (res == WAIT_OBJECT_0 + 1 && count == 2) return WakeReason::Device;
        if (res == WAIT_OBJECT_0 + count) { Pump(); continue; }
        if (res == WAIT_TIMEOUT) return WakeReason::Timer;
        return WakeReason::Stop;  // WAIT_FAILED: leave the loop and clean up rather than spin
    }
}

} // namespace vegaflow
