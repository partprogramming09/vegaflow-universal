#pragma once

// Types that describe a detected game, shared by detect/ and session/ (no logic here).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // HWND in DetectedGameProcess (to be replaced by a neutral type in its own phase)

#include <cstdint>
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

enum class ProcessClass : uint8_t {
    Unknown = 0,
    WindowsSystem = 1,
    RecordingStreaming = 2,
    CallsVoice = 3,
    Messaging = 4,
    Browsers = 5,
    StoresLaunchers = 6,
    Music = 7,
    CloudSync = 8,
    DeveloperIde = 9,
    HeavyWork = 10,
    PossibleGame = 11
};

enum class ProcessAction : uint8_t {
    DoNotTouch = 0,
    EcoOnly = 1,
    Throttle = 2,
    BoostAsGame = 3
};

struct ProcessCategoryActions {
    ProcessAction recording_streaming = ProcessAction::DoNotTouch;
    ProcessAction calls_voice = ProcessAction::DoNotTouch;
    ProcessAction messaging = ProcessAction::Throttle;
    ProcessAction browsers = ProcessAction::Throttle;
    ProcessAction stores_launchers = ProcessAction::Throttle;
    ProcessAction music = ProcessAction::EcoOnly;
    ProcessAction cloud_sync = ProcessAction::Throttle;
};

struct ProcessExceptionEntry {
    std::wstring exe_name;
    ProcessAction action = ProcessAction::DoNotTouch;
};

struct ProcessPolicyConfig {
    ProcessCategoryActions category_actions{};
    bool heavy_work_as_game = false;
    std::vector<ProcessExceptionEntry> exceptions;
    std::vector<std::wstring> custom_game_exes;
    std::vector<std::wstring> excluded_game_exes;
};

struct BackgroundProcessTarget {
    uint32_t pid = 0;
    uint64_t creation_time = 0;
    std::wstring exe_name;
    std::wstring full_path;
    ProcessClass process_class = ProcessClass::Unknown;
    ProcessAction action = ProcessAction::Throttle;
};

struct DetectedGameProcess {
    uint32_t pid = 0;
    uint64_t creation_time = 0;
    std::wstring exe_name;
    std::wstring full_path;
    RenderApi api = RenderApi::Unknown;
    std::wstring driver_dll_loaded; // ej. atidxx64.dll, amdxc64.dll, nvwgf2umx.dll, igd10iumd64.dll, igd12umd64.dll
    HWND main_window = nullptr;
    uint32_t presenter_thread_id = 0;
    bool generic_anticheat_detected = false;
};

} // namespace vegaflow
