#include "optimize/fsr/fsr_scaler.hpp"
#include "optimize/fsr/fsr_internal.hpp"

namespace vegaflow {

using namespace fsr_internal;

namespace {

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

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

} // namespace

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

    // Direct3D 11 initialization
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

    // DXGI SwapChain configuration
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

    // Intermediate texture (outW x outH) for EASU output and RCAS input
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

    // Compile and create Shaders
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

    // Rasterizer State without culling
    D3D11_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = TRUE;
    ID3D11RasterizerState* rsCullNone = nullptr;
    device->CreateRasterizerState(&rastDesc, &rsCullNone);

    // DXGI Output Duplication (Desktop Duplication)
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

    // Texture and SRV for the captured desktop
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
    // FSR 1.0 Main Render Loop
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

        // Copy captured resource to capturedTex
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
        // Pass 1: EASU (capturedTex -> intermediateTex)
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
        // Pass 2: RCAS (intermediateTex -> backBufferRtv)
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

        // Present to SwapChain
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
    // Clean teardown of 100% of D3D11/DXGI resources (0 MB VRAM)
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
