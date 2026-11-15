#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "bridge/rust_bridge.h"
#include "model/game.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vegaflow {

class ProcessDetector {
public:
    // Pure policy bridge helper: delegates classification & action selection to Rust policy/process_class.rs.
    static VfProcessDecision ClassifyProcess(
        const std::wstring& full_path,
        const std::wstring& exe_name,
        const ProcessPolicyConfig& policy,
        bool is_foreground_3d,
        bool has_anticheat_signal);

    // Detects whether the foreground window (or Alt+Tab window / custom list) is a game eligible for boosting.
    static std::optional<DetectedGameProcess> DetectActiveGame(const ProcessPolicyConfig& policy);
    static std::optional<DetectedGameProcess> DetectActiveGame(const std::vector<std::wstring>& custom_game_exes = {});

    // Inspects a specific PID safely with respect to Anti-Cheat (only PROCESS_QUERY_LIMITED_INFORMATION and TH32CS_SNAPMODULE).
    static std::optional<DetectedGameProcess> InspectProcess(
        uint32_t pid,
        HWND hwnd_hint = nullptr,
        const ProcessPolicyConfig& policy = {});

    // Classifies background processes via Rust policy and returns only those whose action is EcoOnly or Throttle.
    static std::vector<BackgroundProcessTarget> ClassifyBackgroundProcesses(
        uint32_t active_game_pid = 0,
        const ProcessPolicyConfig& policy = {});

    // Compatibility helper returning PIDs of background processes with EcoOnly or Throttle action.
    static std::vector<uint32_t> FindBackgroundNoisyProcesses(
        uint32_t active_game_pid = 0,
        const ProcessPolicyConfig& policy = {});

    // Checks whether a process is still alive using PROCESS_QUERY_LIMITED_INFORMATION and GetExitCodeProcess.
    static bool IsProcessAlive(uint32_t pid);

    static const char* RenderApiToString(RenderApi api);
    static bool IsSystemOrExcludedProcess(const std::wstring& exe_name);
};

} // namespace vegaflow
