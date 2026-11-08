#include "ui/gui_components.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vegaflow {

// =============================================================================
// TAB 0: DASHBOARD & LIVE TELEMETRY
// =============================================================================
void RenderTabDashboard(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                        int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW) {
    if (!state) return;

    // 1. Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"VEGAFLOW APU TUNER v1.2", CLR_ACCENT_CYAN, state->hFontHeader);

    std::wstring hwLine = L"AMD Ryzen 5 4600H  |  Radeon Vega 6 (UMA)";
    if (!state->data.system_info.cpu.brand_string.empty()) {
        std::string cpuShort = state->data.system_info.cpu.brand_string;
        std::string gpuShort = state->data.gpu_summary.empty() ? "Radeon Vega 6 (UMA)" : state->data.gpu_summary;
        hwLine = Utf8ToWide(cpuShort) + L"  |  " + Utf8ToWide(gpuShort);
        if (hwLine.length() > 65) {
            hwLine = L"AMD Ryzen 5 4600H  |  Radeon Vega 6 (UMA)";
        }
    }
    DrawTextAt(hdcMem, contentLeft, contentTop + 22, hwLine, CLR_TEXT_MUTED, state->hFontSmall);

    // APU Guard badge (active) to the left of the UAC badge
    RECT rcGuardBadge = { contentRight - 335, contentTop + 2, contentRight - 142, contentTop + 26 };
    std::string guardMode = state->data.system_info.apu_guard.guard_mode.empty()
        ? "SWEET-SPOT GUARD"
        : state->data.system_info.apu_guard.guard_mode;
    for (auto& c : guardMode) {
        c = static_cast<char>(toupper(c));
    }
    std::wstring wGuardMode = Utf8ToWide(guardMode);
    if (state->data.uma_guard_triggered_live) {
        DrawBadge(hdcMem, rcGuardBadge, wGuardMode, CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    } else {
        DrawBadge(hdcMem, rcGuardBadge, wGuardMode, CLR_ACCENT_CYAN, RGB(18, 32, 38), CLR_ACCENT_CYAN, state->hFontSmallBold);
    }

    // Top-right UAC badge
    RECT rcUacBadge = { contentRight - 135, contentTop + 2, contentRight, contentTop + 26 };
    if (state->data.is_elevated) {
        DrawBadge(hdcMem, rcUacBadge, L"ADMINISTRADOR", CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        DrawBadge(hdcMem, rcUacBadge, L"ESTÁNDAR", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    }

    // Subtle top divider
    HPEN hPenTopDiv = CreatePen(PS_SOLID, 1, CLR_CARD_BORDER);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenTopDiv));
    MoveToEx(hdcMem, contentLeft, contentTop + 46, nullptr);
    LineTo(hdcMem, contentRight, contentTop + 46);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenTopDiv);

    // -------------------------------------------------------------------------
    // 2. Top KPIs (4 cards Compactas)
    // -------------------------------------------------------------------------
    int kpiY = contentTop + 54;
    int kpiH = 68;
    int kpiGutter = 10;
    int kpiW = (contentW - 3 * kpiGutter) / 4;

    // KPI 1: AVG FPS
    RECT rcK1 = { contentLeft, kpiY, contentLeft + kpiW, kpiY + kpiH };
    DrawRoundedCard(hdcMem, rcK1, CLR_CARD_BG, CLR_CARD_BORDER, 6);
    DrawTextAt(hdcMem, rcK1.left + 12, rcK1.top + 8, L"AVG FPS", CLR_TEXT_MUTED, state->hFontSmall);
    if (state->data.game_active && state->data.avg_fps > 0.0) {
        wchar_t fpsBuf[32];
        swprintf_s(fpsBuf, L"%.1f", state->data.avg_fps);
        DrawTextAt(hdcMem, rcK1.left + 12, rcK1.top + 28, fpsBuf, CLR_ACCENT_GREEN, state->hFontMetric);
    } else {
        DrawTextAt(hdcMem, rcK1.left + 12, rcK1.top + 28, L"--.-", CLR_TEXT_DIM, state->hFontMetric);
    }

    // KPI 2: 1% LOW
    RECT rcK2 = { rcK1.right + kpiGutter, kpiY, rcK1.right + kpiGutter + kpiW, kpiY + kpiH };
    DrawRoundedCard(hdcMem, rcK2, CLR_CARD_BG, CLR_CARD_BORDER, 6);
    DrawTextAt(hdcMem, rcK2.left + 12, rcK2.top + 8, L"1% LOW", CLR_TEXT_MUTED, state->hFontSmall);
    if (state->data.game_active && state->data.fps_1_low > 0.0) {
        wchar_t lowBuf[32];
        swprintf_s(lowBuf, L"%.1f", state->data.fps_1_low);
        DrawTextAt(hdcMem, rcK2.left + 12, rcK2.top + 28, lowBuf, CLR_ACCENT_CYAN, state->hFontMetric);
    } else {
        DrawTextAt(hdcMem, rcK2.left + 12, rcK2.top + 28, L"--.-", CLR_TEXT_DIM, state->hFontMetric);
    }

    // KPI 3: TIMER KERNEL
    RECT rcK3 = { rcK2.right + kpiGutter, kpiY, rcK2.right + kpiGutter + kpiW, kpiY + kpiH };
    DrawRoundedCard(hdcMem, rcK3, CLR_CARD_BG, CLR_CARD_BORDER, 6);
    DrawTextAt(hdcMem, rcK3.left + 12, rcK3.top + 8, L"TIMER KERNEL", CLR_TEXT_MUTED, state->hFontSmall);
    bool timerActive = (state->data.timer_resolution_ms <= 0.55 || state->data.timer_forced_manual || state->data.game_active);
    wchar_t timerBuf[32];
    if (timerActive) {
        wcscpy_s(timerBuf, L"0.500 ms");
        DrawTextAt(hdcMem, rcK3.left + 12, rcK3.top + 28, timerBuf, CLR_ACCENT_GREEN, state->hFontMetric);
    } else {
        swprintf_s(timerBuf, L"%.3f ms", state->data.timer_resolution_ms);
        DrawTextAt(hdcMem, rcK3.left + 12, rcK3.top + 28, timerBuf, CLR_TEXT_TITLE, state->hFontMetric);
    }

    // KPI 4: STANDBY RAM
    RECT rcK4 = { rcK3.right + kpiGutter, kpiY, contentRight, kpiY + kpiH };
    DrawRoundedCard(hdcMem, rcK4, CLR_CARD_BG, CLR_CARD_BORDER, 6);
    DrawTextAt(hdcMem, rcK4.left + 12, rcK4.top + 8, L"STANDBY RAM", CLR_TEXT_MUTED, state->hFontSmall);
    wchar_t ramBuf[32];
    swprintf_s(ramBuf, L"%llu MB", static_cast<unsigned long long>(state->data.standby_ram_mb));
    DrawTextAt(hdcMem, rcK4.left + 12, rcK4.top + 28, ramBuf, CLR_ACCENT_PURPLE, state->hFontMetric);

    // -------------------------------------------------------------------------
    // 3. Optimization Status Grid (4 cards, 2x2)
    // -------------------------------------------------------------------------
    int gridY = kpiY + kpiH + 12;
    int rowH = 50;
    int rowGutter = 8;
    int colGutter = 12;
    int colW = (contentW - colGutter) / 2;

    struct OptCardDef {
        const wchar_t* title;
        const wchar_t* subtitle;
        bool active;
        bool req_admin;
    };

    std::wstring csmSubtitle = L"Cascadas CSM (2 Splits) | DX12 AsyncCompute: " +
        std::wstring(state->data.system_info.apu_guard.dx12_async_compute_allowed ? L"ON" : L"OFF");

    const OptCardDef optCards[4] = {
        { L"MMCSS Prioridad GPU=8", L"Máxima prioridad en cola WDDM para el juego", (state->data.mmcss_task_active || state->data.mmcss_gaming_optimized), !state->data.is_elevated },
        { L"Temporizador 0.500 ms", L"Quantum de kernel a 0.5ms para latencia mínima", timerActive, false },
        { L"Perfil Sombras APU", csmSubtitle.c_str(), state->data.apu_shadows_active, false },
        { L"Blindaje Kernel NT", L"DisablePagingExecutive activo en memoria física", state->data.kernel_paging_shield_active, !state->data.is_elevated }
    };

    for (int i = 0; i < 4; ++i) {
        int row = i / 2;
        int col = i % 2;
        int cX = contentLeft + col * (colW + colGutter);
        int cY = gridY + row * (rowH + rowGutter);
        RECT rcCard = { cX, cY, cX + colW, cY + rowH };

        DrawRoundedCard(hdcMem, rcCard, CLR_CARD_BG, CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCard.left + 12, rcCard.top + 8, optCards[i].title, CLR_TEXT_TITLE, state->hFontBold);
        DrawTextAt(hdcMem, rcCard.left + 12, rcCard.top + 28, optCards[i].subtitle, CLR_TEXT_MUTED, state->hFontSmall);

        RECT rcBadge = { rcCard.right - 124, rcCard.top + (rowH - 24) / 2, rcCard.right - 12, rcCard.top + (rowH - 24) / 2 + 24 };
        if (optCards[i].active) {
            DrawBadge(hdcMem, rcBadge, L"OPTIMIZADO", CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
        } else if (optCards[i].req_admin) {
            DrawBadge(hdcMem, rcBadge, L"REQUIERE ADMIN", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
        } else {
            DrawBadge(hdcMem, rcBadge, L"INACTIVO", CLR_TEXT_MUTED, RGB(33, 38, 48), CLR_CARD_BORDER, state->hFontSmallBold);
        }
    }

    // -------------------------------------------------------------------------
    // 4. Quick Actions Bar (1-click buttons with hit-test)
    // -------------------------------------------------------------------------
    int actY = gridY + 2 * (rowH + rowGutter) + 4;
    int actH = 38;
    int btnGutter = 10;
    int btnW = (contentW - 3 * btnGutter) / 4;

    // Button 1: Purge Standby RAM
    RECT rcBtn1 = { contentLeft, actY, contentLeft + btnW, actY + actH };
    bool isP1Hover = (state->hovered_id == ID_ACT_PURGE_RAM);
    DrawActionButton(hdcMem, rcBtn1, L"Purgar Standby RAM", isP1Hover, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_PURGE_RAM, state);

    // Button 2: Apply Gaming Combo 2B
    RECT rcBtn2 = { rcBtn1.right + btnGutter, actY, rcBtn1.right + btnGutter + btnW, actY + actH };
    bool isP2Hover = (state->hovered_id == ID_ACT_APPLY_ALL_2B);
    DrawActionButton(hdcMem, rcBtn2, L"Aplicar Combo Gaming 2B", isP2Hover, CLR_ACCENT_GREEN, state->hFontSmallBold, ID_ACT_APPLY_ALL_2B, state);

    // Button 3: Revert Settings
    RECT rcBtn3 = { rcBtn2.right + btnGutter, actY, rcBtn2.right + btnGutter + btnW, actY + actH };
    bool isP3Hover = (state->hovered_id == ID_ACT_RESTORE_DEFAULTS);
    DrawActionButton(hdcMem, rcBtn3, L"Revertir Ajustes", isP3Hover, CLR_ACCENT_AMBER, state->hFontSmallBold, ID_ACT_RESTORE_DEFAULTS, state);

    // Button 4: Minimize to Tray
    RECT rcBtn4 = { rcBtn3.right + btnGutter, actY, contentRight, actY + actH };
    bool isP4Hover = (state->hovered_id == ID_ACT_MINIMIZE_TRAY);
    DrawActionButton(hdcMem, rcBtn4, L"Minimizar a Bandeja", isP4Hover, CLR_TEXT_MUTED, state->hFontSmallBold, ID_ACT_MINIMIZE_TRAY, state);

    // -------------------------------------------------------------------------
    // 5. Bottom Panel: Game Monitor / Real-Time Telemetry
    // -------------------------------------------------------------------------
    int botY = actY + actH + 10;
    RECT rcBotCard = { contentLeft, botY, contentRight, contentBottom };
    DrawRoundedCard(hdcMem, rcBotCard, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    if (state->data.game_active) {
        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 10, L"MONITOR DE JUEGO ACTIVO", CLR_ACCENT_CYAN, state->hFontBold);

        RECT rcFgBadge = { rcBotCard.right - 180, rcBotCard.top + 8, rcBotCard.right - 16, rcBotCard.top + 28 };
        DrawBadge(hdcMem, rcFgBadge, state->data.game_is_foreground ? L"PRIMER PLANO" : L"SEGUNDO PLANO",
                  state->data.game_is_foreground ? CLR_ACCENT_GREEN : CLR_TEXT_MUTED,
                  state->data.game_is_foreground ? RGB(20, 36, 28) : RGB(33, 38, 48),
                  CLR_CARD_BORDER, state->hFontSmallBold);

        std::wstring gTitle = L"Juego: " + Utf8ToWide(state->data.active_game_name) +
                              L" (PID: " + std::to_wstring(state->data.active_game_pid) + L")  |  API: " +
                              Utf8ToWide(state->data.active_game_api);
        if (state->data.running_steam_app_id != 0) {
            gTitle += L"  |  AppID: " + std::to_wstring(state->data.running_steam_app_id);
        }
        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 32, gTitle, CLR_TEXT_TITLE, state->hFontSmallBold);

        int chipW = (contentW - 32 - 2 * btnGutter) / 3;
        RECT rcCh1 = { rcBotCard.left + 16, rcBotCard.top + 54, rcBotCard.left + 16 + chipW, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh1, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh1.left + 10, rcCh1.top + 5, L"EcoQoS en 2º Plano", CLR_TEXT_MUTED, state->hFontSmall);
        wchar_t ecoBuf[64];
        swprintf_s(ecoBuf, L"Activo (%u procesos)", state->data.ecoqos_processes_count);
        DrawTextAt(hdcMem, rcCh1.left + 10, rcCh1.top + 20, ecoBuf, CLR_ACCENT_GREEN, state->hFontSmallBold);

        RECT rcCh2 = { rcCh1.right + btnGutter, rcBotCard.top + 54, rcCh1.right + btnGutter + chipW, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh2, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh2.left + 10, rcCh2.top + 5, L"Prioridad I/O", CLR_TEXT_MUTED, state->hFontSmall);
        DrawTextAt(hdcMem, rcCh2.left + 10, rcCh2.top + 20, state->data.io_priority_active ? L"IoPriorityHigh (3D)" : L"Estándar NT", state->data.io_priority_active ? CLR_ACCENT_CYAN : CLR_TEXT_MUTED, state->hFontSmallBold);

        RECT rcCh3 = { rcCh2.right + btnGutter, rcBotCard.top + 54, rcBotCard.right - 16, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh3, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh3.left + 10, rcCh3.top + 5, L"Presentación DXGI", CLR_TEXT_MUTED, state->hFontSmall);
        DrawTextAt(hdcMem, rcCh3.left + 10, rcCh3.top + 20, state->data.directflip_active ? L"DirectFlip 0ms (Zero-Copy)" : L"Flip Model Estándar", state->data.directflip_active ? CLR_ACCENT_GREEN : CLR_TEXT_MUTED, state->hFontSmallBold);

        std::wstring stLine = L"Estado: " + (state->data.last_action_status.empty() ? L"Telemetría en vivo activa" : Utf8ToWide(state->data.last_action_status));
        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 102, stLine, CLR_TEXT_MUTED, state->hFontSmall);
    } else {
        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 10, L"MONITOR DE SISTEMA EN ESPERA", CLR_ACCENT_CYAN, state->hFontBold);

        RECT rcWaitBadge = { rcBotCard.right - 160, rcBotCard.top + 8, rcBotCard.right - 16, rcBotCard.top + 28 };
        DrawBadge(hdcMem, rcWaitBadge, L"0% OVERHEAD", CLR_ACCENT_CYAN, RGB(20, 36, 44), CLR_CARD_BORDER, state->hFontSmallBold);

        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 32, L"Listo para detección automática en DirectX 11/12, Vulkan y títulos de Steam.", CLR_TEXT_TITLE, state->hFontSmallBold);

        int chipW = (contentW - 32 - 2 * btnGutter) / 3;
        RECT rcCh1 = { rcBotCard.left + 16, rcBotCard.top + 54, rcBotCard.left + 16 + chipW, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh1, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh1.left + 10, rcCh1.top + 5, L"Detección de Juegos", CLR_TEXT_MUTED, state->hFontSmall);
        DrawTextAt(hdcMem, rcCh1.left + 10, rcCh1.top + 20, L"Auto-Detección Activa", CLR_ACCENT_GREEN, state->hFontSmallBold);

        RECT rcCh2 = { rcCh1.right + btnGutter, rcBotCard.top + 54, rcCh1.right + btnGutter + chipW, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh2, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh2.left + 10, rcCh2.top + 5, L"Gestión de Memoria UMA", CLR_TEXT_MUTED, state->hFontSmall);
        DrawTextAt(hdcMem, rcCh2.left + 10, rcCh2.top + 20, L"Purga Standby Disponible", CLR_ACCENT_CYAN, state->hFontSmallBold);

        RECT rcCh3 = { rcCh2.right + btnGutter, rcBotCard.top + 54, rcBotCard.right - 16, rcBotCard.top + 94 };
        DrawRoundedCard(hdcMem, rcCh3, RGB(22, 25, 30), CLR_CARD_BORDER, 6);
        DrawTextAt(hdcMem, rcCh3.left + 10, rcCh3.top + 5, L"Consumo en Reposo", CLR_TEXT_MUTED, state->hFontSmall);
        DrawTextAt(hdcMem, rcCh3.left + 10, rcCh3.top + 20, L"0% CPU | Cero VRAM", CLR_ACCENT_GREEN, state->hFontSmallBold);

        std::wstring stLine = L"Última acción: " + (state->data.last_action_status.empty() ? L"Sistema sincronizado y listo para jugar." : Utf8ToWide(state->data.last_action_status));
        DrawTextAt(hdcMem, rcBotCard.left + 16, rcBotCard.top + 102, stLine, CLR_TEXT_MUTED, state->hFontSmall);
    }
}

