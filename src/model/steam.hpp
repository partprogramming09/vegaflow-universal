#pragma once
// Plain data about Steam games and engine presets (optimize/SteamEngineOptimizer fills it, the screens read it).

#include <cstdint>
#include <string>

namespace vegaflow {

enum class GameEngineType {
    Unknown = 0,
    UnrealEngine4_5,
    Unity,
    Source1_2,
    NativeDirectX_Vulkan
};

struct SteamGameEntry {
    uint32_t app_id = 0;
    std::string name;
    std::wstring install_dir;
    std::wstring primary_exe_path;
    GameEngineType engine = GameEngineType::Unknown;
    bool has_anticheat = false;
    bool flip_model_applied = false;
    bool engine_preset_applied = false;
    bool dxvk_installed = false;
};

struct EngineOptimizationReport {
    bool success = false;
    bool blocked_by_anticheat = false;
    bool backup_created = false;
    std::wstring config_modified_path;
    std::string status_message;
};

struct ApuShadowConfig {
    uint32_t max_cascades = 2;          // r.Shadow.CSM.MaxCascades
    float distance_scale = 0.7f;        // r.Shadow.DistanceScale
    float radius_threshold = 0.04f;     // r.Shadow.RadiusThreshold (prunes distant microscopic shadows)
    bool half_res_ao = true;            // r.AOHalfRes
    uint32_t volumetric_fog = 0;        // r.VolumetricFog (0 = disabled, 1 = active)
};

inline const char* EngineTypeToString(GameEngineType type) {
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
