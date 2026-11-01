#include "fsr_scaler.hpp"

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

namespace {

// ============================================================================
// Shaders HLSL Embebidos (AMD FSR 1.0 - EASU + RCAS)
// ============================================================================

// Vertex Shader para triángulo de pantalla completa sin Vertex Buffer
const char* kFullscreenVs = R"(
struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VS_Fullscreen(uint vertexID : SV_VertexID) {
    VSOutput output;
    output.uv = float2((vertexID == 1) ? 2.0f : 0.0f, (vertexID == 2) ? 2.0f : 0.0f);
    output.pos = float4(output.uv.x * 2.0f - 1.0f, 1.0f - output.uv.y * 2.0f, 0.0f, 1.0f);
    return output;
}
)";

// Pixel Shader Pasada 1: EASU / Multi-Algorithm Spatial Upsampling
const char* kEasuPs = R"(
cbuffer EasuConstants : register(b0) {
    float2 srcOffsetUV;
    float2 srcScaleUV;
    float2 invSrcTexSize;
    float2 outSize;
    int algorithmMode;
    float3 pad;
};

Texture2D<float4> InputTex : register(t0);

struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Lum(float3 c) {
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float Lanczos2(float x) {
    float ax = abs(x);
    if (ax < 1e-5f) return 1.0f;
    if (ax >= 2.0f) return 0.0f;
    float pi_x = 3.141592653589793f * x;
    return (sin(pi_x) / pi_x) * (sin(pi_x * 0.5f) / (pi_x * 0.5f));
}

float CatmullRom(float x) {
    float ax = abs(x);
    if (ax <= 1.0f) {
        return 1.5f * ax * ax * ax - 2.5f * ax * ax + 1.0f;
    } else if (ax < 2.0f) {
        return -0.5f * ax * ax * ax + 2.5f * ax * ax - 4.0f * ax + 2.0f;
    }
    return 0.0f;
}

float4 PS_Easu(VSOutput input) : SV_TARGET {
    float2 gameUV = srcOffsetUV + input.uv * srcScaleUV;
    float2 posInSrc = gameUV / invSrcTexSize - 0.5f;
    int2 base = int2(floor(posInSrc));
    float2 f = frac(posInSrc);

    int2 srcSize = int2(round(1.0f / invSrcTexSize.x), round(1.0f / invSrcTexSize.y));
    int2 maxCoord = max(srcSize - int2(1, 1), int2(0, 0));

    #define FETCH_TAP(ox, oy) InputTex.Load(int3(clamp(base + int2(ox, oy), int2(0, 0), maxCoord), 0)).rgb

    // Muestreo de los 4 vecinos centrales
    float3 c_b1 = FETCH_TAP( 0,  0);
    float3 c_c1 = FETCH_TAP( 1,  0);
    float3 c_b2 = FETCH_TAP( 0,  1);
    float3 c_c2 = FETCH_TAP( 1,  1);

    // Mínimo y máximo local de los 4 vecinos centrales para anti-ringing
    float3 minColor = min(min(c_b1, c_c1), min(c_b2, c_c2));
    float3 maxColor = max(max(c_b1, c_c1), max(c_b2, c_c2));

    if (algorithmMode == 0) {
        // [MODO 0: FSR 1.0 Ultra-Clean + Anti-Ringing]
        float3 c_b0 = FETCH_TAP( 0, -1);
        float3 c_c0 = FETCH_TAP( 1, -1);
        float3 c_a1 = FETCH_TAP(-1,  0);
        float3 c_d1 = FETCH_TAP( 2,  0);
        float3 c_a2 = FETCH_TAP(-1,  1);
        float3 c_d2 = FETCH_TAP( 2,  1);
        float3 c_b3 = FETCH_TAP( 0,  2);
        float3 c_c3 = FETCH_TAP( 1,  2);

        float l_b0 = Lum(c_b0); float l_c0 = Lum(c_c0);
        float l_a1 = Lum(c_a1); float l_b1 = Lum(c_b1); float l_c1 = Lum(c_c1); float l_d1 = Lum(c_d1);
        float l_a2 = Lum(c_a2); float l_b2 = Lum(c_b2); float l_c2 = Lum(c_c2); float l_d2 = Lum(c_d2);
        float l_b3 = Lum(c_b3); float l_c3 = Lum(c_c3);

        // Análisis direccional con anisotropía suavizada
        float d_x = (l_c1 - l_b1) + (l_c2 - l_b2) + 0.5f * ((l_d1 - l_a1) + (l_d2 - l_a2));
        float d_y = (l_b2 - l_b1) + (l_c2 - l_c1) + 0.5f * ((l_b3 - l_b0) + (l_c3 - l_c0));

        float gradLen = length(float2(d_x, d_y));
        float2 dir = (gradLen > 1e-4f) ? float2(d_x, d_y) / gradLen : float2(1.0f, 0.0f);
        float2 tangent = float2(-dir.y, dir.x);

        float stretch = 1.0f + saturate(gradLen * 1.8f) * 0.32f;

        float3 sumLanczos = float3(0.0f, 0.0f, 0.0f);
        float sumWeight = 0.0f;

        #define ACCUM_L(col, ox, oy) { \
            float2 d = float2(ox, oy) - f; \
            float u = dot(d, tangent) / stretch; \
            float v = dot(d, dir) * stretch; \
            float w = Lanczos2(u) * Lanczos2(v); \
            sumLanczos += col * w; \
            sumWeight += w; \
        }

        ACCUM_L(c_b0,  0, -1)
        ACCUM_L(c_c0,  1, -1)
        ACCUM_L(c_a1, -1,  0)
        ACCUM_L(c_b1,  0,  0)
        ACCUM_L(c_c1,  1,  0)
        ACCUM_L(c_d1,  2,  0)
        ACCUM_L(c_a2, -1,  1)
        ACCUM_L(c_b2,  0,  1)
        ACCUM_L(c_c2,  1,  1)
        ACCUM_L(c_d2,  2,  1)
        ACCUM_L(c_b3,  0,  2)
        ACCUM_L(c_c3,  1,  2)

        #undef ACCUM_L

        float3 lanczosColor = sumLanczos / max(sumWeight, 1e-5f);

        // Mezcla suave con bilineal en áreas de alta varianza local (evita efecto óleo/acuarela)
        float3 bilinearColor = lerp(lerp(c_b1, c_c1, f.x), lerp(c_b2, c_c2, f.x), f.y);
        float localVar = max(abs(l_b1 - l_c1), max(abs(l_b1 - l_b2), abs(l_c2 - l_b1)));
        float blendFactor = saturate(localVar * 2.0f) * 0.15f;
        float3 fsrColor = lerp(lanczosColor, bilinearColor, blendFactor);
        float3 result = clamp(fsrColor, minColor, maxColor);
        return float4(result, 1.0f);
    }
    else if (algorithmMode == 1) {
        // [MODO 1: NIS / Catmull-Rom 16-Tap Direccional Natural]
        float3 nisSum = float3(0.0f, 0.0f, 0.0f);
        float nisWeight = 0.0f;

        [unroll]
        for (int dy = -1; dy <= 2; ++dy) {
            float wy = CatmullRom(float(dy) - f.y);
            [unroll]
            for (int dx = -1; dx <= 2; ++dx) {
                float wx = CatmullRom(float(dx) - f.x);
                float w = wx * wy;
                float3 sCol = FETCH_TAP(dx, dy);
                nisSum += sCol * w;
                nisWeight += w;
            }
        }

        float3 nisColor = nisSum / max(nisWeight, 1e-5f);
        float3 result = clamp(nisColor, minColor, maxColor);
        return float4(result, 1.0f);
    }
    else {
        // [MODO 2: Anime4K Line Reconstruction - Pasada 1 Bilateral Cel-Shading]
        float3 animeSum = float3(0.0f, 0.0f, 0.0f);
        float animeWeight = 0.0f;
        float centerLum = Lum(0.25f * (c_b1 + c_c1 + c_b2 + c_c2));

        [unroll]
        for (int dy = -1; dy <= 2; ++dy) {
            float wy = CatmullRom(float(dy) - f.y);
            [unroll]
            for (int dx = -1; dx <= 2; ++dx) {
                float wx = CatmullRom(float(dx) - f.x);
                float3 sCol = FETCH_TAP(dx, dy);
                float sim = exp(-abs(Lum(sCol) - centerLum) * 6.0f);
                float w = (wx * wy) * max(sim, 0.05f);
                animeSum += sCol * w;
                animeWeight += w;
            }
        }

        float3 animeColor = animeSum / max(animeWeight, 1e-5f);
        float3 result = clamp(animeColor, minColor, maxColor);
        return float4(result, 1.0f);
    }

    #undef FETCH_TAP
}
)";