// =============================================================================
// TAB 1: KERNEL & SYSTEM TWEAKS (MINIMALIST - 6 COMPACT CARDS WITH SWITCH)
// =============================================================================
void RenderTabKernel(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                     int contentLeft, int contentRight, int contentTop, int /*contentBottom*/, int /*contentW*/) {
    if (!state) return;

    // Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"AJUSTES DE KERNEL & SISTEMA", CLR_ACCENT_CYAN, state->hFontHeader);
    DrawTextAt(hdcMem, contentLeft, contentTop + 20, L"Optimización de baja latencia con conmutación dinámica instantánea", CLR_TEXT_MUTED, state->hFontSmall);

    struct KernelTweakDef {
        int id;
        const wchar_t* title;
        const wchar_t* purpose;
        const wchar_t* activeBadge;
        const wchar_t* inactiveBadge;
        bool isActive;
        COLORREF activeColor;
    };

    bool swTimer = state->data.timer_forced_manual || state->data.game_active || (state->data.timer_resolution_ms <= 0.55);
    bool swMpo = state->data.mpo_disabled;
    bool swShader = state->data.amd_shader_cache_forced;
    bool swVramOverride = state->data.amd_vram_override_active || state->data.amd_vram_reported_mb >= 2048;
    bool swMmcss = state->data.mmcss_gaming_optimized || state->data.mmcss_task_active;
    bool swRamLock = !state->data.mem_compression_enabled || state->data.kernel_paging_shield_active;
    bool swApuPower = state->data.apu_gpu_priority_active;

    uint32_t vramPoolMb = (state->data.system_info.apu_guard.safe_vram_pool_mb > 0)
        ? state->data.system_info.apu_guard.safe_vram_pool_mb
        : (state->data.amd_vram_reported_mb > 0 ? state->data.amd_vram_reported_mb : 4096);
    std::wstring vramTitle = L"4. VRAM Pool Override (" + std::to_wstring(vramPoolMb) + L" MB DXGI/AMD)";
    std::wstring vramActiveBadge = std::to_wstring(vramPoolMb) + L" MB UMA POOL";

    uint32_t stapmMhz = state->data.system_info.apu_guard.cpu_stapm_cap_mhz;
    std::wstring apuTitle;
    std::wstring apuPurpose;
    std::wstring apuActiveBadge;
    if (stapmMhz == 0) {
        apuTitle = L"7. Potencia APU Smart Boost (100% CPU + 100% GPU)";
        apuPurpose = L"4.0 GHz sin tope + EPP 25 + Ahorro en 2º plano (EcoQoS)";
        apuActiveBadge = L"100% SMART BOOST";
    } else {
        apuTitle = L"7. Control Térmico APU (" + std::to_wstring(stapmMhz) + L" MHz Sustained)";
        apuPurpose = L"Prioriza TDP continuo para gráficos integrados";
        apuActiveBadge = L"GPU-PRIORITY (" + std::to_wstring(stapmMhz) + L"M)";
    }

    const KernelTweakDef tweaks[7] = {
        {
            ID_ACT_TOGGLE_TIMER,
            L"1. Temporizador del Kernel (0.50 ms)",
            L"Tick submilisegundo para latencia ultra-baja",
            L"0.50 ms (ACTIVO)",
            L"15.62 ms (STOCK)",
            swTimer,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_MPO,
            L"2. Multi-Plane Overlay (MPO Anti-Flicker)",
            L"Elimina stuttering y parpadeos en pantalla",
            L"MPO DESACTIVADO",
            L"MPO ACTIVO (STOCK)",
            swMpo,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_SHADERCACHE,
            L"3. Boost Driver AMD (Shader Cache On)",
            L"Compilación continua y cola baja latencia",
            L"CACHE FORZADA",
            L"CACHE STOCK",
            swShader,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_VRAM_OVERRIDE,
            vramTitle.c_str(),
            L"Elimina pop-in de texturas y límite de 512 MB",
            vramActiveBadge.c_str(),
            L"512 MB STOCK",
            swVramOverride,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_MMCSS,
            L"5. MMCSS Prioridad GPU 8",
            L"Máxima prioridad en cola WDDM para juegos",
            L"GPU PRIO: 8",
            L"PRIO: STOCK (2)",
            swMmcss,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_MEMCOMP,
            L"6. RAM Lock (DisablePagingExecutive)",
            L"Bloquea el kernel en RAM física",
            L"RAM LOCK: ACTIVO",
            L"PAGING: STOCK",
            swRamLock,
            CLR_ACCENT_GREEN
        },
        {
            ID_ACT_TOGGLE_APU_POWER,
            apuTitle.c_str(),
            apuPurpose.c_str(),
            apuActiveBadge.c_str(),
            L"STOCK (4.0G)",
            swApuPower,
            CLR_ACCENT_GREEN
        }
    };

    int startY = contentTop + 44;
    int cardH = 58;
    int cardSpacing = 8;

    for (int i = 0; i < 7; ++i) {
        int cY = startY + i * (cardH + cardSpacing);
        RECT rcCard = { contentLeft, cY, contentRight, cY + cardH };

        DrawRoundedCard(hdcMem, rcCard, CLR_CARD_BG, CLR_CARD_BORDER, 8);

        // Tweak title
        DrawTextAt(hdcMem, rcCard.left + 16, rcCard.top + 10, tweaks[i].title, CLR_TEXT_TITLE, state->hFontBold);

        // Concise purpose (4-6 words)
        DrawTextAt(hdcMem, rcCard.left + 16, rcCard.top + 32, tweaks[i].purpose, CLR_TEXT_MUTED, state->hFontSmall);

        // Status badge to the left of the switch
        RECT rcBadge = { rcCard.right - 265, rcCard.top + (cardH - 24) / 2, rcCard.right - 105, rcCard.top + (cardH - 24) / 2 + 24 };
        if (tweaks[i].isActive) {
            DrawBadge(hdcMem, rcBadge, tweaks[i].activeBadge, tweaks[i].activeColor, RGB(20, 36, 28), tweaks[i].activeColor, state->hFontSmallBold);
        } else {
            DrawBadge(hdcMem, rcBadge, tweaks[i].inactiveBadge, CLR_TEXT_MUTED, RGB(33, 38, 48), CLR_CARD_BORDER, state->hFontSmallBold);
        }

        // Interactive ON/OFF toggle switch
        RECT rcSw = { rcCard.right - 90, rcCard.top + (cardH - 28) / 2, rcCard.right - 20, rcCard.top + (cardH - 28) / 2 + 28 };
        bool isHovered = (state->hovered_id == tweaks[i].id);
        DrawToggleSwitch(hdcMem, rcSw, tweaks[i].isActive, isHovered, tweaks[i].id, state);
    }
}

