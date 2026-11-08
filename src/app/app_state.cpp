#include "app/app_state.hpp"

namespace vegaflow::app {

std::atomic<bool> g_running{true};
HANDLE g_stop_event = nullptr;
HANDLE g_single_instance_mutex = nullptr;

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false);
            if (g_stop_event) {
                SetEvent(g_stop_event);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

} // namespace vegaflow::app