// Pixel Shader Pasada 2: RCAS / Multi-Algorithm Contrast & Line Adaptive Sharpening
const char* kRcasPs = R"(
cbuffer RcasConstants : register(b0) {
    float2 invOutSize;
    float sharpness;
    int algorithmMode;
};

Texture2D<float4> InputTex : register(t0);

struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

float Lum(float3 c) {
    return dot(c, float3(0.299f, 0.587f, 0.114f));
}

float4 PS_Rcas(VSOutput input) : SV_TARGET {
    int2 pos = int2(input.uv / invOutSize);
    int2 outSize = int2(round(1.0f / invOutSize.x), round(1.0f / invOutSize.y));
    int2 maxCoord = max(outSize - int2(1, 1), int2(0, 0));

    #define LOAD_TAP(ox, oy) InputTex.Load(int3(clamp(pos + int2(ox, oy), int2(0, 0), maxCoord), 0)).rgb

    // Muestreo de 9 taps en vecindad 3x3
    float3 nw = LOAD_TAP(-1, -1);
    float3 b  = LOAD_TAP( 0, -1);
    float3 ne = LOAD_TAP( 1, -1);
    float3 d  = LOAD_TAP(-1,  0);
    float3 e  = LOAD_TAP( 0,  0);
    float3 f  = LOAD_TAP( 1,  0);
    float3 sw = LOAD_TAP(-1,  1);
    float3 h  = LOAD_TAP( 0,  1);
    float3 se = LOAD_TAP( 1,  1);

    #undef LOAD_TAP

    float nw_l = Lum(nw); float b_l = Lum(b); float ne_l = Lum(ne);
    float d_l  = Lum(d);  float e_l = Lum(e); float f_l  = Lum(f);
    float sw_l = Lum(sw); float h_l = Lum(h); float se_l = Lum(se);

    // Mínimo y máximo de la cruz y de la cuadrícula completa de 9
    float minL = min(e_l, min(min(b_l, d_l), min(f_l, h_l)));
    float maxL = max(e_l, max(max(b_l, d_l), max(f_l, h_l)));

    float minL9 = min(minL, min(min(nw_l, ne_l), min(sw_l, se_l)));
    float maxL9 = max(maxL, max(max(nw_l, ne_l), max(sw_l, se_l)));

    float3 minRgb = min(e, min(min(b, d), min(f, h)));
    float3 maxRgb = max(e, max(max(b, d), max(f, h)));

    float3 minRgb9 = min(minRgb, min(min(nw, ne), min(sw, se)));
    float3 maxRgb9 = max(maxRgb, max(max(nw, ne), max(sw, se)));

    // Protección Anti-Halo para Texto/HUD
    float localContrast = maxL9 - minL9;
    float uiProtect = 1.0f - smoothstep(0.35f, 0.72f, localContrast);

    if (algorithmMode == 0) {
        // [MODO 0: FSR 1.0 Ultra-Clean RCAS]
        float amp = saturate(min(minL, 1.0f - maxL) / max(maxL, 1e-5f));
        amp = sqrt(amp);

        float peak = -lerp(0.10f, 0.25f, saturate(sharpness));
        float w = peak * amp * (0.35f + 0.65f * uiProtect);

        float3 crossRgb = b + d + f + h;
        float3 diagRgb = 0.25f * (nw + ne + sw + se);
        float3 neighbors = 0.8f * crossRgb + 0.8f * diagRgb;
        float3 sharpened = neighbors * w + e;
        sharpened /= (4.0f * w + 1.0f);
        sharpened = clamp(sharpened, minRgb, maxRgb);
        return float4(sharpened, 1.0f);
    }
    else if (algorithmMode == 1) {
        // [MODO 1: NIS Directional Sharpening con compresión Soft-Knee]
        float3 detail = e - (b + d + f + h + 0.5f * (nw + ne + sw + se)) / 6.0f;
        float gain = lerp(0.25f, 0.85f, saturate(sharpness)) * uiProtect;
        float3 boostedDetail = tanh(detail * 3.0f) * (gain / 3.0f);
        float3 sharpened = e + boostedDetail;
        sharpened = clamp(sharpened, minRgb9, maxRgb9);
        return float4(sharpened, 1.0f);
    }
    else {
        // [MODO 2: Anime4K Push-Pull Line Reconstruction]
        float gx = (ne_l + 2.0f * f_l + se_l) - (nw_l + 2.0f * d_l + sw_l);
        float gy = (sw_l + 2.0f * h_l + se_l) - (nw_l + 2.0f * b_l + ne_l);
        float edgeStrength = saturate(length(float2(gx, gy)) * 1.5f);

        float laplacian = (b_l + d_l + f_l + h_l + 0.5f * (nw_l + ne_l + sw_l + se_l)) / 6.0f - e_l;
        float pushFactor = saturate(sharpness) * edgeStrength * uiProtect;

        float3 sharpened = e;
        if (laplacian > 0.01f) {
            sharpened = lerp(e, minRgb, saturate(laplacian * 4.0f) * pushFactor);
        } else if (laplacian < -0.01f) {
            sharpened = lerp(e, maxRgb, saturate(-laplacian * 4.0f) * pushFactor);
        }
        sharpened = clamp(sharpened, minRgb9, maxRgb9);
        return float4(sharpened, 1.0f);
    }
}
)";

