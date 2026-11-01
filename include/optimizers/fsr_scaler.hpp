#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace vegaflow {

enum class UpscaleAlgorithm : int {
    FSR1_UltraClean = 0,
    NIS_HybridCatmull = 1,
    Anime4K_LineReconstruct = 2
};

struct FsrScalerStatus {
    bool active = false;
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    float sharpness = 0.2f;
    UpscaleAlgorithm algorithm = UpscaleAlgorithm::FSR1_UltraClean;
    double last_pass_time_ms = 0.0;
    uint64_t frames_upscaled = 0;
    std::string status_message = "Inactive (0 MB VRAM)";
};

class FsrSpatialScaler {
public:
    FsrSpatialScaler();
    ~FsrSpatialScaler();

    FsrSpatialScaler(const FsrSpatialScaler&) = delete;
    FsrSpatialScaler& operator=(const FsrSpatialScaler&) = delete;

    // Inicia el escalador espacial capturando la ventana del juego y proyectando un overlay click-through a pantalla completa con FSR 1.0 (EASU + RCAS)
    // Si dry_run == true, valida dimensiones, compila los shaders HLSL de EASU y RCAS en memoria y actualiza el estado sin abrir el bucle de ventana/overlay.
    bool StartScaling(HWND target_game_hwnd, float sharpness = 0.2f, bool dry_run = false);

    // Detiene el escalador y destruye el 100% de los recursos D3D11/DXGI y la ventana overlay (restaurando el consumo a 0 MB de VRAM)
    void StopScaling();

    // Alterna entre encendido y apagado
    bool ToggleScaling(HWND target_game_hwnd, float sharpness = 0.2f, bool dry_run = false);

    // Actualiza la nitidez RCAS (0.0f = máxima nitidez, 1.0f = suave) en vivo sin recrear el dispositivo D3D11
    void SetSharpness(float sharpness);

    // Cambia el algoritmo de reescalado HLSL en vivo (FSR 1.0 Ultra-Clean, NIS Bicúbico 16-Tap o Anime4K)
    void SetAlgorithm(UpscaleAlgorithm algo);
    UpscaleAlgorithm GetAlgorithm() const;
    static const char* AlgorithmToString(UpscaleAlgorithm algo);

    // Devuelve el estado actual de forma thread-safe
    FsrScalerStatus GetStatus() const;

    // Compila y valida únicamente los shaders HLSL de EASU (Pass 1) y RCAS (Pass 2) con D3DCompile (para pruebas automatizadas sin requerir juego abierto)
    static bool CompileAndValidateShadersOnly(std::string* out_error = nullptr);

private:
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::atomic<float> sharpness_{0.2f};
    std::atomic<int> algorithm_{0};
    std::thread render_thread_;
    FsrScalerStatus status_{};

    void RenderLoop(HWND target_hwnd, float sharpness);
};

} // namespace vegaflow
