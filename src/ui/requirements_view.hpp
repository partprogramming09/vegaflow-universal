#pragma once
// "Requisitos y advertencias" screen: what VegaFlow needs and whether this PC meets it. Reached from Configuración;
// its one native button goes back there. Read-only otherwise.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kRequirementsBackId = 9600; // above Programas (9001..9501), below 65536

void RequirementsViewRefresh(HWND hwnd, GuiWindowState* state);
void RequirementsViewShow(GuiWindowState* state, bool show);
void RequirementsViewRelayout(GuiWindowState* state);
void RequirementsViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void RequirementsViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours and came from our button on our tab.
bool RequirementsViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void RequirementsViewScroll(GuiWindowState* state, int delta_px);
void RequirementsViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