// ============================================================================
// Estructuras de Constant Buffers
// ============================================================================

struct alignas(16) EasuConstants {
    float srcOffsetUV[2];
    float srcScaleUV[2];
    float invSrcTexSize[2];
    float outSize[2];
    int algorithmMode;
    float pad[3];
};

struct alignas(16) RcasConstants {
    float invOutSize[2];
    float sharpness;
    int algorithmMode;
};

// ============================================================================
// Funciones Seguras de Enlace Dinámico para D3D11 y D3DCompiler
// ============================================================================

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

typedef HRESULT (WINAPI *pfn_D3DCompile)(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob** ppCode,
    ID3DBlob** ppErrorMsgs
);

typedef HRESULT (WINAPI *pfn_D3D11CreateDevice)(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
);

HRESULT SafeD3DCompile(
    LPCVOID pSrcData,
    SIZE_T SrcDataSize,
    LPCSTR pSourceName,
    const D3D_SHADER_MACRO* pDefines,
    ID3DInclude* pInclude,
    LPCSTR pEntrypoint,
    LPCSTR pTarget,
    UINT Flags1,
    UINT Flags2,
    ID3DBlob** ppCode,
    ID3DBlob** ppErrorMsgs
) {
    static pfn_D3DCompile pD3DCompile = nullptr;
    static bool resolved = false;
    if (!resolved) {
        HMODULE hMod = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!hMod) hMod = LoadLibraryW(L"d3dcompiler_46.dll");
        if (!hMod) hMod = LoadLibraryW(L"d3dcompiler_43.dll");
        if (!hMod) hMod = LoadLibraryW(L"d3dcompiler.dll");
        if (hMod) {
            pD3DCompile = reinterpret_cast<pfn_D3DCompile>(GetProcAddress(hMod, "D3DCompile"));
        }
        resolved = true;
    }
    if (pD3DCompile) {
        return pD3DCompile(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
    }
    return E_FAIL;
}

