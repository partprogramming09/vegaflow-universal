#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "model/hardware.hpp"
#include "ports/file_system.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace vegaflow {

// Installs and removes DXVK in a game folder. Writes, copies and removals go through the FileSystem port it is given
// (the real one is windows/SafeFileSystem, i.e. SandboxGuard). Not yet: reading dxvk.conf (ifstream), exists/
// create_directories, and the fallback std::filesystem::remove in RemoveApuProfile; they move to the port in phase 1.
class DxvkManager {
public:
    explicit DxvkManager(FileSystem& fs) : fs_(fs) {}

    // Checks whether d3d11.dll and dxgi.dll exist in the directory of game_exe_path
    static bool IsDxvkInstalled(const std::wstring& game_exe_path);

    // Deploys DXVK (DirectX 11 to Vulkan with asynchronous GPL compilation)
    // If has_anticheat == true, refuses the installation to protect the user's account.
    bool DeployDxvk(
        const std::wstring& game_exe_path,
        const ApuOptimizationPolicy& policy,
        bool has_anticheat = false,
        bool is_64bit = true,
        bool dry_run = false,
        std::string* out_message = nullptr);

    // Removes DXVK and restores the original .original.bak files if they existed
    bool RemoveDxvk(
        const std::wstring& game_exe_path,
        bool dry_run = false,
        std::string* out_message = nullptr);

    // Returns the existing absolute path of assets/dxvk/dxvk-2.5.3
    static std::wstring GetDxvkAssetsPath();

    // Generates or updates dxvk.conf with parameters calibrated for APU Zen 2 / Vega 6
    bool ConfigureApuProfile(const std::filesystem::path& targetDir, const ApuOptimizationPolicy& policy, bool dryRun = false);

    // Reads and validates the contents of dxvk.conf in targetDir
    bool ValidateDxvkApuProfile(const std::filesystem::path& targetDir, bool& asyncEnabled, uint32_t& compilerThreads);

    // Cleanly deletes dxvk.conf in targetDir
    bool RemoveApuProfile(const std::filesystem::path& targetDir, bool dryRun = false);

private:
    FileSystem& fs_;
};

} // namespace vegaflow
