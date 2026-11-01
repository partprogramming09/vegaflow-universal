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
    // Detecta si la ventana en primer plano (o algún proceso de una lista objetivo opcional) es un juego con DX9/10/11/12/Vulkan
    static std::optional<DetectedGameProcess> DetectActiveGame(const std::vector<std::wstring>& custom_game_exes = {});

    // Inspecciona un PID específico de forma 100% segura frente a Anti-Cheat (solo PROCESS_QUERY_LIMITED_INFORMATION y TH32CS_SNAPMODULE)
    static std::optional<DetectedGameProcess> InspectProcess(uint32_t pid, HWND hwnd_hint = nullptr);

    // Devuelve los PIDs de procesos ruidosos en segundo plano (navegadores, Discord, launchers) excluyendo el PID del juego activo
    static std::vector<uint32_t> FindBackgroundNoisyProcesses(uint32_t active_game_pid = 0);

    // Comprueba si un proceso sigue activo mediante PROCESS_QUERY_LIMITED_INFORMATION y GetExitCodeProcess
    static bool IsProcessAlive(uint32_t pid);

    static const char* RenderApiToString(RenderApi api);
    static bool IsSystemOrExcludedProcess(const std::wstring& exe_name);
};

} // namespace vegaflow
