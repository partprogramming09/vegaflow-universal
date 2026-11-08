#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vegaflow {

using namespace steam_internal;

namespace {

bool EqualsInsensitive(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(
        a.begin(), a.end(),
        b.begin(), b.end(),
        [](char ca, char cb) {
            return std::tolower(static_cast<unsigned char>(ca)) ==
                   std::tolower(static_cast<unsigned char>(cb));
        }
    );
}

std::string_view TrimWhitespace(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string FormatFloatTrimmed(float val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", val);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s.empty() ? "0" : s;
}

} // namespace


bool SteamEngineOptimizer::ApplyApuShadowConfig(
    const std::filesystem::path& iniPath,
    const ApuShadowConfig& config,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    // 1. Validate the path via SandboxGuard (kernel-level anti-cheat safety)
    if (!SandboxGuard::CanModifyGamePath(targetDir)) {
        return false;
    }
    if (SandboxGuard::PathContainsAntiCheatSignature(iniPath.wstring())) {
        return false;
    }

    auto val = SandboxGuard::ValidatePath(iniPath.wstring(), SandboxAction::Write, false);
    if (!val.allowed) {
        return false;
    }

    // 2. Validate configuration parameters
    if (config.max_cascades > 16 || config.distance_scale < 0.0f ||
        config.radius_threshold < 0.0f || config.volumetric_fog > 1) {
        return false;
    }

    // If dryRun == true, validates permissions and syntax without touching disk
    if (dryRun) {
        return true;
    }

    std::error_code ec;

    // 3. Create a .bak backup if it does not exist before modifying
    std::filesystem::path bakPath = iniPath.wstring() + L".bak";
    if (!std::filesystem::exists(bakPath, ec)) {
        if (std::filesystem::exists(iniPath, ec)) {
            if (!SandboxGuard::SafeCopyFile(iniPath.wstring(), bakPath.wstring(), true, false)) {
                return false;
            }
        } else {
            if (!SandboxGuard::SafeWriteFile(bakPath.wstring(), "; VegaFlow Empty Backup\n", false)) {
                return false;
            }
        }
    }

    // 4. Read existing content
    std::string existingContent;
    if (std::filesystem::exists(iniPath, ec)) {
        existingContent = ReadFileToString(iniPath);
    }

    struct ShadowEntry {
        std::string key;
        std::string val;
        bool applied = false;
    };

    std::vector<ShadowEntry> shadowKeys = {
        {"r.Shadow.CSM.MaxCascades", std::to_string(config.max_cascades), false},
        {"r.Shadow.DistanceScale", FormatFloatTrimmed(config.distance_scale), false},
        {"r.Shadow.RadiusThreshold", FormatFloatTrimmed(config.radius_threshold), false},
        {"r.AOHalfRes", config.half_res_ao ? "1" : "0", false},
        {"r.VolumetricFog", std::to_string(config.volumetric_fog), false}
    };

    std::stringstream in(existingContent);
    std::string rawLine;
    std::vector<std::string> lines;
    while (std::getline(in, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }
        lines.push_back(rawLine);
    }

    std::vector<std::string> outputLines;
    bool inSystemSettings = false;
    bool foundSystemSettings = false;

    for (const auto& line : lines) {
        std::string_view trimmed = TrimWhitespace(line);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
            if (inSystemSettings) {
                // On leaving [SystemSettings], inject any missing keys
                for (auto& entry : shadowKeys) {
                    if (!entry.applied) {
                        outputLines.push_back(entry.key + "=" + entry.val);
                        entry.applied = true;
                    }
                }
                inSystemSettings = false;
            }

            if (EqualsInsensitive(secName, "SystemSettings")) {
                inSystemSettings = true;
                foundSystemSettings = true;
            }
            outputLines.push_back(line);
            continue;
        }

        if (inSystemSettings) {
            size_t eqPos = trimmed.find('=');
            if (eqPos != std::string_view::npos) {
                std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                bool matched = false;
                for (auto& entry : shadowKeys) {
                    if (EqualsInsensitive(keyPart, entry.key)) {
                        outputLines.push_back(entry.key + "=" + entry.val);
                        entry.applied = true;
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    continue;
                }
            }
        }

        outputLines.push_back(line);
    }

    if (inSystemSettings) {
        for (auto& entry : shadowKeys) {
            if (!entry.applied) {
                outputLines.push_back(entry.key + "=" + entry.val);
                entry.applied = true;
            }
        }
    }

    if (!foundSystemSettings) {
        if (!outputLines.empty() && !outputLines.back().empty()) {
            outputLines.push_back("");
        }
        outputLines.push_back("[SystemSettings]");
        for (auto& entry : shadowKeys) {
            outputLines.push_back(entry.key + "=" + entry.val);
            entry.applied = true;
        }
    }

    std::ostringstream out;
    for (const auto& l : outputLines) {
        out << l << "\n";
    }

    return SandboxGuard::SafeWriteFile(iniPath.wstring(), out.str(), false);
}

bool SteamEngineOptimizer::RestoreApuShadowConfig(
    const std::filesystem::path& iniPath,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    // 1. Validate the path via SandboxGuard
    if (!SandboxGuard::CanModifyGamePath(targetDir)) {
        return false;
    }
    if (SandboxGuard::PathContainsAntiCheatSignature(iniPath.wstring())) {
        return false;
    }

    auto val = SandboxGuard::ValidatePath(iniPath.wstring(), SandboxAction::Write, false);
    if (!val.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path bakPath = iniPath.wstring() + L".bak";
    bool bakExists = std::filesystem::exists(bakPath, ec);
    bool iniExists = std::filesystem::exists(iniPath, ec);

    if (!bakExists && !iniExists) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    // 2. Restore from .bak if it exists
    if (bakExists) {
        std::string bakContent = ReadFileToString(bakPath);
        bool restoredOk = false;
        if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
            restoredOk = SandboxGuard::SafeRemoveFile(iniPath.wstring(), false);
        } else {
            restoredOk = SandboxGuard::SafeCopyFile(bakPath.wstring(), iniPath.wstring(), true, false);
        }
        if (restoredOk) {
            SandboxGuard::SafeRemoveFile(bakPath.wstring(), false);
            return true;
        }
        return false;
    }

    // 3. If there is no .bak file but iniPath exists, remove the injected keys from [SystemSettings]
    if (iniExists) {
        std::string content = ReadFileToString(iniPath);
        std::stringstream in(content);
        std::string rawLine;
        std::vector<std::string> lines;
        while (std::getline(in, rawLine)) {
            if (!rawLine.empty() && rawLine.back() == '\r') {
                rawLine.pop_back();
            }
            lines.push_back(rawLine);
        }

        const std::vector<std::string> targetKeys = {
            "r.Shadow.CSM.MaxCascades",
            "r.Shadow.DistanceScale",
            "r.Shadow.RadiusThreshold",
            "r.AOHalfRes",
            "r.VolumetricFog"
        };

        std::vector<std::string> outputLines;
        bool inSystemSettings = false;

        for (const auto& line : lines) {
            std::string_view trimmed = TrimWhitespace(line);
            if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
                std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
                inSystemSettings = EqualsInsensitive(secName, "SystemSettings");
                outputLines.push_back(line);
                continue;
            }

            if (inSystemSettings) {
                size_t eqPos = trimmed.find('=');
                if (eqPos != std::string_view::npos) {
                    std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                    bool matched = false;
                    for (const auto& k : targetKeys) {
                        if (EqualsInsensitive(keyPart, k)) {
                            matched = true;
                            break;
                        }
                    }
                    if (matched) {
                        continue;
                    }
                }
            }

            outputLines.push_back(line);
        }

        std::ostringstream out;
        for (const auto& l : outputLines) {
            out << l << "\n";
        }

        return SandboxGuard::SafeWriteFile(iniPath.wstring(), out.str(), false);
    }

