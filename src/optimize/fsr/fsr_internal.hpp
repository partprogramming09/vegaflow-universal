#pragma once
// Internal helpers shared by the implementation files of this module. Not part of the public API.

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

namespace vegaflow::fsr_internal {

extern const char* const kFullscreenVs;

extern const char* const kEasuPs;

extern const char* const kRcasPs;

// ============================================================================
// Constant Buffer Structures
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
);

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

bool CompileShaderHelper(
    const char* source,
    const char* entryPoint,
    const char* target,
    ID3DBlob** outBlob,
    std::string* outError
);

} // namespace vegaflow::fsr_internal
