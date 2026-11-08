#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include "ui/gui_window.hpp"

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

namespace vegaflow {

// =========================================================================
// Timer and system tray constants
// =========================================================================
constexpr UINT_PTR TIMER_TELEMETRY_ID       = 1;
constexpr UINT     TIMER_TELEMETRY_INTERVAL = 1500;
constexpr UINT     WM_APP_TRAYICON          = WM_USER + 101;
constexpr UINT     ID_TRAY_ICON             = 1001;

// Side menu tab identifiers (0 to 5)
constexpr int ID_TAB_0 = 100;
constexpr int ID_TAB_1 = 101;
constexpr int ID_TAB_2 = 102;
constexpr int ID_TAB_3 = 103;
constexpr int ID_TAB_4 = 104;
constexpr int ID_TAB_5 = 105;

// Interactive control identifiers (GDI hit-test)
constexpr int ID_ACT_APPLY_PRESET       = 201;
constexpr int ID_ACT_PURGE_RAM          = 202;
constexpr int ID_ACT_TOGGLE_TIMER       = 203;
constexpr int ID_ACT_TOGGLE_MPO         = 204;
constexpr int ID_ACT_TOGGLE_SHADERCACHE = 205;
constexpr int ID_ACT_TOGGLE_MEMCOMP     = 206;
constexpr int ID_ACT_ISOLATE_BG         = 207;
constexpr int ID_ACT_RESTORE_CPU        = 208;
constexpr int ID_ACT_TOGGLE_FSR         = 209;
constexpr int ID_ACT_FSR_SHARP_10       = 210;
constexpr int ID_ACT_FSR_SHARP_20       = 211;
constexpr int ID_ACT_FSR_SHARP_40       = 212;
constexpr int ID_ACT_FSR_SHARP_70       = 213;
constexpr int ID_ACT_RESTORE_DEFAULTS   = 214;
constexpr int ID_ACT_ELEVATE            = 215;
constexpr int ID_ACT_MINIMIZE_TRAY      = 216;
constexpr int ID_ACT_TOGGLE_APU_POWER   = 217;
constexpr int ID_ACT_APPLY_ALL_2B       = 218;
constexpr int ID_ACT_TOGGLE_VRAM_OVERRIDE = 219;
constexpr int ID_ACT_TOGGLE_MMCSS       = 123;
constexpr int ID_ACT_FSR_MODE_0         = 124;
constexpr int ID_ACT_FSR_MODE_1         = 125;
constexpr int ID_ACT_FSR_MODE_2         = 126;

constexpr int ID_ACT_RESCAN_STEAM       = 220;
constexpr int ID_ACT_STEAM_PREV_PAGE    = 221;
constexpr int ID_ACT_STEAM_NEXT_PAGE    = 222;
constexpr int ID_ACT_STEAM_APPLY_0      = 230;
constexpr int ID_ACT_STEAM_APPLY_1      = 231;
constexpr int ID_ACT_STEAM_APPLY_2      = 232;
constexpr int ID_ACT_STEAM_RESTORE_0    = 240;
constexpr int ID_ACT_STEAM_RESTORE_1    = 241;
constexpr int ID_ACT_STEAM_RESTORE_2    = 242;
constexpr int ID_ACT_STEAM_DXVK_0       = 250;
constexpr int ID_ACT_STEAM_DXVK_1       = 251;
constexpr int ID_ACT_STEAM_DXVK_2       = 252;

// Tray context menu
constexpr int ID_TRAY_RESTORE           = 3001;
constexpr int ID_TRAY_PURGE             = 3002;
constexpr int ID_TRAY_FSR               = 3003;
constexpr int ID_TRAY_EXIT              = 3004;

constexpr int ID_HOTKEY_FSR             = 4001;

// UXTU design dimensions
constexpr int SIDEBAR_WIDTH             = 250;
constexpr int STATUSBAR_HEIGHT          = 32;

// Modern Dark Mode color palette, Clean Slate / Nordic Dark style
constexpr COLORREF CLR_BG_MAIN          = RGB(17, 19, 23);     // #111317 main background
constexpr COLORREF CLR_BG_SIDEBAR       = RGB(22, 25, 30);     // #16191E Sidebar
constexpr COLORREF CLR_SIDEBAR_BORDER   = RGB(38, 41, 48);     // #262930 side divider
constexpr COLORREF CLR_TAB_IDLE         = RGB(22, 25, 30);     // Inactive tab
constexpr COLORREF CLR_TAB_HOVER        = RGB(33, 38, 48);     // Tab hover
constexpr COLORREF CLR_TAB_ACTIVE       = RGB(40, 47, 60);     // Selected tab
constexpr COLORREF CLR_TAB_INDICATOR    = RGB(59, 130, 246);   // Active indicator (sober steel blue #3B82F6)

constexpr COLORREF CLR_CARD_BG          = RGB(27, 30, 36);     // #1B1E24 card
constexpr COLORREF CLR_CARD_BORDER      = RGB(43, 48, 60);     // #2B303C card border (subtle 1px)
constexpr COLORREF CLR_CARD_HEADER      = RGB(32, 36, 44);     // #20242C card header

constexpr COLORREF CLR_TEXT_TITLE       = RGB(243, 244, 246);  // #F3F4F6 neutral white title
constexpr COLORREF CLR_TEXT_BODY        = RGB(209, 213, 219);  // #D1D5DB light text
constexpr COLORREF CLR_TEXT_MUTED       = RGB(156, 163, 175);  // #9CA3AF medium text
constexpr COLORREF CLR_TEXT_DIM         = RGB(107, 114, 128);  // #6B7280 dark text

constexpr COLORREF CLR_ACCENT_CYAN      = RGB(59, 130, 246);   // #3B82F6 sober steel blue
constexpr COLORREF CLR_ACCENT_GREEN     = RGB(16, 185, 129);   // #10B981 optimal green
constexpr COLORREF CLR_ACCENT_AMBER     = RGB(245, 158, 11);   // #F59E0B warning amber
constexpr COLORREF CLR_ACCENT_RED       = RGB(239, 68, 68);    // #EF4444 critical red
constexpr COLORREF CLR_ACCENT_PURPLE    = RGB(139, 92, 246);   // #8B5CF6 sober indigo/purple

constexpr COLORREF CLR_BTN_BG           = RGB(33, 38, 48);     // Normal button
constexpr COLORREF CLR_BTN_HOVER        = RGB(48, 55, 70);     // Hover button
constexpr COLORREF CLR_STATUSBAR_BG     = RGB(14, 16, 20);     // Bottom bar
constexpr COLORREF CLR_BAR_BG           = RGB(20, 23, 28);     // Progress bar background

// Interactive hit-test region structure for GDI (zero child controls)
struct HitBox {
    int id = 0;
    RECT rect{};
};

// Full user interface state
struct GuiWindowState {
    GuiCallbacks callbacks;
    GuiDashboardData data;
    int current_tab = 0; // 0: Dashboard, 1: Kernel, 2: Mem/CPU, 3: FSR, 4: Steam, 5: Audit Log
    int hovered_id = 0;
    int steam_page = 0;
    std::vector<HitBox> hit_boxes;

