#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

struct TimerResolutionState {
    ULONG minimum_ns100 = 0; // ej. 156250 (15.625 ms)
    ULONG maximum_ns100 = 0; // ej. 5000 (0.5 ms)
    ULONG current_ns100 = 0;
    bool active_high_res = false;
};

struct ShaderCacheReport {
    std::wstring path;
    uint64_t total_size_bytes = 0;
    uint32_t file_count = 0;
    bool exists = false;
};

struct SystemGraphicsTweaksState {
    bool mpo_disabled = false; // HKLM\SOFTWARE\Microsoft\Windows\Dwm -> OverlayTestMode == 5
    uint32_t dwm_overlay_test_mode = 0;
    bool amd_shader_cache_forced = false; // ShaderCache == 32 00 en Class\{4d36e968-e325-11ce-bfc1-08002be10318}
    bool memory_compression_enabled = false; // Proceso del kernel "MemCompression" activo
    bool mmcss_gaming_optimized = false; // Tasks\Games -> GPU Priority == 8 && SystemResponsiveness <= 10
    bool amd_dx11_uma_boost = false;     // SurfaceFormatReplacements == 0x3100
    bool gamedvr_disabled = false;       // GameDVR_Enabled == 0 && AppCaptureEnabled == 0
    bool apu_power_optimized = false;    // Plan de energía APU (Boost Eficiente + PCIe ASPM Off)
    uint32_t win32_priority_separation = 2; // HKLM\SYSTEM\CurrentControlSet\Control\PriorityControl -> Win32PrioritySeparation
    bool kernel_paging_shield_active = false; // HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management -> DisablePagingExecutive == 1
    uint32_t amd_tessellation_max = 0;       // Límite de teselación detectado (ej. 16, 8, 64)
    std::string mmcss_scheduling_category = "";
    std::string mmcss_sfio_priority = "";
    uint32_t mmcss_clock_rate = 10000;
    uint32_t amd_vram_reported_mb = 0;
    bool amd_vram_override_active = false;
};

enum class ApuPowerMode {
    Balanced = 0,         // Default: Boost agresivo estándar (4.0 GHz)
    GpuPrioritySustained = 1 // Boost eficiente (3.2-3.4 GHz ceiling / Mode 4) para ceder ~15W continuos a la Vega 6 a 1500 MHz
};

class GraphicsOptimizer {
public:
    static TimerResolutionState QueryTimerResolution();
    static bool EnableHighPrecisionTimer(ULONG desired_ns100 = 5000);
    static bool SetHighResolutionTimer(ULONG desired_ns100 = 5000) { return EnableHighPrecisionTimer(desired_ns100); }
    static bool RestoreDefaultTimer();

    // Estado e inspección de MPO y tweaks del sistema
    static SystemGraphicsTweaksState QuerySystemGraphicsTweaks();
    static bool SetMpoDisabled(bool disable, bool dry_run = false);
    static bool SetAmdShaderCacheForced(bool force_on, bool dry_run = false);
    static bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false);
    static bool SetMmcssGamingProfileOptimized(bool optimize, bool dry_run = false);
    static bool SetAmdDx11UmaDriverBoost(bool enable, bool dry_run = false);
    static bool ApplyAmdDx11DriverBoost(bool enable, bool dry_run = false);
    static bool SetAmdTessellationCap(uint32_t max_tess = 16, bool dry_run = false);
    static bool SetDynamicPrioritySeparation(uint32_t value, bool dry_run = false);
    static bool SetKernelMemoryPagingShield(bool enable, bool dry_run = false);
    static bool QueryKernelMemoryPagingShield();
    static uint32_t QueryAmdTessellationCap();

    // Sobrescritura de Presupuesto VRAM UMA (Windows & DirectX & Driver AMD)
    static bool ApplyAmdVramReportingOverride(uint32_t target_mb = 4096, bool dry_run = false);
    static bool RestoreAmdVramReporting(bool dry_run = false);
    static bool QueryAmdVramReporting(uint32_t& out_driver_mb, uint64_t& out_dxgi_bytes);

    // MMCSS Audio & Gaming Priority Shield (Ronda 2 - Riesgo 2)
    static bool ApplyMmcssTaskTweaks(bool dryRun = false);
    static bool ApplyMmcssTweaks(bool dryRun = false) { return ApplyMmcssTaskTweaks(dryRun); }
    static bool RestoreMmcssTaskTweaks(bool dryRun = false);
    static bool QueryMmcssTaskTweaks(std::string& schedulingCategory, std::string& sfioPriority, uint32_t& clockRate);

    // Desactiva GameDVR / AppCapture en segundo plano (ahorra ancho de banda UMA en todos los juegos DX11/DX12)
    // y fuerza Fullscreen Optimizations de latencia cero (FSEBehaviorMode = 2)
    static bool SetGameDvrAndFseOptimized(bool optimize, bool dry_run = false);

    // Control térmico y de energía APU (Modo GPU-Priority Sustained vs Balanced):
    static bool SetApuPowerMode(ApuPowerMode mode, bool dry_run = false);

    // Optimiza el perfil de energía de la laptop APU (Ryzen 4000/5000/6000/7000 + Radeon iGPU):
    // - Conservado por compatibilidad; invoca SetApuPowerMode(GpuPrioritySustained / Balanced)
    static bool SetApuLaptopPowerProfile(bool optimize, bool dry_run = false);

    // Escribe en HKCU\Software\Microsoft\DirectX\UserGpuPreferences:
    // [exe_full_path] = "SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;"
    static bool ApplyDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run = false);
    static bool ApplyDxgiFlipModelUpgrade(
        const std::wstring& exe_full_path,
        bool dry_run,
        std::wstring* out_effective_preference);
    static bool ApplyDxgiDirectFlipUpgrade(
        const std::wstring& exe_full_path,
        bool dry_run = false,
        std::wstring* out_effective_preference = nullptr);
    static bool HasDxgiFlipModelUpgrade(const std::wstring& exe_full_path);
    static bool HasDirectFlipUpgrade(const std::wstring& exe_full_path);
    static bool QueryUserGpuPreferences(const std::wstring& exe_full_path, std::wstring& out_prefs);
    static bool RemoveDxgiFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run = false);

    // Inspecciona las carpetas de caché de shaders (D3DSCache, AMD\DxCache, NVIDIA\DXCache)
    static std::vector<ShaderCacheReport> InspectShaderCaches();
};

} // namespace vegaflow