    return false;
}

bool SteamEngineOptimizer::QueryApuShadowConfig(
    const std::filesystem::path& iniPath,
    ApuShadowConfig& currentConfig) {

    if (iniPath.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(iniPath, ec) || !std::filesystem::is_regular_file(iniPath, ec)) {
        return false;
    }

    std::string content;
    if (!SandboxGuard::SafeReadFile(iniPath.wstring(), content)) {
        return false;
    }

    std::stringstream in(content);
    std::string rawLine;
    bool inSystemSettings = false;
    uint32_t keysFound = 0;

    while (std::getline(in, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }
        std::string_view trimmed = TrimWhitespace(rawLine);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            std::string_view secName = trimmed.substr(1, trimmed.size() - 2);
            inSystemSettings = EqualsInsensitive(secName, "SystemSettings");
            continue;
        }

        if (inSystemSettings) {
            size_t eqPos = trimmed.find('=');
            if (eqPos != std::string_view::npos) {
                std::string_view keyPart = TrimWhitespace(trimmed.substr(0, eqPos));
                std::string_view valPart = TrimWhitespace(trimmed.substr(eqPos + 1));
                std::string valStr(valPart);

                if (EqualsInsensitive(keyPart, "r.Shadow.CSM.MaxCascades")) {
                    try {
                        currentConfig.max_cascades = static_cast<uint32_t>(std::stoul(valStr));
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.Shadow.DistanceScale")) {
                    try {
                        currentConfig.distance_scale = std::stof(valStr);
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.Shadow.RadiusThreshold")) {
                    try {
                        currentConfig.radius_threshold = std::stof(valStr);
                        keysFound++;
                    } catch (...) {}
                } else if (EqualsInsensitive(keyPart, "r.AOHalfRes")) {
                    currentConfig.half_res_ao = (valStr == "1" || EqualsInsensitive(valStr, "true"));
                    keysFound++;
                } else if (EqualsInsensitive(keyPart, "r.VolumetricFog")) {
                    try {
                        currentConfig.volumetric_fog = static_cast<uint32_t>(std::stoul(valStr));
                        keysFound++;
                    } catch (...) {}
                }
            }
        }
    }

    return (keysFound > 0);
}

} // namespace vegaflow
