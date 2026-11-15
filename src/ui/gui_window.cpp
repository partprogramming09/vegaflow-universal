#include "ui/gui_window.hpp"
#include "ui/app_identity.hpp"
#include "ui/gui_components.hpp"
#include "ui/config_view.hpp"
#include "ui/programs_view.hpp"
#include "ui/equipment_view.hpp"
#include "ui/requirements_view.hpp"
#include "ui/games_view.hpp"
#include "ui/history_view.hpp"
#include "ui/home_view.hpp"
#include "ui/settings_view.hpp"

#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace vegaflow {

namespace {

// =============================================================================
// Main Dashboard painting (Sidebar + Tab Dispatch + Status Bar)
// =============================================================================
void PaintDashboard(HDC hdcMem, const RECT& rcClient, GuiWindowState* state) {
    if (!state) return;

    state->hit_boxes.clear();

    // 1. Overall window background
    HBRUSH hbrMainBg = CreateSolidBrush(CLR_BG_MAIN);
    FillRect(hdcMem, &rcClient, hbrMainBg);
    DeleteObject(hbrMainBg);

    SetBkMode(hdcMem, TRANSPARENT);

    // =========================================================================
    // 2. Left Side Menu (250 px sidebar)
    // =========================================================================
    RECT rcSidebar = { 0, 0, SIDEBAR_WIDTH, rcClient.bottom - STATUSBAR_HEIGHT };
    HBRUSH hbrSidebar = CreateSolidBrush(CLR_BG_SIDEBAR);
    FillRect(hdcMem, &rcSidebar, hbrSidebar);
    DeleteObject(hbrSidebar);

    // Vertical side divider
    HPEN hPenSidebarDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenSidebarDiv));
    MoveToEx(hdcMem, SIDEBAR_WIDTH, 0, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH, rcClient.bottom - STATUSBAR_HEIGHT);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenSidebarDiv);

    // Logo and title in the sidebar
    DrawTextAt(hdcMem, 22, 18, L"VEGAFLOW", CLR_ACCENT_CYAN, state->hFontTitle);
    DrawTextAt(hdcMem, 22, 44, L"UNIVERSAL TUNING", CLR_TEXT_MUTED, state->hFontSmall);

    RECT rcChip = { 158, 22, 234, 42 };
    DrawBadge(hdcMem, rcChip, L"v" + Utf8ToWide(ui::kVersion), CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);

    // Divider line under the logo
    HPEN hPenHeadDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenHeadDiv));
    MoveToEx(hdcMem, 16, 70, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH - 16, 70);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenHeadDiv);

    // 8 tab buttons in the side menu
    const wchar_t* tabTitles[8] = {
        L"1. Inicio",
        L"2. Ajustes del sistema",
        L"3. Memoria UMA & Núcleos CPU",
        L"4. Juegos",
        L"5. Historial",
        L"6. Configuración",
        L"7. Programas",
        L"8. Tu equipo"
    };

    for (int i = 0; i < 8; ++i) {
        int tabY = 78 + i * 44;
        RECT rcTab = { 12, tabY, SIDEBAR_WIDTH - 12, tabY + 38 };
        bool isActive = (state->current_tab == i) || (i == 5 && state->current_tab == 8); // Requisitos hangs from Configuración
        bool isHovered = (state->hovered_id == (ID_TAB_0 + i));
        DrawTabButton(hdcMem, rcTab, tabTitles[i], isActive, isHovered, ID_TAB_0 + i, state);
    }

    // Bottom of the sidebar (UAC, tray, memory usage)
    int sideBottomY = rcClient.bottom - STATUSBAR_HEIGHT;
    HPEN hPenSideLowDiv = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
    hOldPen = static_cast<HPEN>(SelectObject(hdcMem, hPenSideLowDiv));
    MoveToEx(hdcMem, 16, sideBottomY - 145, nullptr);
    LineTo(hdcMem, SIDEBAR_WIDTH - 16, sideBottomY - 145);
    SelectObject(hdcMem, hOldPen);
    DeleteObject(hPenSideLowDiv);

    // UAC status
    RECT rcElevBadge = { 14, sideBottomY - 136, SIDEBAR_WIDTH - 14, sideBottomY - 110 };
    if (state->data.is_elevated) {
        DrawBadge(hdcMem, rcElevBadge, L"ADMINISTRADOR", CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, state->hFontSmallBold);
    } else {
        DrawBadge(hdcMem, rcElevBadge, L"USUARIO ESTÁNDAR", CLR_ACCENT_AMBER, RGB(36, 30, 20), CLR_ACCENT_AMBER, state->hFontSmallBold);
    }

    // Minimize to Tray button
    RECT rcTrayBtn = { 14, sideBottomY - 102, SIDEBAR_WIDTH - 14, sideBottomY - 76 };
    bool isTrayHover = (state->hovered_id == ID_ACT_MINIMIZE_TRAY);
    DrawActionButton(hdcMem, rcTrayBtn, L"Minimizar a Bandeja", isTrayHover, CLR_ACCENT_CYAN, state->hFontSmall, ID_ACT_MINIMIZE_TRAY, state);

    // Daemon process usage
    wchar_t wsBuf[64];
    swprintf_s(wsBuf, L"Daemon WS: %llu KB", static_cast<unsigned long long>(state->data.daemon_private_ws_kb));
    DrawTextAt(hdcMem, 16, sideBottomY - 68, wsBuf, CLR_TEXT_MUTED, state->hFontSmall);
    DrawTextAt(hdcMem, 16, sideBottomY - 50, L"Auto-Suspensión: ACTIVA", CLR_ACCENT_CYAN, state->hFontSmall);

    // =========================================================================
    // 3. Main Content Area (modular tab dispatch)
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
            ui::HomeViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 1:
            ui::SettingsViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 2:
            RenderTabMemory(hdcMem, rcClient, state, contentLeft, contentRight, contentTop, contentBottom, contentW);
            break;
        case 3:
            ui::GamesViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 4:
            ui::HistoryViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 5:
            ui::ConfigViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 6:
            ui::ProgramsViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 7:
            ui::EquipmentViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        case 8:
            ui::RequirementsViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
        default:
            ui::HomeViewPaint(hdcMem, state, RECT{contentLeft, contentTop, contentRight, contentBottom});
            break;
    }

    // =========================================================================
    // 4. Bottom Status Bar
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

    // Green circular indicator
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
                                ? L"VegaFlow activo - vigilando juegos y memoria"
                                : Utf8ToWide(state->data.last_action_status);
    // One line that ends in an ellipsis before the APU button instead of running over it.
    RECT rcStatus = { 32, rcClient.bottom - 24, rcClient.right - 470, rcClient.bottom - 4 };
    DrawTextRect(hdcMem, rcStatus, statusText, CLR_TEXT_TITLE, state->hFontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    RECT rcApuBtn = { rcClient.right - 460, rcClient.bottom - 26, rcClient.right - 295, rcClient.bottom - 6 };
    bool isApuHov = (state->hovered_id == ID_ACT_TOGGLE_APU_POWER);
    std::wstring apuBtnText = state->data.apu_gpu_priority_active ? L"APU: GPU-PRIORITY" : L"APU: BALANCEADO";
    COLORREF apuColor = state->data.apu_gpu_priority_active ? CLR_ACCENT_GREEN : CLR_ACCENT_AMBER;
    DrawActionButton(hdcMem, rcApuBtn, apuBtnText, isApuHov, apuColor, state->hFontSmallBold, ID_ACT_TOGGLE_APU_POWER, state);

    DrawTextAt(hdcMem, rcClient.right - 280, rcClient.bottom - 23, L"VegaFlow Universal | Win32 GDI", CLR_TEXT_MUTED, state->hFontSmall);
}

