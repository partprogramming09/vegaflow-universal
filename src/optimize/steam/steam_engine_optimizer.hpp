#pragma once

#include "model/hardware.hpp"
#include "model/engine_presets.hpp"
#include "model/steam.hpp"
#include "ports/file_system.hpp"
#include "ports/registry.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

// Steam library scan (static, read-only) and engine presets. The methods that change game files are members and go
// through the FileSystem port given to the constructor (the real one is windows/SafeFileSystem, i.e. SandboxGuard).
class SteamEngineOptimizer {
public:
    explicit SteamEngineOptimizer(FileSystem& fs, RegistryPort* reg = nullptr)
        : fs_(fs), reg_(reg) {}

    // Reads HKCU\Software\Valve\Steam -> RunningAppID (0 if no Steam game is running)
    static uint32_t GetRunningSteamAppId();

    // Gets the Steam install path from HKCU\Software\Valve\Steam (SteamPath) or HKLM\SOFTWARE\WOW6432Node\Valve\Steam
    static std::wstring GetSteamInstallPath();

    // Scans libraryfolders.vdf and every appmanifest_*.acf to discover installed games, their real executable, engine and anti-cheat
    static std::vector<SteamGameEntry> ScanSteamLibrary();

    // Inspects an arbitrary game install folder to determine its main executable, graphics engine and Anti-Cheat presence
    static SteamGameEntry InspectGameDirectory(
        const std::wstring& install_dir,
        uint32_t app_id = 0,
        const std::string& name = "",
        RegistryPort* reg = nullptr);

    // Generates the UMA configuration block for Engine.ini according to the hardware policy and VRAM pool
    static std::string GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override = 0);

    // The boot.config keys a Unity profile writes (the one list: the writer and the Juegos screen both read it).
    static const std::vector<std::pair<std::string, std::string>>& UnityBootConfigKeys();

    // Applies the hidden engine preset for APU UMA (and DXGI Flip Model).
    // If has_anticheat == true, blocks editing of engine files and only applies the safe DXGI Flip Model.
    // Always creates a .vegaflow.bak backup before modifying Engine.ini or boot.config.
    EngineOptimizationReport ApplyEngineUmaPreset(
        const SteamGameEntry& game,
        const ApuOptimizationPolicy& policy,
        uint32_t vram_pool_mb = 4096,
        bool dry_run = false,
        const std::wstring& custom_config_dir_override = L"");

    // Restores the original engine configuration from the .vegaflow.bak file
    EngineOptimizationReport RestoreEngineOriginalConfig(
        const SteamGameEntry& game,
        bool dry_run = false,
        const std::wstring& custom_config_dir_override = L"");

    // Injects or updates shadow settings optimized for the Vega 6 APU in [SystemSettings] of iniPath.
    // Validates the path via the FileSystem port (CanModifyGameDir) and blocks if kernel-level Anti-Cheat is detected.
    bool ApplyApuShadowConfig(const std::filesystem::path& iniPath, const ApuShadowConfig& config, bool dryRun = false);

    // Restores the shadow settings from the .bak backup or removes the injected keys.
    bool RestoreApuShadowConfig(const std::filesystem::path& iniPath, bool dryRun = false);

    // Parses [SystemSettings] of iniPath and extracts the current values of the 5 optimized shadow variables.
    bool QueryApuShadowConfig(const std::filesystem::path& iniPath, ApuShadowConfig& currentConfig);

    static const char* EngineTypeToString(GameEngineType type);

private:
    FileSystem& fs_;
    RegistryPort* reg_ = nullptr;
};

} // namespace vegaflow
