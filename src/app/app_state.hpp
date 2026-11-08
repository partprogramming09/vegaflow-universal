#pragma once
// Process-wide state shared by the daemon loop, the console handler and the monitor dashboard.

#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace vegaflow::app {

extern std::atomic<bool> g_running;
extern HANDLE g_stop_event;
extern HANDLE g_single_instance_mutex;

// Console control handler: Ctrl+C, close, logoff and shutdown stop the daemon cleanly.
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type);

} // namespace vegaflow::app
