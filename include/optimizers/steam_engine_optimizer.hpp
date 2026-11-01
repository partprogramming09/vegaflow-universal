#pragma once

#include "hardware_profiler.hpp"

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
#include <vector>

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
    float radius_threshold = 0.04f;     // r.Shadow.RadiusThreshold (poda sombras microscópicas lejanas)
    bool half_res_ao = true;            // r.AOHalfRes
    uint32_t volumetric_fog = 0;        // r.VolumetricFog (0 = desactivado, 1 = activo)
};

class SteamEngineOptimizer {
public:
    // Lee HKCU\Software\Valve\Steam -> RunningAppID (0 si no hay ningún juego de Steam en ejecución)
    static uint32_t GetRunningSteamAppId();

    // Obtiene la ruta de instalación de Steam desde HKCU\Software\Valve\Steam (SteamPath) o HKLM\SOFTWARE\WOW6432Node\Valve\Steam
    static std::wstring GetSteamInstallPath();

    // Escanea libraryfolders.vdf y todos los appmanifest_*.acf para descubrir juegos instalados, su ejecutable real, motor y anti-cheat
    static std::vector<SteamGameEntry> ScanSteamLibrary();

    // Inspecciona una carpeta de instalación de juego arbitraria para determinar su ejecutable principal, motor gráfico y presencia de Anti-Cheat
    static SteamGameEntry InspectGameDirectory(const std::wstring& install_dir, uint32_t app_id = 0, const std::string& name = "");

    // Genera el bloque de configuración UMA para Engine.ini según la política de hardware y VRAM pool
    static std::string GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override = 0);

    // Aplica el preset oculto del motor para APU UMA (y DXGI Flip Model).
    // Si has_anticheat == true, bloquea la edición de archivos del motor y solo aplica DXGI Flip Model seguro.
    // Crea siempre copia de seguridad .vegaflow.bak antes de modificar Engine.ini o boot.config.
    static EngineOptimizationReport ApplyEngineUmaPreset(
        const SteamGameEntry& game,
        uint32_t vram_pool_mb = 4096,
        bool dry_run = false,
        const std::wstring& custom_config_dir_override = L"");

    // Restaura la configuración original del motor a partir del archivo .vegaflow.bak
    static EngineOptimizationReport RestoreEngineOriginalConfig(
        const SteamGameEntry& game,
        bool dry_run = false,
        const std::wstring& custom_config_dir_override = L"");

    // Inyecta o actualiza configuración de sombras optimizada para el APU Vega 6 en [SystemSettings] de iniPath.
    // Valida la ruta mediante SandboxGuard::CanModifyGamePath y bloquea si detecta Anti-Cheat a nivel kernel.
    static bool ApplyApuShadowConfig(const std::filesystem::path& iniPath, const ApuShadowConfig& config, bool dryRun = false);

    // Restaura la configuración de sombras a partir del respaldo .bak o elimina las claves inyectadas.
    static bool RestoreApuShadowConfig(const std::filesystem::path& iniPath, bool dryRun = false);

    // Parsea [SystemSettings] de iniPath y extrae los valores actuales de las 5 variables de sombras optimizadas.
    static bool QueryApuShadowConfig(const std::filesystem::path& iniPath, ApuShadowConfig& currentConfig);

    static const char* EngineTypeToString(GameEngineType type);
};

} // namespace vegaflow
