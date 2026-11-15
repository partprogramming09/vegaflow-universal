#pragma once
// "Historial" screen: one row per change VegaFlow made, with the value before and after and a native Deshacer button.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kHistoryRestoreAllId = 6000;
constexpr int kHistoryRowBaseId = 6001; // row i: base + i

void HistoryViewRefresh(HWND hwnd, GuiWindowState* state);
void HistoryViewShow(GuiWindowState* state, bool show);
void HistoryViewRelayout(GuiWindowState* state);
void HistoryViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void HistoryViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; `from` must be one of our controls or the message is ignored.
bool HistoryViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void HistoryViewScroll(GuiWindowState* state, int delta_px);

} // namespace vegaflow::ui
