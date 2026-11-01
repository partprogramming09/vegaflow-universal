#pragma once

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

namespace vegaflow {

class DxvkManager {
public:
    // Comprueba si existen d3d11.dll y dxgi.dll en el directorio de game_exe_path
    static bool IsDxvkInstalled(const std::wstring& game_exe_path);

    // Despliega DXVK (DirectX 11 to Vulkan con compilación asíncrona GPL)
    // Si has_anticheat == true, rechaza la instalación para proteger la cuenta del usuario.
    static bool DeployDxvk(
        const std::wstring& game_exe_path,
        bool has_anticheat = false,
        bool is_64bit = true,
        bool dry_run = false,
        std::string* out_message = nullptr);

    // Remueve DXVK y restaura los archivos originales .original.bak si existían
    static bool RemoveDxvk(
        const std::wstring& game_exe_path,
        bool dry_run = false,
        std::string* out_message = nullptr);

    // Retorna la ruta absoluta existente de assets/dxvk/dxvk-2.5.3
    static std::wstring GetDxvkAssetsPath();

    // Genera o actualiza dxvk.conf con parámetros calibrados para APU Zen 2 / Vega 6
    static bool ConfigureApuProfile(const std::filesystem::path& targetDir, bool dryRun = false);

    // Lee y valida el contenido de dxvk.conf en targetDir
    static bool ValidateDxvkApuProfile(const std::filesystem::path& targetDir, bool& asyncEnabled, uint32_t& compilerThreads);

    // Elimina limpiamente dxvk.conf en targetDir
    static bool RemoveApuProfile(const std::filesystem::path& targetDir, bool dryRun = false);
};

} // namespace vegaflow
