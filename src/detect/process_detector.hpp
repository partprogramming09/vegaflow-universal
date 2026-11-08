#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vegaflow {

enum class RenderApi {
    Unknown = 0,
    DirectX9_10,
    DirectX11,
    DirectX12,
    Vulkan
};

struct DetectedGameProcess {
    uint32_t pid = 0;
    std::wstring exe_name;
    std::wstring full_path;
    RenderApi api = RenderApi::Unknown;
    std::wstring driver_dll_loaded; // ej. atidxx64.dll, amdxc64.dll, nvwgf2umx.dll, igd10iumd64.dll, igd12umd64.dll
    HWND main_window = nullptr;
    uint32_t presenter_thread_id = 0;
};

class ProcessDetector {
public:
    // Detects whether the foreground window (or any process in an optional target list) is a DX9/10/11/12/Vulkan game
    static std::optional<DetectedGameProcess> DetectActiveGame(const std::vector<std::wstring>& custom_game_exes = {});

    // Inspects a specific PID 100% safely with respect to Anti-Cheat (only PROCESS_QUERY_LIMITED_INFORMATION and TH32CS_SNAPMODULE)
    static std::optional<DetectedGameProcess> InspectProcess(uint32_t pid, HWND hwnd_hint = nullptr);

    // Returns the PIDs of noisy background processes (browsers, Discord, launchers), excluding the active game's PID
    static std::vector<uint32_t> FindBackgroundNoisyProcesses(uint32_t active_game_pid = 0);

    // Checks whether a process is still alive using PROCESS_QUERY_LIMITED_INFORMATION and GetExitCodeProcess
    static bool IsProcessAlive(uint32_t pid);

    static const char* RenderApiToString(RenderApi api);
    static bool IsSystemOrExcludedProcess(const std::wstring& exe_name);
};

} // namespace vegaflow