// =============================================================================
// TAB 2: UMA MEMORY & CPU CORES (WITH COMPACT BARS AND DIRECT BUTTON)
// =============================================================================
void RenderTabMemory(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                     int contentLeft, int contentRight, int contentTop, int contentBottom, int /*contentW*/) {
    if (!state) return;

    // Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"MEMORIA UNIFICADA UMA & ASIGNACIÓN DE NÚCLEOS", CLR_ACCENT_CYAN, state->hFontHeader);
    DrawTextAt(hdcMem, contentLeft, contentTop + 20, L"Gestión en tiempo real de Standby List y afinidad de procesos de fondo", CLR_TEXT_MUTED, state->hFontSmall);

    // -------------------------------------------------------------------------
    // 1. Top Card: UMA Memory Manager & Purge
    // -------------------------------------------------------------------------
    int topMemY = contentTop + 46;
    int topMemH = 138;
    RECT rcTopMem = { contentLeft, topMemY, contentRight, topMemY + topMemH };
    DrawRoundedCard(hdcMem, rcTopMem, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    DrawTextAt(hdcMem, rcTopMem.left + 16, rcTopMem.top + 12, L"GESTOR DE MEMORIA UNIFICADA (UMA)", CLR_ACCENT_CYAN, state->hFontBold);

    RECT rcDxgiBadge = { rcTopMem.right - 195, rcTopMem.top + 10, rcTopMem.right - 16, rcTopMem.top + 32 };
    wchar_t dxgiBadgeText[64];
    bool highPressure = (state->data.dxgi_pressure_ratio >= 0.85 || state->data.uma_guard_triggered_live);
    if (highPressure) {
        swprintf_s(dxgiBadgeText, L"ALERTA UMA: %.0f%% (>85%%)", state->data.dxgi_pressure_ratio * 100.0);
        DrawBadge(hdcMem, rcDxgiBadge, dxgiBadgeText, CLR_ACCENT_AMBER, RGB(36, 26, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    } else {
        swprintf_s(dxgiBadgeText, L"PRESIÓN DXGI: %.0f%% [OK]", state->data.dxgi_pressure_ratio * 100.0);
        DrawBadge(hdcMem, rcDxgiBadge, dxgiBadgeText, CLR_ACCENT_CYAN, RGB(20, 36, 44), CLR_ACCENT_CYAN, state->hFontSmallBold);
    }

    bool vramPoolActive = state->data.amd_vram_override_active || state->data.amd_vram_reported_mb >= 2048;
    uint32_t activePoolMb = state->data.amd_vram_reported_mb > 0 ? state->data.amd_vram_reported_mb : (vramPoolActive ? 4096 : 512);
    wchar_t poolBadgeText[64];
    swprintf_s(poolBadgeText, L"POOL TEXTURAS: %u MB", activePoolMb);
    RECT rcPoolBadge = { rcTopMem.right - 390, rcTopMem.top + 10, rcTopMem.right - 203, rcTopMem.top + 32 };
    if (vramPoolActive) {
        DrawBadge(hdcMem, rcPoolBadge, poolBadgeText, CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        DrawBadge(hdcMem, rcPoolBadge, poolBadgeText, CLR_TEXT_MUTED, RGB(33, 38, 48), CLR_CARD_BORDER, state->hFontSmallBold);
    }

    wchar_t mLine[256];
    swprintf_s(mLine, L"Total: %llu MB   |   Libre: %llu MB   |   Standby: %llu MB   |   VRAM Local: %llu / %llu MB   |   Pool: %u MB",
               static_cast<unsigned long long>(state->data.total_ram_mb),
               static_cast<unsigned long long>(state->data.free_ram_mb),
               static_cast<unsigned long long>(state->data.standby_ram_mb),
               static_cast<unsigned long long>(state->data.vram_local_used_mb),
               static_cast<unsigned long long>(state->data.vram_local_budget_mb),
               activePoolMb);
    DrawTextAt(hdcMem, rcTopMem.left + 16, rcTopMem.top + 38, mLine, CLR_TEXT_TITLE, state->hFontRegular);

    // Styled multi-segment progress bar
    RECT rcBar = { rcTopMem.left + 16, rcTopMem.top + 62, rcTopMem.right - 16, rcTopMem.top + 76 };
    double totalRam = static_cast<double>(state->data.total_ram_mb > 0 ? state->data.total_ram_mb : 16384);
    double freeFrac = static_cast<double>(state->data.free_ram_mb) / totalRam;
    double standbyFrac = static_cast<double>(state->data.standby_ram_mb) / totalRam;
    double modFrac = static_cast<double>(state->data.modified_ram_mb) / totalRam;
    double usedFrac = std::max<double>(0.0, 1.0 - (freeFrac + standbyFrac + modFrac));

    std::vector<std::pair<double, COLORREF>> segments = {
        { usedFrac, CLR_ACCENT_CYAN },
        { standbyFrac, CLR_ACCENT_PURPLE },
        { modFrac, CLR_ACCENT_AMBER },
        { freeFrac, CLR_ACCENT_GREEN }
    };
    DrawMultiSegmentBar(hdcMem, rcBar, segments, CLR_BAR_BG, CLR_CARD_BORDER);

    // Bottom line: last purge info + direct button
    int botRowY = rcTopMem.top + 88;
    if (state->data.last_purge_after_free_mb > 0) {
        wchar_t repBuf[256];
        swprintf_s(repBuf, L"Última purga: %llu MB libres  |  Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral 85%%)",
                   static_cast<unsigned long long>(state->data.last_purge_after_free_mb),
                   state->data.uma_guard_interventions,
                   state->data.dxgi_pressure_ratio * 100.0);
        DrawTextAt(hdcMem, rcTopMem.left + 16, botRowY + 10, repBuf, CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        wchar_t repBuf[256];
        swprintf_s(repBuf, L"Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral 85%%)",
                   state->data.uma_guard_interventions,
                   state->data.dxgi_pressure_ratio * 100.0);
        DrawTextAt(hdcMem, rcTopMem.left + 16, botRowY + 10, repBuf,
                   highPressure ? CLR_ACCENT_AMBER : CLR_TEXT_MUTED,
                   state->hFontSmall);
    }

    RECT rcPurgeBtn = { rcTopMem.right - 260, botRowY, rcTopMem.right - 16, botRowY + 36 };
    bool isPurgeHov = (state->hovered_id == ID_ACT_PURGE_RAM);
    DrawActionButton(hdcMem, rcPurgeBtn, L"VACIAR STANDBY RAM AHORA", isPurgeHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_PURGE_RAM, state);

    // -------------------------------------------------------------------------
    // 2. Bottom Card: CPU Visual Map & Process Isolation
    // -------------------------------------------------------------------------
    int botCpuY = topMemY + topMemH + 12;
    RECT rcBotCpu = { contentLeft, botCpuY, contentRight, contentBottom };
    DrawRoundedCard(hdcMem, rcBotCpu, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    DrawTextAt(hdcMem, rcBotCpu.left + 16, rcBotCpu.top + 12, L"MAPA DE TOPOLOGÍA CPU & AISLAMIENTO (CPU SETS)", CLR_ACCENT_CYAN, state->hFontBold);

    // Compact legend
    DrawTextAt(hdcMem, rcBotCpu.left + 16, rcBotCpu.top + 34,
               L"[ ■ Juego (Cian) ]    [ ■ Segundo Plano (Morado) ]    [ ■ DPC / ISR (Ámbar) ]    [ ■ Estándar ]",
               CLR_TEXT_MUTED, state->hFontSmall);

    uint32_t totalThreads = state->data.logical_processors > 0 ? state->data.logical_processors : 12;
    int threadsPerRow = std::min<uint32_t>(12, std::max<uint32_t>(6, (totalThreads + 1) / 2));
    int boxW = 86;
    int boxH = 46;
    int gapX = 10;
    int gapY = 8;
    int startX = rcBotCpu.left + 16;
    int startY = rcBotCpu.top + 56;

    uint32_t minSetId = UINT32_MAX;
    for (uint32_t gid : state->data.game_cpu_sets) {
        if (gid < minSetId) minSetId = gid;
    }
    for (uint32_t bid : state->data.background_cpu_sets) {
        if (bid < minSetId) minSetId = bid;
    }
    uint32_t baseSetId = (minSetId != UINT32_MAX && minSetId >= 256) ? 256 : 0;

    for (uint32_t t = 0; t < totalThreads; ++t) {
        int row = static_cast<int>(t / threadsPerRow);
        int col = static_cast<int>(t % threadsPerRow);
        RECT rcCoreBox = { startX + col * (boxW + gapX), startY + row * (boxH + gapY), startX + col * (boxW + gapX) + boxW, startY + row * (boxH + gapY) + boxH };

        uint32_t expectedSetId = baseSetId + t;
        bool isGameSet = false;
        for (uint32_t gid : state->data.game_cpu_sets) {
            if (gid == t || gid == expectedSetId) { isGameSet = true; break; }
        }
        bool isBgSet = false;
        for (uint32_t bid : state->data.background_cpu_sets) {
            if (bid == t || bid == expectedSetId) { isBgSet = true; break; }
        }

        COLORREF boxBorder;
        std::wstring roleLabel;

        if (isGameSet) {
            boxBorder = CLR_ACCENT_CYAN;
            roleLabel = L"JUEGO";
        } else if (isBgSet) {
            boxBorder = CLR_ACCENT_PURPLE;
            roleLabel = L"FONDO";
        } else if (t < 2) {
            boxBorder = CLR_ACCENT_AMBER;
            roleLabel = L"DPC / ISR";
        } else {
            boxBorder = CLR_CARD_BORDER;
            roleLabel = L"ESTANDAR";
        }

        DrawRoundedCard(hdcMem, rcCoreBox, CLR_BAR_BG, boxBorder, 6);

        wchar_t threadTitle[32];
        swprintf_s(threadTitle, L"Hilo #%u", t);
        DrawTextAt(hdcMem, rcCoreBox.left + 8, rcCoreBox.top + 6, threadTitle, CLR_TEXT_TITLE, state->hFontSmallBold);
        DrawTextAt(hdcMem, rcCoreBox.left + 8, rcCoreBox.top + 24, roleLabel, boxBorder, state->hFontSmallBold);
    }

    int afterBoxesY = startY + 2 * (boxH + gapY) + 12;
    wchar_t bgStatsBuf[256];
    swprintf_s(bgStatsBuf, L"Procesos en segundo plano detectados: %u   |   Procesos aislados en núcleos secundarios: %u",
               state->data.noisy_bg_processes_count, state->data.isolated_bg_processes_count);
    DrawTextAt(hdcMem, rcBotCpu.left + 16, afterBoxesY, bgStatsBuf, CLR_TEXT_BODY, state->hFontRegular);

    // Clean action buttons
    int actBtnsY = afterBoxesY + 24;
    RECT rcIsolateBtn = { rcBotCpu.left + 16, actBtnsY, rcBotCpu.left + 330, actBtnsY + 38 };
    bool isIsoHov = (state->hovered_id == ID_ACT_ISOLATE_BG);
    DrawActionButton(hdcMem, rcIsolateBtn, L"Aislar Procesos en 2º Plano", isIsoHov, CLR_ACCENT_PURPLE, state->hFontSmallBold, ID_ACT_ISOLATE_BG, state);

    RECT rcRestoreCpuBtn = { rcIsolateBtn.right + 12, actBtnsY, rcIsolateBtn.right + 310, actBtnsY + 38 };
    bool isRestHov = (state->hovered_id == ID_ACT_RESTORE_CPU);
    DrawActionButton(hdcMem, rcRestoreCpuBtn, L"Restaurar Afinidad de CPU", isRestHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_RESTORE_CPU, state);
}

// =============================================================================
// TAB 3: STEAM LIBRARY & ENGINES (COMPACT CARDS AND DIRECT BUTTONS)
// =============================================================================
void RenderTabSteam(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                    int contentLeft, int contentRight, int contentTop, int contentBottom, int /*contentW*/) {
    if (!state) return;

    // Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"BIBLIOTECA STEAM & MOTORES DE JUEGO", CLR_ACCENT_CYAN, state->hFontHeader);
    DrawTextAt(hdcMem, contentLeft, contentTop + 20, L"Optimización de archivos de configuración y capa Vulkan DXVK por título", CLR_TEXT_MUTED, state->hFontSmall);

    // Top control bar
    int topBarY = contentTop + 46;
    int topBarH = 42;
    RECT rcTopBar = { contentLeft, topBarY, contentRight, topBarY + topBarH };
    DrawRoundedCard(hdcMem, rcTopBar, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    wchar_t steamStatsBuf[160];
    std::wstring runningAppStr = (state->data.running_steam_app_id != 0)
        ? (std::to_wstring(state->data.running_steam_app_id) + L" (EN EJECUCIÓN)")
        : L"Ninguno (0)";
    swprintf_s(steamStatsBuf, L"Juegos en Biblioteca: %zu   |   Steam RunningAppID: %s",
               state->data.steam_games.size(), runningAppStr.c_str());
    DrawTextAt(hdcMem, rcTopBar.left + 16, rcTopBar.top + 12, steamStatsBuf, CLR_TEXT_TITLE, state->hFontSmallBold);

    RECT rcRescanBtn = { rcTopBar.right - 170, rcTopBar.top + 6, rcTopBar.right - 12, rcTopBar.bottom - 6 };
    bool isRescanHov = (state->hovered_id == ID_ACT_RESCAN_STEAM);
    DrawActionButton(hdcMem, rcRescanBtn, L"RE-ESCANEAR STEAM", isRescanHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_RESCAN_STEAM, state);

    int totalPages = static_cast<int>((state->data.steam_games.size() + 2) / 3);
    if (totalPages <= 0) totalPages = 1;
    if (state->steam_page >= totalPages) state->steam_page = totalPages - 1;
    if (state->steam_page < 0) state->steam_page = 0;

    if (state->data.steam_games.size() > 3) {
        RECT rcNextBtn = { rcRescanBtn.left - 90, rcTopBar.top + 6, rcRescanBtn.left - 8, rcTopBar.bottom - 6 };
        RECT rcPrevBtn = { rcNextBtn.left - 80, rcTopBar.top + 6, rcNextBtn.left - 8, rcTopBar.bottom - 6 };
        DrawActionButton(hdcMem, rcPrevBtn, L"◀ Ant", (state->hovered_id == ID_ACT_STEAM_PREV_PAGE), CLR_TEXT_MUTED, state->hFontSmall, ID_ACT_STEAM_PREV_PAGE, state);
        DrawActionButton(hdcMem, rcNextBtn, L"Sig ▶", (state->hovered_id == ID_ACT_STEAM_NEXT_PAGE), CLR_TEXT_MUTED, state->hFontSmall, ID_ACT_STEAM_NEXT_PAGE, state);

        wchar_t pageBuf[32];
        swprintf_s(pageBuf, L"Pág %d / %d", state->steam_page + 1, totalPages);
        DrawTextAt(hdcMem, rcPrevBtn.left - 75, rcTopBar.top + 12, pageBuf, CLR_ACCENT_CYAN, state->hFontSmallBold);
    }

    if (state->data.steam_games.empty()) {
        RECT rcEmptyCard = { contentLeft, topBarY + topBarH + 12, contentRight, contentBottom };
        DrawRoundedCard(hdcMem, rcEmptyCard, CLR_CARD_BG, CLR_CARD_BORDER, 8);

        DrawTextAt(hdcMem, rcEmptyCard.left + 24, rcEmptyCard.top + 24,
                   L"No se detectaron juegos instalados en las carpetas de biblioteca de Steam.",
                   CLR_TEXT_TITLE, state->hFontBold);

        DrawTextAt(hdcMem, rcEmptyCard.left + 24, rcEmptyCard.top + 52,
                   L"VegaFlow inspecciona automáticamente 'libraryfolders.vdf' y los archivos ACF para optimizar motores gráficos.",
                   CLR_TEXT_MUTED, state->hFontRegular);

        DrawTextAt(hdcMem, rcEmptyCard.left + 24, rcEmptyCard.top + 76,
                   L"Pulsa 'RE-ESCANEAR STEAM' para iniciar una búsqueda automática en discos secundarios.",
                   CLR_ACCENT_CYAN, state->hFontSmall);
    } else {
        size_t startIdx = static_cast<size_t>(state->steam_page * 3);
        int cardH = 110;
        int cardSpacing = 10;

        for (int idx = 0; idx < 3; ++idx) {
            size_t gIdx = startIdx + idx;
            if (gIdx >= state->data.steam_games.size()) break;
            const auto& game = state->data.steam_games[gIdx];

            int cardY = topBarY + topBarH + 12 + idx * (cardH + cardSpacing);
            RECT rcGameCard = { contentLeft, cardY, contentRight, cardY + cardH };
            DrawRoundedCard(hdcMem, rcGameCard, CLR_CARD_BG, CLR_CARD_BORDER, 8);

            // Game title
            std::wstring wGameName = Utf8ToWide(game.name);
            if (wGameName.length() > 36) wGameName = wGameName.substr(0, 33) + L"...";
            DrawTextAt(hdcMem, rcGameCard.left + 16, rcGameCard.top + 12, wGameName, CLR_TEXT_TITLE, state->hFontBold);

            // Badges in a compact row
            int badgeX = rcGameCard.left + 16;
            int badgeY = rcGameCard.top + 38;

            wchar_t appBadgeBuf[32];
            swprintf_s(appBadgeBuf, L"AppID: %u", game.app_id);
            RECT rcAppB = { badgeX, badgeY, badgeX + 95, badgeY + 22 };
            DrawBadge(hdcMem, rcAppB, appBadgeBuf, CLR_TEXT_MUTED, CLR_BAR_BG, CLR_CARD_BORDER, state->hFontSmall);

            std::wstring wEngine = Utf8ToWide(game.engine_name);
            RECT rcEngB = { rcAppB.right + 8, badgeY, rcAppB.right + 8 + 140, badgeY + 22 };
            DrawBadge(hdcMem, rcEngB, wEngine, CLR_ACCENT_CYAN, RGB(20, 32, 48), CLR_CARD_BORDER, state->hFontSmallBold);

            RECT rcAcB = { rcEngB.right + 8, badgeY, rcEngB.right + 8 + 200, badgeY + 22 };
            if (game.has_anticheat) {
                DrawBadge(hdcMem, rcAcB, L"Anti-Cheat: PROTEGIDO (Solo DXGI)", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
            } else {
                DrawBadge(hdcMem, rcAcB, L"Sin Anti-Cheat (Motor + DXGI)", CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmall);
            }

            // Subtitle: executable and status
            std::wstring exeLine = L"Ejecutable: " + Utf8ToWide(game.exe_name) + L"   |   Estado: " +
                (game.engine_preset_applied ? L"OPTIMIZADO (Preset UMA)" : L"STOCK");
            if (game.dxvk_installed) {
                exeLine += L"   |   DXVK Vulkan: ACTIVO";
            }
            DrawTextAt(hdcMem, rcGameCard.left + 16, rcGameCard.top + 70, exeLine, CLR_TEXT_MUTED, state->hFontSmall);

            // Direct interactive buttons on the right
            int btnW = 180;
            int btnH = 26;
            int btnRightX = rcGameCard.right - 16;
            int btnLeftX = btnRightX - btnW;

            // Button 1: DXVK Vulkan
            RECT rcDxvkBtn = { btnLeftX, rcGameCard.top + 10, btnRightX, rcGameCard.top + 10 + btnH };
            if (game.has_anticheat) {
                DrawBadge(hdcMem, rcDxvkBtn, L"DXVK BLOQUEADO (AC)", CLR_TEXT_MUTED, CLR_BTN_BG, CLR_CARD_BORDER, state->hFontSmall);
            } else {
                bool isDxvkHov = (state->hovered_id == (ID_ACT_STEAM_DXVK_0 + idx));
                if (game.dxvk_installed) {
                    DrawActionButton(hdcMem, rcDxvkBtn, L"VULKAN DXVK ON", isDxvkHov, CLR_ACCENT_GREEN, state->hFontSmallBold, ID_ACT_STEAM_DXVK_0 + idx, state);
                } else {
                    DrawActionButton(hdcMem, rcDxvkBtn, L"ACTIVAR DXVK", isDxvkHov, CLR_ACCENT_CYAN, state->hFontSmallBold, ID_ACT_STEAM_DXVK_0 + idx, state);
                }
            }

            // Button 2: Apply APU Preset
            RECT rcApplyBtn = { btnLeftX, rcGameCard.top + 42, btnRightX, rcGameCard.top + 42 + btnH };
            bool isApplyHov = (state->hovered_id == (ID_ACT_STEAM_APPLY_0 + idx));
            DrawActionButton(hdcMem, rcApplyBtn, L"Aplicar Preset APU", isApplyHov, CLR_ACCENT_GREEN, state->hFontSmallBold, ID_ACT_STEAM_APPLY_0 + idx, state);

            // Button 3: Restore Original
            RECT rcRestBtn = { btnLeftX, rcGameCard.top + 74, btnRightX, rcGameCard.top + 74 + btnH };
            bool isRestHov = (state->hovered_id == (ID_ACT_STEAM_RESTORE_0 + idx));
            DrawActionButton(hdcMem, rcRestBtn, L"Restaurar Backup (.bak)", isRestHov, CLR_ACCENT_AMBER, state->hFontSmall, ID_ACT_STEAM_RESTORE_0 + idx, state);
        }
    }
}

// =============================================================================
// TAB 4: LIVE CHANGE HISTORY (CLEAN MONOSPACE AUDIT LOG)
// =============================================================================
void RenderTabAuditLog(HDC hdcMem, const RECT& /*rcClient*/, GuiWindowState* state,
                       int contentLeft, int contentRight, int contentTop, int contentBottom, int /*contentW*/) {
    if (!state) return;

    // Clean top header
    DrawTextAt(hdcMem, contentLeft, contentTop, L"HISTORIAL DE CAMBIOS Y AUDITORÍA EN VIVO", CLR_ACCENT_CYAN, state->hFontHeader);
    DrawTextAt(hdcMem, contentLeft, contentTop + 20, L"Registro cronológico detallado de todas las modificaciones y eventos del sistema", CLR_TEXT_MUTED, state->hFontSmall);

    int logBoxY = contentTop + 46;
    int logBoxH = contentBottom - logBoxY - 52;
    RECT rcLogBox = { contentLeft, logBoxY, contentRight, logBoxY + logBoxH };

    // Dark terminal for logs
    DrawRoundedCard(hdcMem, rcLogBox, CLR_BAR_BG, CLR_CARD_BORDER, 8);

    RECT rcLogHeader = { rcLogBox.left + 14, rcLogBox.top + 8, rcLogBox.right - 14, rcLogBox.top + 28 };
    DrawTextRect(hdcMem, rcLogHeader, L"REGISTRO EN VIVO (AUDIT LOG):", CLR_ACCENT_CYAN, state->hFontSmallBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HPEN hPenLogDiv = CreatePen(PS_SOLID, 1, CLR_CARD_BORDER);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenLogDiv));
    MoveToEx(hdcMem, rcLogBox.left + 14, rcLogBox.top + 30, nullptr);
    LineTo(hdcMem, rcLogBox.right - 14, rcLogBox.top + 30);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenLogDiv);

    // Listing in monospace font
    int lineHeight = 18;
    int maxLines = std::max<int>(1, (logBoxH - 42) / lineHeight);
    int startIdx = 0;
    if (state->data.audit_log.size() > static_cast<size_t>(maxLines)) {
        startIdx = static_cast<int>(state->data.audit_log.size()) - maxLines;
    }

    int curY = rcLogBox.top + 38;
    if (state->data.audit_log.empty()) {
        DrawTextAt(hdcMem, rcLogBox.left + 16, curY, L"[00:00:00] Sesión inicializada. Monitoreo en espera.", CLR_TEXT_MUTED, state->hFontMonospace);
    } else {
        for (size_t i = startIdx; i < state->data.audit_log.size(); ++i) {
            const auto& item = state->data.audit_log[i];
            COLORREF textCol = CLR_TEXT_BODY;
            if (item.find("Fallo") != std::string::npos || item.find("Requiere") != std::string::npos || item.find("Error") != std::string::npos) {
                textCol = CLR_ACCENT_RED;
            } else if (item.find("Purga") != std::string::npos || item.find("exitosamente") != std::string::npos || item.find("Optimizado") != std::string::npos) {
                textCol = CLR_ACCENT_GREEN;
            }
            DrawTextAt(hdcMem, rcLogBox.left + 16, curY, Utf8ToWide(item), textCol, state->hFontMonospace);
            curY += lineHeight;
        }
    }

    // Bottom full-revert button
    RECT rcRestoreBtn = { contentLeft, contentBottom - 44, contentRight, contentBottom };
    bool isRestHov = (state->hovered_id == ID_ACT_RESTORE_DEFAULTS);
    DrawActionButton(hdcMem, rcRestoreBtn, L"REVERTIR TODOS LOS AJUSTES A VALORES ORIGINALES DE WINDOWS", isRestHov, CLR_ACCENT_AMBER, state->hFontBold, ID_ACT_RESTORE_DEFAULTS, state);
}

} // namespace vegaflow
