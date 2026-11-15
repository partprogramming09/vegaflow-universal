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

#include "ui/app_identity.hpp"
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

// Side menu tab identifiers (0 to 7)
constexpr int ID_TAB_0 = 100;
constexpr int ID_TAB_1 = 101;
constexpr int ID_TAB_2 = 102;
constexpr int ID_TAB_3 = 103;
constexpr int ID_TAB_4 = 104;
constexpr int ID_TAB_5 = 105;
constexpr int ID_TAB_6 = 106;
constexpr int ID_TAB_7 = 107;
constexpr int ID_TAB_8 = 108; // Requisitos: reached from Configuración, not listed in the sidebar

// Interactive control identifiers (GDI hit-test)
constexpr int ID_ACT_PURGE_RAM          = 202;
constexpr int ID_ACT_ISOLATE_BG         = 207;
constexpr int ID_ACT_RESTORE_CPU        = 208;
constexpr int ID_ACT_MINIMIZE_TRAY      = 216;
constexpr int ID_ACT_TOGGLE_APU_POWER   = 217;


// Tray context menu
constexpr int ID_TRAY_RESTORE           = ui::kTrayRestoreCommand;
constexpr int ID_TRAY_PURGE             = 3002;
constexpr int ID_TRAY_EXIT              = 3004;

// Design dimensions
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

constexpr COLORREF CLR_ACCENT_CYAN      = RGB(59, 130, 246);   // #3B82F6 sober steel blue
constexpr COLORREF CLR_ACCENT_GREEN     = RGB(16, 185, 129);   // #10B981 optimal green
constexpr COLORREF CLR_ACCENT_AMBER     = RGB(245, 158, 11);   // #F59E0B warning amber
constexpr COLORREF CLR_ACCENT_RED       = RGB(239, 68, 68);    // #EF4444 critical red
constexpr COLORREF CLR_CHIP_RED_FILL    = RGB(58, 28, 31);     // fill of the "No recomendado" chip
constexpr COLORREF CLR_CHIP_RED_TEXT    = RGB(248, 113, 113);  // its text: 5.6:1 on the fill (CLR_ACCENT_RED gives 4.1:1)
constexpr COLORREF CLR_ACCENT_PURPLE    = RGB(139, 92, 246);   // #8B5CF6 sober indigo/purple (fills and borders only: 3.9:1 on cards)
constexpr COLORREF CLR_ACCENT_PURPLE_TEXT = RGB(167, 139, 250); // #A78BFA the same hue for text (4.5:1 or more)

// Controls: the border of a button, field or switch must reach 3:1 against the surface it sits on (WCAG 1.4.11);
// the keyboard focus ring must be visible on every surface (WCAG 2.4.7). Checked by Test 62.
constexpr COLORREF CLR_CONTROL_BORDER   = RGB(110, 119, 135);  // #6E7787
constexpr COLORREF CLR_FOCUS_RING       = RGB(147, 180, 248);  // #93B4F8

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
    int current_tab = 0; // 0: Dashboard, 1: Ajustes, 2: Mem/CPU, 3: Juegos, 4: Historial, 5: Configuración, 6: Programas, 7: Tu equipo
    int hovered_id = 0;
    std::vector<HitBox> hit_boxes;

    HFONT hFontTitle = nullptr;
    HFONT hFontHeader = nullptr;
    HFONT hFontRegular = nullptr;
    HFONT hFontBold = nullptr;
    HFONT hFontSmall = nullptr;
    HFONT hFontSmallBold = nullptr;
    HFONT hFontMetric = nullptr;

    NOTIFYICONDATAW nid = {};
    bool tray_registered = false;

    // Ajustes del sistema screen (ui/settings_view.cpp)
    HWND hwnd = nullptr;
    SettingsScreenModel settings_model;
    SettingsScreenInputs settings_inputs;
    bool settings_show_experimental = false;
    std::string settings_open_tip;        // id of the row whose (i) text is open
    int settings_scroll = 0;
    std::vector<HWND> settings_controls;  // [0] experimental switch, then per row: switch, (i); in Tab order
    std::vector<std::string> settings_signature;

    // Historial screen (ui/history_view.cpp)
    session::HistoryScreenModel history_model;
    int history_scroll = 0;
    std::vector<HWND> history_controls; // [0] restore all, then one per row (nullptr when the row cannot be undone)
    std::vector<std::string> history_signature;

    // Configuración screen (ui/config_view.cpp)
    session::ConfigScreenModel config_model;
    int config_scroll = 0;
    std::vector<HWND> config_controls; // [0] reset, then one per row in order (switch button or EDIT), then restore-all, then Requisitos
    size_t config_structure = 0;       // how many rows the controls were built for
    int config_focus_id = 0;           // the EDIT that has the keyboard, for its focus ring
    HBRUSH config_edit_brush = nullptr;

    // Programas screen (ui/programs_view.cpp)
    session::ProgramsScreenModel programs_model;
    int programs_scroll = 0;
    std::vector<HWND> programs_controls; // 21 kind buttons, then per exception [action, remove], its EDIT and add, per game [remove], its EDIT and add
    size_t programs_exceptions = 0;      // rows the controls were built for
    size_t programs_games = 0;
    int programs_focus_id = 0;           // the EDIT that has the keyboard, for its focus ring

    // Inicio screen (ui/home_view.cpp): the numbers are drawn from `data` on every paint; three native buttons
    std::vector<HWND> home_controls;
    int home_scroll = 0;
    int home_max_scroll = 0;

    // Juegos screen (ui/games_view.cpp)
    session::GamesScreenModel games_model;
    int games_scroll = 0;
    uint32_t games_open_app_id = 0;     // the game whose details are open (0 = none)
    std::vector<HWND> games_controls;   // [0] rescan, then one per game row, then the open row's action buttons
    std::string games_signature;

    // Tu equipo screen (ui/equipment_view.cpp): read-only, scrolls with the wheel
    int equipment_scroll = 0;
    int equipment_max_scroll = 0;

    // Requisitos screen (ui/requirements_view.cpp): read-only, one native button back to Configuración
    std::vector<HWND> requirements_controls;
    int requirements_scroll = 0;
    int requirements_max_scroll = 0;
};

// Shows tab `tab` (0-based), saving what is half typed on the screen being left.
void SelectTab(HWND hwnd, GuiWindowState* state, int tab);

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

// Modular rendering of the 5 tabs
void RenderTabMemory(HDC hdcMem, const RECT& rcClient, GuiWindowState* state, int contentLeft, int contentRight, int contentTop, int contentBottom, int contentW);

} // namespace vegaflow
