#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"

namespace vegaflow {

using namespace steam_internal;

namespace {

bool ContainsInsensitiveW(std::wstring_view haystack, std::wstring_view needle) {
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](wchar_t a, wchar_t b) {
            return std::towlower(a) == std::towlower(b);
        }
    );
    return it != haystack.end();
}

} // namespace

SteamGameEntry SteamEngineOptimizer::InspectGameDirectory(
    const std::wstring& install_dir,
    uint32_t app_id,
    const std::string& name) {

    SteamGameEntry entry{};
    entry.app_id = app_id;
    entry.name = name;
    entry.install_dir = install_dir;

    std::error_code ec;
    if (!std::filesystem::exists(install_dir, ec)) {
        return entry;
    }

    bool has_anticheat = false;
    bool has_unreal_clues = false;
    bool has_unity_clues = false;
    bool has_source_clues = false;

    std::wstring unreal_shipping_exe;
    std::wstring largest_exe_path;
    uintmax_t largest_exe_size = 0;
    uint32_t exe_count = 0;

    for (auto it = std::filesystem::recursive_directory_iterator(
             install_dir, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator();) {
        if (ec) break;
        if (it.depth() > 6) {
            it.pop(ec);
            continue;
        }

        const auto& path = it->path();
        std::wstring itemPathStr = path.wstring();
        std::wstring filename = path.filename().wstring();

        // 1. Anti-Cheat Check
        if (!has_anticheat) {
            if (ContainsInsensitiveW(itemPathStr, L"easyanticheat") ||
                ContainsInsensitiveW(itemPathStr, L"battleye") ||
                ContainsInsensitiveW(itemPathStr, L"vanguard") ||
                ContainsInsensitiveW(itemPathStr, L"vgk.sys") ||
                ContainsInsensitiveW(itemPathStr, L"randgrid.sys") ||
                ContainsInsensitiveW(itemPathStr, L"anticheatexpert") ||
                ContainsInsensitiveW(itemPathStr, L"hoyokprotect") ||
                ContainsInsensitiveW(itemPathStr, L"mhyprot") ||
                ContainsInsensitiveW(itemPathStr, L"mhypbase")) {
                has_anticheat = true;
            }
        }

        // 2. Engine Detection Clues
        if (EndsWithInsensitiveW(filename, L"-win64-shipping.exe") ||
            EndsWithInsensitiveW(filename, L"-wingdk-shipping.exe") ||
            ContainsInsensitiveW(itemPathStr, L"engine\\binaries") ||
            ContainsInsensitiveW(itemPathStr, L"engine/binaries") ||
            (EndsWithInsensitiveW(filename, L".pak") &&
             (ContainsInsensitiveW(itemPathStr, L"content\\paks") || ContainsInsensitiveW(itemPathStr, L"content/paks")))) {
            has_unreal_clues = true;
        }

        if (EqualsInsensitiveW(filename, L"unityplayer.dll") ||
            ContainsInsensitiveW(itemPathStr, L"managed\\unityengine.dll") ||
            ContainsInsensitiveW(itemPathStr, L"managed/unityengine.dll") ||
            (EqualsInsensitiveW(filename, L"boot.config") &&
             EndsWithInsensitiveW(path.parent_path().filename().wstring(), L"_data"))) {
            has_unity_clues = true;
        }

        if (EqualsInsensitiveW(filename, L"engine2.dll") ||
            ContainsInsensitiveW(itemPathStr, L"source2") ||
            EqualsInsensitiveW(filename, L"cs2.exe") ||
            EqualsInsensitiveW(filename, L"dota2.exe") ||
            EqualsInsensitiveW(filename, L"hl2.exe") ||
            EqualsInsensitiveW(filename, L"portal2.exe") ||
            EqualsInsensitiveW(filename, L"left4dead2.exe")) {
            has_source_clues = true;
        }

        // 3. Executable Inspection
        if (it->is_regular_file(ec) && EndsWithInsensitiveW(filename, L".exe")) {
            bool is_auxiliary =
                ContainsInsensitiveW(filename, L"crash") ||
                ContainsInsensitiveW(filename, L"unins") ||
                ContainsInsensitiveW(filename, L"redist") ||
                ContainsInsensitiveW(filename, L"dxsetup") ||
                ContainsInsensitiveW(filename, L"vcredist") ||
                ContainsInsensitiveW(filename, L"cefsubprocess") ||
                ContainsInsensitiveW(filename, L"easyanticheat") ||
                ContainsInsensitiveW(filename, L"beservice") ||
                ContainsInsensitiveW(filename, L"launcher_helper") ||
                ContainsInsensitiveW(filename, L"unitycrashhandler");

            if (!is_auxiliary) {
                exe_count++;
                if (EndsWithInsensitiveW(filename, L"-win64-shipping.exe") ||
                    EndsWithInsensitiveW(filename, L"-wingdk-shipping.exe")) {
                    unreal_shipping_exe = itemPathStr;
                }

                uintmax_t size = std::filesystem::file_size(path, ec);
                if (!ec && size > largest_exe_size) {
                    largest_exe_size = size;
                    largest_exe_path = itemPathStr;
                }
            }
        }

        it.increment(ec);
    }

    entry.has_anticheat = has_anticheat;

    if (has_unreal_clues) {
        entry.engine = GameEngineType::UnrealEngine4_5;
    } else if (has_unity_clues) {
        entry.engine = GameEngineType::Unity;
    } else if (has_source_clues) {
        entry.engine = GameEngineType::Source1_2;
    } else if (exe_count > 0) {
        entry.engine = GameEngineType::NativeDirectX_Vulkan;
    } else {
        entry.engine = GameEngineType::Unknown;
    }

    if (entry.engine == GameEngineType::UnrealEngine4_5 && !unreal_shipping_exe.empty()) {
        entry.primary_exe_path = unreal_shipping_exe;
    } else {
        entry.primary_exe_path = largest_exe_path;
    }

    entry.flip_model_applied = !entry.primary_exe_path.empty() &&
        GraphicsOptimizer::HasDxgiFlipModelUpgrade(entry.primary_exe_path);

    entry.dxvk_installed = !entry.primary_exe_path.empty() &&
        DxvkManager::IsDxvkInstalled(entry.primary_exe_path);

    if (entry.has_anticheat ||
        entry.engine == GameEngineType::Source1_2 ||
        entry.engine == GameEngineType::NativeDirectX_Vulkan) {
        entry.engine_preset_applied = entry.flip_model_applied;
    } else if (entry.engine == GameEngineType::UnrealEngine4_5) {
        auto iniPaths = ResolveAllUnrealEngineIniPaths(entry, L"");
        for (const auto& iniPath : iniPaths) {
            std::filesystem::path pIni(iniPath);
            std::filesystem::path pBak(iniPath + L".vegaflow.bak");
            if (std::filesystem::exists(pBak, ec)) {
                entry.engine_preset_applied = true;
                break;
            } else if (std::filesystem::exists(pIni, ec)) {
                std::string content = ReadFileToString(pIni);
                if (content.find("; [VegaFlowUMA_Start]") != std::string::npos) {
                    entry.engine_preset_applied = true;
                    break;
                }
            }
        }
    } else if (entry.engine == GameEngineType::Unity) {
        std::wstring bootPath = ResolveUnityBootConfigPath(entry, L"");
        if (!bootPath.empty()) {
            std::filesystem::path pBoot(bootPath);
            std::filesystem::path pBak(bootPath + L".vegaflow.bak");
            if (std::filesystem::exists(pBak, ec)) {
                entry.engine_preset_applied = true;
            } else if (std::filesystem::exists(pBoot, ec)) {
                std::string content = ReadFileToString(pBoot);
                entry.engine_preset_applied = (content.find("gfx-enable-native-gfx-jobs=1") != std::string::npos);
            }
        }
    }

    return entry;
}

const char* SteamEngineOptimizer::EngineTypeToString(GameEngineType type) {
    switch (type) {
        case GameEngineType::UnrealEngine4_5:
            return "Unreal Engine 4/5";
        case GameEngineType::Unity:
            return "Unity";
        case GameEngineType::Source1_2:
            return "Source 1/2";
        case GameEngineType::NativeDirectX_Vulkan:
            return "Native DirectX/Vulkan";
        default:
            return "Unknown";
    }
}

} // namespace vegaflow