    HFONT hFontTitle = nullptr;
    HFONT hFontHeader = nullptr;
    HFONT hFontRegular = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontSmall = nullptr;
    HFONT hFontSmallBold = nullptr;
    HFONT hFontMetric = nullptr;
    HFONT hFontMonospace = nullptr;

    NOTIFYICONDATAW nid = {};
    bool tray_registered = false;
};

// UTF-8 to UTF-16 string conversion
std::wstring Utf8ToWide(const std::string& str);

// Primitivas visuales y controles GDI reutilizables
void DrawRoundedCard(HDC hdc, const RECT& rc, COLORREF bgCol, COLORREF borderCol, int radius = 6);
void DrawTextAt(HDC hdc, int x, int y, const std::wstring& text, COLORREF color, HFONT hFont);
void DrawTextRect(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF color, HFONT hFont, UINT format);
void DrawBadge(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF textCol, COLORREF bgCol, COLORREF borderCol, HFONT hFont);
void DrawProgressBar(HDC hdc, const RECT& rc, double fraction, COLORREF fillCol, COLORREF bgCol, COLORREF borderCol);
void DrawMultiSegmentBar(HDC hdc, const RECT& rc, const std::vector<std::pair<double, COLORREF>>& segments, COLORREF bgCol, COLORREF borderCol);
void DrawActionButton(HDC hdc, const RECT& rc, const std::wstring& text, bool is_hovered, COLORREF accentCol, HFONT hFont, int id, GuiWindowState* state);
void DrawTabButton(HDC hdc, const RECT& rc, const std::wstring& text, bool is_active, bool is_hovered, int id, GuiWindowState* state);
void DrawToggleSwitch(HDC hdc, const RECT& rc, bool is_on, bool is_hovered, int id, GuiWindowState* state);

// Modular rendering of the 6 tabs
void RenderTabDashboard(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);
void RenderTabKernel(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);
void RenderTabMemory(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);
void RenderTabScaler(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);
void RenderTabSteam(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);
void RenderTabAuditLog(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);

} // namespace vegaflow
