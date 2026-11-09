#pragma once

// Types that describe a detected game, shared by detect/ and session/ (no logic here).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // HWND in DetectedGameProcess (to be replaced by a neutral type in its own phase)

#include <cstdint>
#include <string>

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

} // namespace vegaflow
