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

    // Starts the spatial scaler, capturing the game window and projecting a full-screen click-through overlay with FSR 1.0 (EASU + RCAS)
    // If dry_run == true, validates dimensions, compiles the EASU and RCAS HLSL shaders in memory and updates the state without opening the window/overlay loop.
    bool StartScaling(HWND target_game_hwnd, float sharpness = 0.2f, bool dry_run = false);

    // Stops the scaler and destroys 100% of the D3D11/DXGI resources and the overlay window (restoring consumption to 0 MB of VRAM)
    void StopScaling();

    // Toggles between on and off
    bool ToggleScaling(HWND target_game_hwnd, float sharpness = 0.2f, bool dry_run = false);

    // Updates RCAS sharpness (0.0f = maximum sharpness, 1.0f = soft) live without recreating the D3D11 device
    void SetSharpness(float sharpness);

    // Switches the HLSL upscaling algorithm live (FSR 1.0 Ultra-Clean, NIS Bicubic 16-Tap or Anime4K)
    void SetAlgorithm(UpscaleAlgorithm algo);
    UpscaleAlgorithm GetAlgorithm() const;
    static const char* AlgorithmToString(UpscaleAlgorithm algo);

    // Returns the current state in a thread-safe way
    FsrScalerStatus GetStatus() const;

    // Compiles and validates only the EASU (Pass 1) and RCAS (Pass 2) HLSL shaders with D3DCompile (for automated tests without needing an open game)
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
