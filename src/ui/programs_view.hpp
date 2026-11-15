#pragma once
// "Programas" screen: what VegaFlow does with each kind of program (three-way choice), the exceptions for single
// programs and the games added by hand. Native owner-drawn buttons and EDIT fields, reachable with Tab.

#include "ui/config_view.hpp"
#include "ui/gui_components.hpp"

namespace vegaflow::ui {

// Control ids (all below 65536: LOWORD(wParam) is 16 bits).
constexpr int kProgKindBaseId = 9001;   // kind c, choice o: base + 3c + o (7 kinds, choices Frenar, Solo ahorro, No tocar)
constexpr int kProgExcBaseId = 9100;    // exception i: base + 2i is its action button, base + 2i + 1 its remove button
constexpr int kProgExcEditId = 9300;
constexpr int kProgExcAddId = 9301;
constexpr int kProgGameBaseId = 9400;   // game j: remove button
constexpr int kProgGameEditId = 9500;
constexpr int kProgGameAddId = 9501;
constexpr size_t kProgMaxRows = 100;    // rows shown per list; adding stops there so nothing saved goes unseen
static_assert(kProgKindBaseId > kConfigRestoreAllId, "Programas ids start above Configuración");

static_assert(kProgExcBaseId + 2 * kProgMaxRows <= kProgExcEditId, "exception ids overlap the field");
static_assert(kProgGameBaseId + kProgMaxRows <= kProgGameEditId, "game ids overlap the field");
static_assert(kProgGameAddId < 9600, "Programas ids stay below Requisitos (ui/requirements_view.hpp)");

void ProgramsViewRefresh(HWND hwnd, GuiWindowState* state);
void ProgramsViewShow(GuiWindowState* state, bool show);
void ProgramsViewRelayout(GuiWindowState* state);
void ProgramsViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void ProgramsViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
bool ProgramsViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void ProgramsViewScroll(GuiWindowState* state, int delta_px);
void ProgramsViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
