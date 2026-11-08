#include "optimize/fsr/fsr_internal.hpp"

namespace vegaflow::fsr_internal {

// ============================================================================
// Embedded HLSL Shaders (AMD FSR 1.0 - EASU + RCAS)
// ============================================================================

// Vertex Shader for a full-screen triangle without a Vertex Buffer
const char* const kFullscreenVs = R"(
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

// Pixel Shader Pass 1: EASU / Multi-Algorithm Spatial Upsampling
const char* const kEasuPs = R"(
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

// Pixel Shader Pass 2: RCAS / Multi-Algorithm Contrast & Line Adaptive Sharpening
const char* const kRcasPs = R"(
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
// Safe Dynamic-Link Functions for D3D11 and D3DCompiler
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

} // namespace vegaflow::fsr_internal