// =============================================================================
// Interactive Action Dispatch (click handling on hit-boxes)
// =============================================================================
// Polls the screen with native controls that is in front, so its switches and fields show what is saved now.
void RefreshActiveScreen(HWND hwnd, GuiWindowState* state) {
    if (state->current_tab == 0) ui::HomeViewRefresh(hwnd, state);
    else if (state->current_tab == 1) ui::SettingsViewRefresh(hwnd, state);
    else if (state->current_tab == 3) ui::GamesViewRefresh(hwnd, state);
    else if (state->current_tab == 4) ui::HistoryViewRefresh(hwnd, state);
    else if (state->current_tab == 5) ui::ConfigViewRefresh(hwnd, state);
    else if (state->current_tab == 6) ui::ProgramsViewRefresh(hwnd, state);
    else if (state->current_tab == 8) ui::RequirementsViewRefresh(hwnd, state);
}

void HandleActionClick(HWND hwnd, GuiWindowState* state, int id) {
    if (!state) return;

    // Tab switching
    if (id >= ID_TAB_0 && id <= ID_TAB_8) {
        ui::ConfigViewCommitPending(hwnd, state);
        state->current_tab = id - ID_TAB_0;
        RefreshActiveScreen(hwnd, state);
        ui::SettingsViewShow(state, state->current_tab == 1);
        ui::HomeViewShow(state, state->current_tab == 0);
        ui::GamesViewShow(state, state->current_tab == 3);
        ui::HistoryViewShow(state, state->current_tab == 4);
        ui::ConfigViewShow(state, state->current_tab == 5);
        ui::ProgramsViewShow(state, state->current_tab == 6);
        ui::RequirementsViewShow(state, state->current_tab == 8);
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

        case ID_ACT_PURGE_RAM: {
            if (state->callbacks.on_PurgeNow) {
                res = state->callbacks.on_PurgeNow();
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
// Win32 Window Procedure (DashboardWndProc)
// =============================================================================
LRESULT CALLBACK DashboardWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<GuiWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // A second launch asks the running window to come forward (the one message let through UIPI, see app_identity.hpp).
    static const UINT restore_msg = RegisterWindowMessageW(ui::kRestoreWindowMessage);
    if (uMsg == restore_msg && state) {
        ShowWindow(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    switch (uMsg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }

        case WM_CREATE: {
            state = reinterpret_cast<GuiWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (!state) return -1;

            // Segoe UI fonts with ClearType
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

            // System Tray icon
            state->nid.cbSize = sizeof(NOTIFYICONDATAW);
            state->nid.hWnd = hwnd;
            state->nid.uID = ID_TRAY_ICON;
            state->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            state->nid.uCallbackMessage = WM_APP_TRAYICON;
            state->nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            wcscpy_s(state->nid.szTip, ui::kTrayTip);
            state->tray_registered = (Shell_NotifyIconW(NIM_ADD, &state->nid) == TRUE);

            // 1500 ms telemetry timer
            SetTimer(hwnd, TIMER_TELEMETRY_ID, TIMER_TELEMETRY_INTERVAL, nullptr);

            ChangeWindowMessageFilterEx(hwnd, RegisterWindowMessageW(ui::kRestoreWindowMessage), MSGFLT_ALLOW, nullptr);

            // Initial query
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
            if (state && state->current_tab == 1) ui::SettingsViewRelayout(state);
            if (state && state->current_tab == 0) ui::HomeViewRelayout(state);
            if (state && state->current_tab == 3) ui::GamesViewRelayout(state);
            if (state && state->current_tab == 4) ui::HistoryViewRelayout(state);
            if (state && state->current_tab == 5) ui::ConfigViewRelayout(state);
            if (state && state->current_tab == 6) ui::ProgramsViewRelayout(state);
            if (state && state->current_tab == 8) ui::RequirementsViewRelayout(state);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (state && state->current_tab == 1) {
                ui::SettingsViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 0) {
                ui::HomeViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 3) {
                ui::GamesViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 4) {
                ui::HistoryViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 5) {
                ui::ConfigViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 6) {
                ui::ProgramsViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 7) {
                ui::EquipmentViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            if (state && state->current_tab == 8) {
                ui::RequirementsViewScroll(state, -GET_WHEEL_DELTA_WPARAM(wParam) / 2);
                return 0;
            }
            break;
        }

        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (state && item && item->CtlType == ODT_BUTTON) {
                if (static_cast<int>(item->CtlID) >= ui::kHomeEquipmentId) ui::HomeViewDrawItem(item, state);
                else if (static_cast<int>(item->CtlID) >= ui::kGamesRowBaseId) ui::GamesViewDrawItem(item, state);
                else if (static_cast<int>(item->CtlID) == ui::kRequirementsBackId) ui::RequirementsViewDrawItem(item, state);
                else if (static_cast<int>(item->CtlID) >= ui::kProgKindBaseId) ui::ProgramsViewDrawItem(item, state);
                else if (static_cast<int>(item->CtlID) >= ui::kConfigResetId) ui::ConfigViewDrawItem(item, state);
                else if (static_cast<int>(item->CtlID) >= ui::kHistoryRestoreAllId) ui::HistoryViewDrawItem(item, state);
                else ui::SettingsViewDrawItem(item, state);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLOREDIT: {
            if (state) return ui::ConfigViewColorEdit(reinterpret_cast<HDC>(wParam), state);
            break;
        }

        case WM_ERASEBKGND:
            return 1; // WM_PAINT handles everything with double buffering

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
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) RefreshActiveScreen(hwnd, state);
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
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Restaurar VegaFlow");
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_PURGE, L"Purgar Standby RAM Ahora");
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
            if (ui::HomeViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::GamesViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::RequirementsViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::ProgramsViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::ConfigViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::HistoryViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;
            if (ui::SettingsViewCommand(hwnd, state, wmId, HIWORD(wParam), reinterpret_cast<HWND>(lParam))) return 0;

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
                KillTimer(hwnd, TIMER_TELEMETRY_ID);
                ui::ConfigViewDestroy(state);
                ui::ProgramsViewDestroy(state);
                ui::HomeViewDestroy(state);
                ui::GamesViewDestroy(state);
                ui::RequirementsViewDestroy(state);

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

void SelectTab(HWND hwnd, GuiWindowState* state, int tab) { HandleActionClick(hwnd, state, ID_TAB_0 + tab); }

int GuiWindow::RunGuiLoop(const GuiCallbacks& callbacks) {
    // The layout is drawn in fixed pixels, so the window asks Windows to scale it (crisp GDI text) instead of being
    // per-monitor aware, which left it tiny at 125 % and 150 %. Older Windows ignores the call and scales it anyway.
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
            pfnSetDpi(reinterpret_cast<HANDLE>(-5)); // DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED
        }
    }

    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    const wchar_t* const CLASS_NAME = ui::kWindowClass;

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

    // Wide 1120 x 740 window, centered
    RECT rc = { 0, 0, 1120, 740 };
    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRect(&rc, dwStyle, FALSE);

    int winWidth = rc.right - rc.left;
    int winHeight = rc.bottom - rc.top;
    // Fit the usable area (screen minus taskbar): at 150 % scale a 1080p screen is 1280 x 720 for this window, smaller than
    // the design size, and a window taller than that hid its status bar under the taskbar.
    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    winWidth = std::min<int>(winWidth, work.right - work.left);
    winHeight = std::min<int>(winHeight, work.bottom - work.top);
    int posX = work.left + std::max<int>(0, (work.right - work.left - winWidth) / 2);
    int posY = work.top + std::max<int>(0, (work.bottom - work.top - winHeight) / 2);

    GuiWindowState state;
    state.callbacks = callbacks;

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        ui::kWindowTitle,
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

    ui::SettingsViewRefresh(hwnd, &state);
    ui::SettingsViewShow(&state, false);
    ui::HomeViewRefresh(hwnd, &state);
    ui::HomeViewShow(&state, true);
    ui::GamesViewRefresh(hwnd, &state);
    ui::GamesViewShow(&state, false);
    ui::HistoryViewRefresh(hwnd, &state);
    ui::HistoryViewShow(&state, false);
    ui::ConfigViewRefresh(hwnd, &state);
    ui::ConfigViewShow(&state, false);
    ui::ProgramsViewRefresh(hwnd, &state);
    ui::ProgramsViewShow(&state, false);
    ui::RequirementsViewRefresh(hwnd, &state);
    ui::RequirementsViewShow(&state, false);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // Standard message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Ctrl+1..8 jump to a tab: the sidebar is not a control yet, so this is the keyboard way in.
        if (msg.message == WM_KEYDOWN && msg.wParam >= '1' && msg.wParam <= '8' && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
            GetAncestor(msg.hwnd, GA_ROOT) == hwnd) {
            HandleActionClick(hwnd, &state, ID_TAB_0 + static_cast<int>(msg.wParam - '1'));
            continue;
        }
        // The read-only screens have no field to focus: Page Up/Down, Home and End (and the arrows on Tu equipo) scroll them.
        if (msg.message == WM_KEYDOWN && (state.current_tab == 0 || state.current_tab == 7 || state.current_tab == 8) && GetAncestor(msg.hwnd, GA_ROOT) == hwnd &&
            !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
            int delta = 0;
            if (msg.wParam == VK_NEXT) delta = 240;
            else if (msg.wParam == VK_PRIOR) delta = -240;
            else if (msg.wParam == VK_HOME) delta = -100000;
            else if (msg.wParam == VK_END) delta = 100000;
            else if (state.current_tab == 7 && msg.wParam == VK_DOWN) delta = 40;
            else if (state.current_tab == 7 && msg.wParam == VK_UP) delta = -40;
            if (delta != 0) {
                if (state.current_tab == 0) ui::HomeViewScroll(&state, delta);
                else if (state.current_tab == 7) ui::EquipmentViewScroll(&state, delta);
                else ui::RequirementsViewScroll(&state, delta);
                continue;
            }
        }
        if (IsDialogMessageW(hwnd, &msg)) continue; // Tab, Shift+Tab, arrows and Space reach the native controls
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(CLASS_NAME, hInstance);
    return static_cast<int>(msg.wParam);
}

} // namespace vegaflow