HRESULT SafeD3D11CreateDevice(
    IDXGIAdapter* pAdapter,
    D3D_DRIVER_TYPE DriverType,
    HMODULE Software,
    UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel,
    ID3D11DeviceContext** ppImmediateContext
) {
    static pfn_D3D11CreateDevice pCreate = nullptr;
    static bool resolved = false;
    if (!resolved) {
        HMODULE hMod = LoadLibraryW(L"d3d11.dll");
        if (hMod) {
            pCreate = reinterpret_cast<pfn_D3D11CreateDevice>(GetProcAddress(hMod, "D3D11CreateDevice"));
        }
        resolved = true;
    }
    if (pCreate) {
        return pCreate(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    }
    return E_FAIL;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool CompileShaderHelper(
    const char* source,
    const char* entryPoint,
    const char* target,
    ID3DBlob** outBlob,
    std::string* outError
) {
    ID3DBlob* codeBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = SafeD3DCompile(
        source,
        std::strlen(source),
        nullptr,
        nullptr,
        nullptr,
        entryPoint,
        target,
        0,
        0,
        &codeBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (outError && errorBlob) {
            *outError = static_cast<const char*>(errorBlob->GetBufferPointer());
        }
        if (errorBlob) errorBlob->Release();
        if (codeBlob) codeBlob->Release();
        return false;
    }

    if (errorBlob) errorBlob->Release();
    if (outBlob) {
        *outBlob = codeBlob;
    } else if (codeBlob) {
        codeBlob->Release();
    }
    return true;
}

} // namespace

// ============================================================================
// Implementación de FsrSpatialScaler
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

    // Ejecución real
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

void FsrSpatialScaler::RenderLoop(HWND target_hwnd, float sharpness) {
    (void)sharpness;
    const wchar_t* kOverlayClassName = L"VegaFlowFsrOverlayClass";
    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.lpszClassName = kOverlayClassName;
    RegisterClassExW(&wc);

    uint32_t outW = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    uint32_t outH = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    if (outW == 0 || outH == 0) {
        outW = 1920;
        outH = 1080;
    }

    HWND hwndOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayClassName,
        L"VegaFlow FSR 1.0 Overlay",
        WS_POPUP,
        0, 0,
        static_cast<int>(outW),
        static_cast<int>(outH),
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwndOverlay) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Failed to create overlay window";
        running_.store(false);
        return;
    }

    SetLayeredWindowAttributes(hwndOverlay, 0, 255, LWA_ALPHA);
    if (!SetWindowDisplayAffinity(hwndOverlay, WDA_EXCLUDEFROMCAPTURE)) {
        SetWindowDisplayAffinity(hwndOverlay, 1 /* WDA_MONITOR */);
    }
    ShowWindow(hwndOverlay, SW_SHOWNOACTIVATE);
    UpdateWindow(hwndOverlay);

    // Inicialización de Direct3D 11
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL featureLevel;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    HRESULT hr = SafeD3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevels, 4,
        D3D11_SDK_VERSION, &device, &featureLevel, &context
    );

    if (FAILED(hr)) {
        hr = SafeD3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevels, 4,
            D3D11_SDK_VERSION, &device, &featureLevel, &context
        );
    }

    if (FAILED(hr) || !device || !context) {
        if (hwndOverlay) DestroyWindow(hwndOverlay);
        UnregisterClassW(kOverlayClassName, hInstance);
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Failed to create D3D11 device";
        running_.store(false);
        return;
    }

    // Configuración de DXGI SwapChain
    IDXGIDevice* dxgiDevice = nullptr;
    device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));

    IDXGIAdapter* dxgiAdapter = nullptr;
    if (dxgiDevice) {
        dxgiDevice->GetAdapter(&dxgiAdapter);
    }

    IDXGIFactory2* dxgiFactory2 = nullptr;
    if (dxgiAdapter) {
        dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
    }

    IDXGISwapChain1* swapChain = nullptr;
    if (dxgiFactory2) {
        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        scDesc.Width = outW;
        scDesc.Height = outH;
        scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.BufferCount = 2;
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        hr = dxgiFactory2->CreateSwapChainForHwnd(device, hwndOverlay, &scDesc, nullptr, nullptr, &swapChain);
        if (FAILED(hr)) {
            scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = dxgiFactory2->CreateSwapChainForHwnd(device, hwndOverlay, &scDesc, nullptr, nullptr, &swapChain);
        }
    }

    if (!swapChain) {
        if (dxgiFactory2) dxgiFactory2->Release();
        if (dxgiAdapter) dxgiAdapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        context->Release();
        device->Release();
        DestroyWindow(hwndOverlay);
        UnregisterClassW(kOverlayClassName, hInstance);
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Failed to create DXGI SwapChain";
        running_.store(false);
        return;
    }

    ID3D11Texture2D* backBuffer = nullptr;
    ID3D11RenderTargetView* backBufferRtv = nullptr;
    if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer)))) {
        device->CreateRenderTargetView(backBuffer, nullptr, &backBufferRtv);
        backBuffer->Release();
    }

    // Textura intermedia (outW x outH) para salida EASU y entrada RCAS
    D3D11_TEXTURE2D_DESC intermediateDesc{};
    intermediateDesc.Width = outW;
    intermediateDesc.Height = outH;
    intermediateDesc.MipLevels = 1;
    intermediateDesc.ArraySize = 1;
    intermediateDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    intermediateDesc.SampleDesc.Count = 1;
    intermediateDesc.Usage = D3D11_USAGE_DEFAULT;
    intermediateDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* intermediateTex = nullptr;
    ID3D11RenderTargetView* intermediateRtv = nullptr;
    ID3D11ShaderResourceView* intermediateSrv = nullptr;
    device->CreateTexture2D(&intermediateDesc, nullptr, &intermediateTex);
    if (intermediateTex) {
        device->CreateRenderTargetView(intermediateTex, nullptr, &intermediateRtv);
        device->CreateShaderResourceView(intermediateTex, nullptr, &intermediateSrv);
    }

    // Compilar y crear Shaders
    ID3DBlob* vsBlob = nullptr;
    ID3D11VertexShader* vs = nullptr;
    if (CompileShaderHelper(kFullscreenVs, "VS_Fullscreen", "vs_5_0", &vsBlob, nullptr) && vsBlob) {
        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs);
        vsBlob->Release();
    }

    ID3DBlob* easuBlob = nullptr;
    ID3D11PixelShader* psEasu = nullptr;
    if (CompileShaderHelper(kEasuPs, "PS_Easu", "ps_5_0", &easuBlob, nullptr) && easuBlob) {
        device->CreatePixelShader(easuBlob->GetBufferPointer(), easuBlob->GetBufferSize(), nullptr, &psEasu);
        easuBlob->Release();
    }

    ID3DBlob* rcasBlob = nullptr;
    ID3D11PixelShader* psRcas = nullptr;
    if (CompileShaderHelper(kRcasPs, "PS_Rcas", "ps_5_0", &rcasBlob, nullptr) && rcasBlob) {
        device->CreatePixelShader(rcasBlob->GetBufferPointer(), rcasBlob->GetBufferSize(), nullptr, &psRcas);
        rcasBlob->Release();
    }

    // Constant Buffers
    ID3D11Buffer* easuCb = nullptr;
    ID3D11Buffer* rcasCb = nullptr;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(EasuConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cbDesc, nullptr, &easuCb);

    cbDesc.ByteWidth = sizeof(RcasConstants);
    device->CreateBuffer(&cbDesc, nullptr, &rcasCb);

    // Rasterizer State sin culling
    D3D11_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rsCullNone = nullptr;
    device->CreateRasterizerState(&rastDesc, &rsCullNone);

    // Duplicación de Salida DXGI (Desktop Duplication)
    IDXGIOutput* dxgiOutput = nullptr;
    IDXGIOutput1* dxgiOutput1 = nullptr;
    IDXGIOutputDuplication* deskDupl = nullptr;
    if (dxgiAdapter && SUCCEEDED(dxgiAdapter->EnumOutputs(0, &dxgiOutput)) && dxgiOutput) {
        if (SUCCEEDED(dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&dxgiOutput1)))) {
            dxgiOutput1->DuplicateOutput(device, &deskDupl);
            dxgiOutput1->Release();
        }
        dxgiOutput->Release();
    }

    DXGI_OUTDUPL_DESC duplDesc{};
    if (deskDupl) {
        deskDupl->GetDesc(&duplDesc);
    }
    uint32_t desktopW = (duplDesc.ModeDesc.Width > 0) ? duplDesc.ModeDesc.Width : outW;
    uint32_t desktopH = (duplDesc.ModeDesc.Height > 0) ? duplDesc.ModeDesc.Height : outH;
    DXGI_FORMAT desktopFmt = (duplDesc.ModeDesc.Format != DXGI_FORMAT_UNKNOWN) ? duplDesc.ModeDesc.Format : DXGI_FORMAT_B8G8R8A8_UNORM;

    // Textura y SRV para el escritorio capturado
    D3D11_TEXTURE2D_DESC capDesc{};
    capDesc.Width = desktopW;
    capDesc.Height = desktopH;
    capDesc.MipLevels = 1;
    capDesc.ArraySize = 1;
    capDesc.Format = desktopFmt;
    capDesc.SampleDesc.Count = 1;
    capDesc.Usage = D3D11_USAGE_DEFAULT;
    capDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* capturedTex = nullptr;
    ID3D11ShaderResourceView* capturedSrv = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&capDesc, nullptr, &capturedTex)) && capturedTex) {
        device->CreateShaderResourceView(capturedTex, nullptr, &capturedSrv);
    }

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(outW);
    vp.Height = static_cast<float>(outH);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;

    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);

    uint64_t frames_upscaled = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = true;
        status_.output_width = outW;
        status_.output_height = outH;
        status_.sharpness = sharpness_.load(std::memory_order_relaxed);
        status_.status_message = "Active: FSR 1.0 (EASU + RCAS) upscaling";
    }

    // ========================================================================
    // Bucle Principal de Renderizado FSR 1.0
    // ========================================================================
    while (running_.load() && IsWindow(target_hwnd)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running_.load()) break;

        RECT clientRc{};
        if (!GetClientRect(target_hwnd, &clientRc)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        POINT pt{clientRc.left, clientRc.top};
        ClientToScreen(target_hwnd, &pt);

        int cWidth = clientRc.right - clientRc.left;
        int cHeight = clientRc.bottom - clientRc.top;
        if (cWidth <= 0 || cHeight <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        uint32_t srcW = static_cast<uint32_t>(cWidth);
        uint32_t srcH = static_cast<uint32_t>(cHeight);

        if (!deskDupl) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo{};
        IDXGIResource* desktopResource = nullptr;
        HRESULT acqHr = deskDupl->AcquireNextFrame(16, &frameInfo, &desktopResource);

        if (acqHr == DXGI_ERROR_WAIT_TIMEOUT) {
            continue;
        }

        if (acqHr == DXGI_ERROR_ACCESS_LOST) {
            deskDupl->Release();
            deskDupl = nullptr;
            IDXGIOutput* out = nullptr;
            if (dxgiAdapter && SUCCEEDED(dxgiAdapter->EnumOutputs(0, &out)) && out) {
                IDXGIOutput1* out1 = nullptr;
                if (SUCCEEDED(out->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void**>(&out1)))) {
                    out1->DuplicateOutput(device, &deskDupl);
                    out1->Release();
                }
                out->Release();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (FAILED(acqHr) || !desktopResource) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        LARGE_INTEGER qpcStart, qpcEnd;
        QueryPerformanceCounter(&qpcStart);

        // Copia de recurso capturado a capturedTex
        ID3D11Texture2D* rawDesktopTex = nullptr;
        if (SUCCEEDED(desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&rawDesktopTex))) && rawDesktopTex) {
            if (capturedTex) {
                context->CopyResource(capturedTex, rawDesktopTex);
            }
            rawDesktopTex->Release();
        }
        deskDupl->ReleaseFrame();
        desktopResource->Release();

        if (rsCullNone) {
            context->RSSetState(rsCullNone);
        }
        context->RSSetViewports(1, &vp);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(nullptr);
        context->VSSetShader(vs, nullptr, 0);

        // --------------------------------------------------------------------
        // Pasada 1: EASU (capturedTex -> intermediateTex)
        // --------------------------------------------------------------------
        context->OMSetRenderTargets(1, &intermediateRtv, nullptr);
        context->PSSetShader(psEasu, nullptr, 0);

        if (easuCb) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(easuCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                EasuConstants* ec = static_cast<EasuConstants*>(mapped.pData);
                ec->srcOffsetUV[0] = static_cast<float>(pt.x) / static_cast<float>(desktopW);
                ec->srcOffsetUV[1] = static_cast<float>(pt.y) / static_cast<float>(desktopH);
                ec->srcScaleUV[0] = static_cast<float>(srcW) / static_cast<float>(desktopW);
                ec->srcScaleUV[1] = static_cast<float>(srcH) / static_cast<float>(desktopH);
                ec->invSrcTexSize[0] = 1.0f / static_cast<float>(desktopW);
                ec->invSrcTexSize[1] = 1.0f / static_cast<float>(desktopH);
                ec->outSize[0] = static_cast<float>(outW);
                ec->outSize[1] = static_cast<float>(outH);
                ec->algorithmMode = algorithm_.load(std::memory_order_relaxed);
                ec->pad[0] = 0.0f;
                ec->pad[1] = 0.0f;
                ec->pad[2] = 0.0f;
                context->Unmap(easuCb, 0);
            }
            context->PSSetConstantBuffers(0, 1, &easuCb);
        }
        context->PSSetShaderResources(0, 1, &capturedSrv);
        context->Draw(3, 0);

        ID3D11ShaderResourceView* nullSrv = nullptr;
        context->PSSetShaderResources(0, 1, &nullSrv);

        // --------------------------------------------------------------------
        // Pasada 2: RCAS (intermediateTex -> backBufferRtv)
        // --------------------------------------------------------------------
        context->OMSetRenderTargets(1, &backBufferRtv, nullptr);
        context->PSSetShader(psRcas, nullptr, 0);

        float curSharpness = sharpness_.load(std::memory_order_relaxed);

        if (rcasCb) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(context->Map(rcasCb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                RcasConstants* rc = static_cast<RcasConstants*>(mapped.pData);
                rc->invOutSize[0] = 1.0f / static_cast<float>(outW);
                rc->invOutSize[1] = 1.0f / static_cast<float>(outH);
                rc->sharpness = curSharpness;
                rc->algorithmMode = algorithm_.load(std::memory_order_relaxed);
                context->Unmap(rcasCb, 0);
            }
            context->PSSetConstantBuffers(0, 1, &rcasCb);
        }
        context->PSSetShaderResources(0, 1, &intermediateSrv);
        context->Draw(3, 0);

        context->PSSetShaderResources(0, 1, &nullSrv);

        // Presentación en SwapChain
        swapChain->Present(1, 0);

        QueryPerformanceCounter(&qpcEnd);
        double passTimeMs = static_cast<double>(qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / static_cast<double>(qpcFreq.QuadPart);
        frames_upscaled++;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.source_width = srcW;
            status_.source_height = srcH;
            status_.output_width = outW;
            status_.output_height = outH;
            status_.sharpness = curSharpness;
            status_.algorithm = static_cast<UpscaleAlgorithm>(algorithm_.load(std::memory_order_relaxed));
            status_.frames_upscaled = frames_upscaled;
            status_.last_pass_time_ms = passTimeMs;
        }
    }

    // ========================================================================
    // Destrucción limpia del 100% de los recursos D3D11/DXGI (0 MB VRAM)
    // ========================================================================
    if (rsCullNone) { rsCullNone->Release(); rsCullNone = nullptr; }
    if (capturedSrv) { capturedSrv->Release(); capturedSrv = nullptr; }
    if (capturedTex) { capturedTex->Release(); capturedTex = nullptr; }
    if (deskDupl) { deskDupl->Release(); deskDupl = nullptr; }
    if (easuCb) { easuCb->Release(); easuCb = nullptr; }
    if (rcasCb) { rcasCb->Release(); rcasCb = nullptr; }
    if (psRcas) { psRcas->Release(); psRcas = nullptr; }
    if (psEasu) { psEasu->Release(); psEasu = nullptr; }
    if (vs) { vs->Release(); vs = nullptr; }
    if (intermediateSrv) { intermediateSrv->Release(); intermediateSrv = nullptr; }
    if (intermediateRtv) { intermediateRtv->Release(); intermediateRtv = nullptr; }
    if (intermediateTex) { intermediateTex->Release(); intermediateTex = nullptr; }
    if (backBufferRtv) { backBufferRtv->Release(); backBufferRtv = nullptr; }
    if (swapChain) { swapChain->Release(); swapChain = nullptr; }
    if (dxgiFactory2) { dxgiFactory2->Release(); dxgiFactory2 = nullptr; }
    if (dxgiAdapter) { dxgiAdapter->Release(); dxgiAdapter = nullptr; }
    if (dxgiDevice) { dxgiDevice->Release(); dxgiDevice = nullptr; }

    if (context) {
        context->ClearState();
        context->Flush();
        context->Release();
        context = nullptr;
    }
    if (device) {
        device->Release();
        device = nullptr;
    }

    if (hwndOverlay && IsWindow(hwndOverlay)) {
        DestroyWindow(hwndOverlay);
        hwndOverlay = nullptr;
    }
    UnregisterClassW(kOverlayClassName, hInstance);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active = false;
        status_.status_message = "Inactive (0 MB VRAM)";
    }
}

} // namespace vegaflow
