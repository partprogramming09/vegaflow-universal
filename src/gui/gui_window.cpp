#include "gui_window.hpp"
#include "gui_components.hpp"

#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace vegaflow {

namespace {

// =============================================================================
// Pintado Principal del Dashboard (Sidebar + Despacho de Pestañas + Barra de Estado)
// =============================================================================
void PaintDashboard(HDC hdcMem, const RECT& rcClient, GuiWindowState* state) {
    if (!state) return;

    state->hit_boxes.clear();

    // 1. Fondo general de la ventana
    HBRUSH hbrMainBg = CreateSolidBrush(CLR_BG_MAIN);
    FillRect(hdcMem, &rcClient, hbrMainBg);
    DeleteObject(hbrMainBg);

    SetBkMode(hdcMem, TRANSPARENT);

    // =========================================================================
    // 2. Menú Lateral Izquierdo (Sidebar de 250 px)
    // =========================================================================
    RECT rcSidebar = { 0, 0, SIDEBAR_WIDTH, rcClient.bottom - STATUSBAR_HEIGHT };
    HBRUSH hbrSidebar = CreateSolidBrush(CLR_BG_SIDEBAR);
    FillRect(hdcMem, &rcSidebar, hbrSidebar);
    DeleteObject(hbrSidebar);

    // Divisor lateral vertical
    HPEN hPenSidebarDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenSidebarDiv));
    MoveToEx(hdcMem, SIDEBAR_WIDTH, 0, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH, rcClient.bottom - STATUSBAR_HEIGHT);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenSidebarDiv);

    // Logotipo y título en el sidebar
    DrawTextAt(hdcMem, 22, 18, L"VEGAFLOW", CLR_ACCENT_CYAN, state->hFontTitle);
    DrawTextAt(hdcMem, 22, 44, L"UNIVERSAL TUNING", CLR_TEXT_MUTED, state->hFontSmall);

    RECT rcChip = { 158, 22, 234, 42 };
    DrawBadge(hdcMem, rcChip, L"v1.2 UMA", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);

    // Línea divisoria bajo el logo
    HPEN hPenHeadDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenHeadDiv));
    MoveToEx(hdcMem, 16, 70, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH - 16, 70);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenHeadDiv);

    // 6 Botones de pestaña en el menú lateral
    const wchar_t* tabTitles[6] = {
        L"1. Dashboard & Telemetría",
        L"2. Ajustes Kernel & Sistema",
        L"3. Memoria UMA & Núcleos CPU",
        L"4. Escalador FSR 1.0 & DXGI",
        L"5. Biblioteca Steam & Motores",
        L"6. Historial de Cambios (Log)"
    };

    for (int i = 0; i < 6; ++i) {
        int tabY = 78 + i * 44;
        RECT rcTab = { 12, tabY, SIDEBAR_WIDTH - 12, tabY + 38 };
        bool isActive = (state->current_tab == i);
        bool isHovered = (state->hovered_id == (ID_TAB_0 + i));
        DrawTabButton(hdcMem, rcTab, tabTitles[i], isActive, isHovered, ID_TAB_0 + i, state);
    }

    // Parte inferior del sidebar (UAC, bandeja, consumo de memoria)
    int sideBottomY = rcClient.bottom - STATUSBAR_HEIGHT;
    HPEN hPenSideLowDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenSideLowDiv));
    MoveToEx(hdcMem, 16, sideBottomY - 145, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH - 16, sideBottomY - 145);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenSideLowDiv);

    // Estado UAC
    RECT rcElevBadge = { 14, sideBottomY - 136, SIDEBAR_WIDTH - 14, sideBottomY - 110 };
    if (state->data.is_elevated) {
        DrawBadge(hdcMem, rcElevBadge, L"ADMINISTRADOR", CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        DrawBadge(hdcMem, rcElevBadge, L"USUARIO ESTÁNDAR", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    }

    // Botón Minimizar a Bandeja
    RECT rcTrayBtn = { 14, sideBottomY - 102, SIDEBAR_WIDTH - 14, sideBottomY - 76 };
    bool isTrayHover = (state->hovered_id == ID_ACT_MINIMIZE_TRAY);
    DrawActionButton(hdcMem, rcTrayBtn, L"Minimizar a Bandeja", isTrayHover, CLR_ACCENT_CYAN, state->hFontSmall, ID_ACT_MINIMIZE_TRAY, state);

    // Consumo del proceso demonio
    wchar_t wsBuf[64];
    swprintf_s(wsBuf, L"Daemon WS: %llu KB", static_cast<unsigned long long>(state->data.daemon_private_ws_kb));
    DrawTextAt(hdcMem, 16, sideBottomY - 68, wsBuf, CLR_TEXT_MUTED, state->hFontSmall);
    DrawTextAt(hdcMem, 16, sideBottomY - 50, L"Auto-Suspensión: ACTIVA", CLR_ACCENT_CYAN, state->hFontSmall);

    // =========================================================================
    // 3. Área de Contenido Principal (Despacho Modular de Pestañas)
    // =========================================================================
    int padX = 16;
    int padY = 16;
    int contentLeft   = SIDEBAR_WIDTH + padX;
    int contentRight  = rcClient.right - padX;
    int contentTop    = padY;
    int contentBottom = rcClient.bottom - STATUSBAR_HEIGHT - padY;
    int contentW      = contentRight - contentLeft;

    switch (state->current_tab) {
        case 0:
            RenderTabDashboard(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 1:
            RenderTabKernel(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 2:
            RenderTabMemory(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 3:
            RenderTabScaler(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 4:
            RenderTabSteam(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 5:
            RenderTabAuditLog(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        default:
            RenderTabDashboard(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
    }

    // =========================================================================
    // 4. Barra de Estado Inferior (Status Bar)
    // =========================================================================
    RECT rcStatusBar = { 0, rcClient.bottom - STATUSBAR_HEIGHT, rcClient.right, rcClient.bottom };
    HBRUSH hbrStatus = CreateSolidBrush(CLR_STATUSBAR_BG);
    FillRect(hdcMem, &rcStatusBar, hbrStatus);
    DeleteObject(hbrStatus);

    HPEN hPenStatusDiv = CreatePen(PS_SOLID, 1, CLR_CARD_BORDER);
    hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenStatusDiv));
    MoveToEx(hdcMem, 0, rcClient.bottom - STATUSBAR_HEIGHT, nullptr);
    LineTo(hdcMem, rcClient.right, rcClient.bottom - STATUSBAR_HEIGHT);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenStatusDiv);

    // Indicador circular verde
    HBRUSH hbrDot = CreateSolidBrush(CLR_ACCENT_GREEN);
    HPEN hPenDot = CreatePen(PS_SOLID, 1, CLR_ACCENT_GREEN);
    HBRUSH hOldDotBr = static_cast<HBRUSH>(SelectObject(hdcMem, hbrDot));
    HPEN hOldDotPen = static_cast<HPEN>(SelectObject(hdcMem, hPenDot));
    Ellipse(hdcMem, 16, rcClient.bottom - 21, 24, rcClient.bottom - 13);
    SelectObject(hdcMem, hOldDotBr);
    SelectObject(hdcMem, hOldDotPen);
    DeleteObject(hbrDot);
    DeleteObject(hPenDot);

    std::wstring statusText = state->data.last_action_status.empty()
                                ? L"Sistema activo - Monitoreo en tiempo real (Cero VRAM)"
                                : Utf8ToWide(state->data.last_action_status);
    DrawTextAt(hdcMem, 32, rcClient.bottom - 23, statusText, CLR_TEXT_TITLE, state->hFontSmall);

    RECT rcApuBtn = { rcClient.right - 460, rcClient.bottom - 26, rcClient.right - 295, rcClient.bottom - 6 };
    bool isApuHov = (state->hovered_id == ID_ACT_TOGGLE_APU_POWER);
    std::wstring apuBtnText = state->data.apu_gpu_priority_active ? L"APU: GPU-PRIORITY" : L"APU: BALANCEADO";
    COLORREF apuColor = state->data.apu_gpu_priority_active ? CLR_ACCENT_GREEN : CLR_ACCENT_AMBER;
    DrawActionButton(hdcMem, rcApuBtn, apuBtnText, isApuHov, apuColor, state->hFontSmallBold, ID_ACT_TOGGLE_APU_POWER, state);

    DrawTextAt(hdcMem, rcClient.right - 280, rcClient.bottom - 23, L"VegaFlow Universal | Win32 GDI | Zero VRAM", CLR_TEXT_MUTED, state->hFontSmall);
}

// =============================================================================
// Despacho de Acciones Interactivas (Manejo de Clics en Hit-Boxes)
// =============================================================================
void HandleActionClick(HWND hwnd, GuiWindowState* state, int id) {
    if (!state) return;

    // Conmutación de pestaña
    if (id >= ID_TAB_0 && id <= ID_TAB_5) {
        state->current_tab = id - ID_TAB_0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    std::string res;
    switch (id) {
        case ID_ACT_TOGGLE_APU_POWER: {
            if (state->callbacks.on_ToggleApuPowerMode) {
                res = state->callbacks.on_ToggleApuPowerMode();
                state->data.apu_gpu_priority_active = !state->data.apu_gpu_priority_active;
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_APPLY_PRESET: {
            if (state->callbacks.on_ToggleTimerForce && !state->data.timer_forced_manual) {
                state->callbacks.on_ToggleTimerForce();
            }
            if (state->callbacks.on_ToggleMpo && !state->data.mpo_disabled) {
                state->callbacks.on_ToggleMpo();
            }
            if (state->callbacks.on_ToggleShaderCache && !state->data.amd_shader_cache_forced) {
                state->callbacks.on_ToggleShaderCache();
            }
            if (state->callbacks.on_ToggleMmcssGaming && !state->data.mmcss_gaming_optimized) {
                state->callbacks.on_ToggleMmcssGaming();
            }
            if (state->callbacks.on_PurgeNow) {
                res = state->callbacks.on_PurgeNow();
            }
            if (state->callbacks.on_IsolateBackgroundNow) {
                state->callbacks.on_IsolateBackgroundNow();
            }
            if (res.empty()) res = "Preset óptimo aplicado exitosamente (Timer, MPO, ShaderCache, MMCSS, Purga, CPU Sets)";
            state->data.last_action_status = res;
            break;
        }

        case ID_ACT_PURGE_RAM: {
            if (state->callbacks.on_PurgeNow) {
                res = state->callbacks.on_PurgeNow();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_APPLY_ALL_2B: {
            if (state->callbacks.on_ApplyAllGamingTweaks) {
                res = state->callbacks.on_ApplyAllGamingTweaks();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_TIMER: {
            if (state->callbacks.on_ToggleTimerForce) {
                res = state->callbacks.on_ToggleTimerForce();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_MPO: {
            if (state->callbacks.on_ToggleMpo) {
                res = state->callbacks.on_ToggleMpo();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_SHADERCACHE: {
            if (state->callbacks.on_ToggleShaderCache) {
                res = state->callbacks.on_ToggleShaderCache();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_VRAM_OVERRIDE: {
            if (state->callbacks.on_ToggleVramOverride) {
                res = state->callbacks.on_ToggleVramOverride();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_MMCSS: {
            if (state->callbacks.on_ToggleMmcssGaming) {
                res = state->callbacks.on_ToggleMmcssGaming();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_MEMCOMP: {
            if (state->callbacks.on_ToggleMemCompression) {
                res = state->callbacks.on_ToggleMemCompression();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_ISOLATE_BG: {
            if (state->callbacks.on_IsolateBackgroundNow) {
                res = state->callbacks.on_IsolateBackgroundNow();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_RESTORE_CPU: {
            if (state->callbacks.on_RestoreCpuAffinity) {
                res = state->callbacks.on_RestoreCpuAffinity();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_TOGGLE_FSR: {
            if (state->callbacks.on_ToggleFsr) {
                res = state->callbacks.on_ToggleFsr();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_MODE_0: {
            if (state->callbacks.on_SetFsrMode) {
                res = state->callbacks.on_SetFsrMode(0);
                state->data.fsr_mode = 0;
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_MODE_1: {
            if (state->callbacks.on_SetFsrMode) {
                res = state->callbacks.on_SetFsrMode(1);
                state->data.fsr_mode = 1;
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_MODE_2: {
            if (state->callbacks.on_SetFsrMode) {
                res = state->callbacks.on_SetFsrMode(2);
                state->data.fsr_mode = 2;
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_SHARP_10: {
            if (state->callbacks.on_SetFsrSharpness) {
                res = state->callbacks.on_SetFsrSharpness(0.10f);
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_SHARP_20: {
            if (state->callbacks.on_SetFsrSharpness) {
                res = state->callbacks.on_SetFsrSharpness(0.20f);
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_SHARP_40: {
            if (state->callbacks.on_SetFsrSharpness) {
                res = state->callbacks.on_SetFsrSharpness(0.40f);
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_FSR_SHARP_70: {
            if (state->callbacks.on_SetFsrSharpness) {
                res = state->callbacks.on_SetFsrSharpness(0.70f);
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_RESTORE_DEFAULTS: {
            if (state->callbacks.on_RestoreAllDefaults) {
                res = state->callbacks.on_RestoreAllDefaults();
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_RESCAN_STEAM: {
            if (state->callbacks.on_RescanSteamLibrary) {
                res = state->callbacks.on_RescanSteamLibrary();
                state->steam_page = 0;
                state->data.last_action_status = res;
            }
            break;
        }

        case ID_ACT_STEAM_PREV_PAGE: {
            if (state->steam_page > 0) {
                state->steam_page--;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case ID_ACT_STEAM_NEXT_PAGE: {
            int totalPages = static_cast<int>((state->data.steam_games.size() + 2) / 3);
            if (state->steam_page + 1 < totalPages) {
                state->steam_page++;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }

        case ID_ACT_STEAM_APPLY_0:
        case ID_ACT_STEAM_APPLY_1:
        case ID_ACT_STEAM_APPLY_2: {
            size_t slotIdx = static_cast<size_t>(id - ID_ACT_STEAM_APPLY_0);
            size_t gIdx = static_cast<size_t>(state->steam_page * 3) + slotIdx;
            if (gIdx < state->data.steam_games.size()) {
                uint32_t appId = state->data.steam_games[gIdx].app_id;
                if (state->callbacks.on_ApplyGameEnginePreset) {
                    res = state->callbacks.on_ApplyGameEnginePreset(appId);
                    state->data.last_action_status = res;
                }
            }
            break;
        }

        case ID_ACT_STEAM_RESTORE_0:
        case ID_ACT_STEAM_RESTORE_1:
        case ID_ACT_STEAM_RESTORE_2: {
            size_t slotIdx = static_cast<size_t>(id - ID_ACT_STEAM_RESTORE_0);
            size_t gIdx = static_cast<size_t>(state->steam_page * 3) + slotIdx;
            if (gIdx < state->data.steam_games.size()) {
                uint32_t appId = state->data.steam_games[gIdx].app_id;
                if (state->callbacks.on_RestoreGameEngineConfig) {
                    res = state->callbacks.on_RestoreGameEngineConfig(appId);
                    state->data.last_action_status = res;
                }
            }
            break;
        }

        case ID_ACT_STEAM_DXVK_0:
        case ID_ACT_STEAM_DXVK_1:
        case ID_ACT_STEAM_DXVK_2: {
            size_t slotIdx = static_cast<size_t>(id - ID_ACT_STEAM_DXVK_0);
            size_t gIdx = static_cast<size_t>(state->steam_page * 3) + slotIdx;
            if (gIdx < state->data.steam_games.size()) {
                uint32_t appId = state->data.steam_games[gIdx].app_id;
                if (state->callbacks.on_ToggleGameDxvk) {
                    res = state->callbacks.on_ToggleGameDxvk(appId);
                    state->data.last_action_status = res;
                }
            }
            break;
        }

        case ID_ACT_ELEVATE: {
            if (state->callbacks.on_RequestElevate) {
                if (state->callbacks.on_RequestElevate()) {
                    DestroyWindow(hwnd);
                    return;
                } else {
                    state->data.last_action_status = "Elevación UAC cancelada o no disponible";
                }
            }
            break;
        }

        case ID_ACT_MINIMIZE_TRAY: {
            ShowWindow(hwnd, SW_HIDE);
            SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
            return;
        }

        default:
            break;
    }

    if (state->callbacks.on_PollTelemetry) {
        state->data = state->callbacks.on_PollTelemetry();
        if (!res.empty()) {
            state->data.last_action_status = res;
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// =============================================================================
// Procedimiento de Ventana Win32 (DashboardWndProc)
// =============================================================================
LRESULT CALLBACK DashboardWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<GuiWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE: {
            state = reinterpret_cast<GuiWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state) return -1;

            // Fuentes Segoe UI con ClearType
            state->hFontTitle = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontHeader = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontRegular = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontSmallBold = CreateFontW(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontMetric = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            state->hFontMonospace = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

            // Atajo global Alt + F11 para FSR 1.0
            RegisterHotKey(hwnd, ID_HOTKEY_FSR, MOD_ALT, VK_F11);

            // Icono en System Tray
            state->nid.cbSize = sizeof(NOTIFYICONDATAW);
            state->nid.hWnd = hwnd;
            state->nid.uID = ID_TRAY_ICON;
            state->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            state->nid.uCallbackMessage = WM_APP_TRAYICON;
            state->nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            wcscpy_s(state->nid.szTip, L"VegaFlow Universal Optimizer - UXTU Dashboard");
            state->tray_registered = (Shell_NotifyIconW(NIM_ADD, &state->nid) == TRUE);

            // Temporizador de telemetría de 1500 ms
            SetTimer(hwnd, TIMER_TELEMETRY_ID, TIMER_TELEMETRY_INTERVAL, nullptr);

            ChangeWindowMessageFilterEx(hwnd, WM_COMMAND, MSGFLT_ALLOW, nullptr);

            // Consulta inicial
            if (state->callbacks.on_PollTelemetry) {
                state->data = state->callbacks.on_PollTelemetry();
            }

            return 0;
        }

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 960;
            mmi->ptMinTrackSize.y = 640;
            return 0;
        }

        case WM_SIZE: {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // WM_PAINT maneja todo con doble búfer

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rc.right - rc.left, rc.bottom - rc.top);
            HBITMAP hbmOld = static_cast<HBITMAP>(SelectObject(hdcMem, hbmMem));

            PaintDashboard(hdcMem, rc, state);

            BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hbmOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!state) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int new_hovered = 0;
            for (const auto& hb : state->hit_boxes) {
                if (PtInRect(&hb.rect, pt)) {
                    new_hovered = hb.id;
                    break;
                }
            }

            if (new_hovered != state->hovered_id) {
                state->hovered_id = new_hovered;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(TRACKMOUSEEVENT);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (state && state->hovered_id != 0) {
                state->hovered_id = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!state) return 0;
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int clicked_id = 0;
            for (const auto& hb : state->hit_boxes) {
                if (PtInRect(&hb.rect, pt)) {
                    clicked_id = hb.id;
                    break;
                }
            }

            if (clicked_id != 0) {
                HandleActionClick(hwnd, state, clicked_id);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_TELEMETRY_ID && state) {
                if (state->callbacks.on_PollTelemetry) {
                    std::string prevStatus = state->data.last_action_status;
                    state->data = state->callbacks.on_PollTelemetry();
                    if (state->data.last_action_status.empty()) {
                        state->data.last_action_status = prevStatus;
                    }

                    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
                        return 0;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_HOTKEY: {
            if (wParam == ID_HOTKEY_FSR && state) {
                if (state->callbacks.on_ToggleFsr) {
                    std::string res = state->callbacks.on_ToggleFsr();
                    state->data.last_action_status = res;
                    if (state->callbacks.on_PollTelemetry) {
                        state->data = state->callbacks.on_PollTelemetry();
                        if (!res.empty()) state->data.last_action_status = res;
                    }
                    if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }

        case WM_APP_TRAYICON: {
            if (LOWORD(lParam) == WM_LBUTTONUP || LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                if (IsIconic(hwnd)) {
                    ShowWindow(hwnd, SW_RESTORE);
                } else {
                    ShowWindow(hwnd, SW_SHOW);
                }
                SetForegroundWindow(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Restaurar Dashboard UXTU");
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_PURGE, L"Purgar Standby RAM Ahora");
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_FSR, (state && state->data.fsr_active) ? L"Desactivar FSR 1.0 (Alt+F11)" : L"Activar FSR 1.0 (Alt+F11)");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Cerrar VegaFlow Universal");
                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(hMenu);
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (!state) break;

            switch (wmId) {
                case ID_TRAY_RESTORE: {
                    if (IsIconic(hwnd)) {
                        ShowWindow(hwnd, SW_RESTORE);
                    } else {
                        ShowWindow(hwnd, SW_SHOW);
                    }
                    SetForegroundWindow(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    break;
                }
                case ID_TRAY_PURGE: {
                    if (state->callbacks.on_PurgeNow) {
                        std::string res = state->callbacks.on_PurgeNow();
                        state->data.last_action_status = res;
                        if (state->callbacks.on_PollTelemetry) {
                            state->data = state->callbacks.on_PollTelemetry();
                            if (!res.empty()) state->data.last_action_status = res;
                        }
                    }
                    break;
                }
                case ID_TRAY_FSR: {
                    if (state->callbacks.on_ToggleFsr) {
                        std::string res = state->callbacks.on_ToggleFsr();
                        state->data.last_action_status = res;
                        if (state->callbacks.on_PollTelemetry) {
                            state->data = state->callbacks.on_PollTelemetry();
                            if (!res.empty()) state->data.last_action_status = res;
                        }
                        if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
                            InvalidateRect(hwnd, nullptr, FALSE);
                        }
                    }
                    break;
                }
                case ID_TRAY_EXIT: {
                    DestroyWindow(hwnd);
                    break;
                }
                default:
                    break;
            }
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) != WA_INACTIVE) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        case WM_QUERYENDSESSION: {
            return TRUE;
        }

        case WM_ENDSESSION: {
            if (wParam == TRUE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (state) {
                UnregisterHotKey(hwnd, ID_HOTKEY_FSR);
                KillTimer(hwnd, TIMER_TELEMETRY_ID);

                if (state->tray_registered) {
                    Shell_NotifyIconW(NIM_DELETE, &state->nid);
                    state->tray_registered = false;
                }

                if (state->hFontTitle)     DeleteObject(state->hFontTitle);
                if (state->hFontHeader)    DeleteObject(state->hFontHeader);
                if (state->hFontRegular)   DeleteObject(state->hFontRegular);
                if (state->hFontBold)      DeleteObject(state->hFontBold);
                if (state->hFontSmall)     DeleteObject(state->hFontSmall);
                if (state->hFontSmallBold) DeleteObject(state->hFontSmallBold);
                if (state->hFontMetric)    DeleteObject(state->hFontMetric);
                if (state->hFontMonospace) DeleteObject(state->hFontMonospace);
            }
            PostQuitMessage(0);
            return 0;
        }

        default:
            break;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

} // namespace

int GuiWindow::RunGuiLoop(const GuiCallbacks& callbacks) {
    // High-DPI V2 si está disponible
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        using SetProcessDpiAwarenessContextProc = BOOL (WINAPI *)(HANDLE);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto pfnSetDpi = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (pfnSetDpi) {
            pfnSetDpi(reinterpret_cast<HANDLE>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        }
    }

    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const wchar_t CLASS_NAME[] = L"VegaFlowUniversalUxtuClass";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = DashboardWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszMenuName  = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm       = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

    RegisterClassExW(&wc);

    // Ventana amplia de 1120 x 740 centrada
    RECT rc = { 0, 0, 1120, 740 };
    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRect(&rc, dwStyle, FALSE);

    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = std::max<int>(0, (screenW - winWidth) / 2);
    int posY = std::max<int>(0, (screenH - winHeight) / 2);

    GuiWindowState state;
    state.callbacks = callbacks;

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"VegaFlow Universal Optimizer - Universal x86 Tuning Utility (UXTU)",
        dwStyle,
        posX, posY, winWidth, winHeight,
        nullptr,
        nullptr,
        hInstance,
        &state
    );

    if (!hwnd) {
        UnregisterClassW(CLASS_NAME, hInstance);
        return -1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Bucle de mensajes estándar
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(CLASS_NAME, hInstance);
    return static_cast<int>(msg.wParam);
}

} // namespace vegaflow
