#include "optimize/fsr/fsr_scaler.hpp"
#include "optimize/fsr/fsr_internal.hpp"
#include "optimize/fsr/fsr_scaler.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace vegaflow {

using namespace fsr_internal;


// ============================================================================
// FsrSpatialScaler Implementation
// ============================================================================

FsrSpatialScaler::FsrSpatialScaler() {
    status_.algorithm = UpscaleAlgorithm::FSR1_UltraClean;
    status_.status_message = "Inactive (0 MB VRAM)";
}

FsrSpatialScaler::~FsrSpatialScaler() {
    StopScaling();
}

bool FsrSpatialScaler::CompileAndValidateShadersOnly(std::string* out_error) {
    ID3DBlob* vsBlob = nullptr;
    if (!CompileShaderHelper(kFullscreenVs, "VS_Fullscreen", "vs_5_0", &vsBlob, out_error)) {
        return false;
    }
    if (vsBlob) vsBlob->Release();

    ID3DBlob* easuBlob = nullptr;
    if (!CompileShaderHelper(kEasuPs, "PS_Easu", "ps_5_0", &easuBlob, out_error)) {
        return false;
    }
    if (easuBlob) easuBlob->Release();

    ID3DBlob* rcasBlob = nullptr;
    if (!CompileShaderHelper(kRcasPs, "PS_Rcas", "ps_5_0", &rcasBlob, out_error)) {
        return false;
    }
    if (rcasBlob) rcasBlob->Release();

    if (out_error) {
        out_error->clear();
    }
    return true;
}

bool FsrSpatialScaler::StartScaling(HWND target_game_hwnd, float sharpness, bool dry_run) {
    if (running_.load()) {
        StopScaling();
    }
    sharpness_.store(std::clamp(sharpness, 0.0f, 1.0f), std::memory_order_relaxed);

    if (dry_run) {
        std::string err;
        if (!CompileAndValidateShadersOnly(&err)) {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.active = false;
            status_.status_message = "Shader compilation failed: " + err;
            return false;
        }

        uint32_t sw = 1280;
        uint32_t sh = 720;
        if (target_game_hwnd != nullptr && IsWindow(target_game_hwnd)) {
            RECT rc{};
            if (GetClientRect(target_game_hwnd, &rc)) {
                int w = rc.right - rc.left;
                int h = rc.bottom - rc.top;
                if (w > 0 && h > 0) {
                    sw = static_cast<uint32_t>(w);
                    sh = static_cast<uint32_t>(h);
                }
            }
        }

        uint32_t ow = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
        uint32_t oh = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
        if (ow == 0 || oh == 0) {
            ow = 1920;
            oh = 1080;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.active = true;
            status_.source_width = sw;
            status_.source_height = sh;
            status_.output_width = ow;
            status_.output_height = oh;
            status_.sharpness = sharpness_.load(std::memory_order_relaxed);
            status_.algorithm = static_cast<UpscaleAlgorithm>(algorithm_.load(std::memory_order_relaxed));
            status_.last_pass_time_ms = 0.0;
            status_.frames_upscaled = 0;
            status_.status_message = "Dry-run: Multi-Algorithm EASU+RCAS shaders compiled and validated";
        }
        return true;
    }

    // Real execution
    if (!target_game_hwnd || !IsWindow(target_game_hwnd)) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Invalid target window HWND";
        return false;
    }

    std::string err;
    if (!CompileAndValidateShadersOnly(&err)) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Shader compilation failed: " + err;
        return false;
    }

    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = true;
        status_.sharpness = sharpness_.load(std::memory_order_relaxed);
        status_.algorithm = static_cast<UpscaleAlgorithm>(algorithm_.load(std::memory_order_relaxed));
        status_.status_message = "Active: Initializing Multi-Algorithm D3D11 overlay";
    }

    render_thread_ = std::thread(&FsrSpatialScaler::RenderLoop, this, target_game_hwnd, sharpness);
    return true;
}

void FsrSpatialScaler::StopScaling() {
    if (running_.load()) {
        running_.store(false);
    }
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    status_.active = false;
    status_.status_message = "Inactive (0 MB VRAM)";
}

bool FsrSpatialScaler::ToggleScaling(HWND target_game_hwnd, float sharpness, bool dry_run) {
    bool is_active = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_active = status_.active;
    }
    if (is_active || running_.load()) {
        StopScaling();
        return false;
    } else {
        return StartScaling(target_game_hwnd, sharpness, dry_run);
    }
}

void FsrSpatialScaler::SetSharpness(float sharpness) {
    sharpness = std::clamp(sharpness, 0.0f, 1.0f);
    sharpness_.store(sharpness, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    status_.sharpness = sharpness;
}

void FsrSpatialScaler::SetAlgorithm(UpscaleAlgorithm algo) {
    algorithm_.store(static_cast<int>(algo), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    status_.algorithm = algo;
}

UpscaleAlgorithm FsrSpatialScaler::GetAlgorithm() const {
    return static_cast<UpscaleAlgorithm>(algorithm_.load(std::memory_order_relaxed));
}

const char* FsrSpatialScaler::AlgorithmToString(UpscaleAlgorithm algo) {
    switch (algo) {
        case UpscaleAlgorithm::FSR1_UltraClean:
            return "FSR 1.0 Ultra-Clean";
        case UpscaleAlgorithm::NIS_HybridCatmull:
            return "NIS / Bicubico 16-Tap";
        case UpscaleAlgorithm::Anime4K_LineReconstruct:
            return "Anime4K (JRPG / Cel)";
        default:
            return "Unknown";
    }
}

FsrScalerStatus FsrSpatialScaler::GetStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

} // namespace vegaflow
