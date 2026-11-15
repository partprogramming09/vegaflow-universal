#pragma once
// "Inicio" screen: is a game open, the machine in three lines, the shared graphics memory and the background programs.
// Three native buttons (all reachable with Tab): the machine's detail, free memory now, and which programs are slowed.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kHomeEquipmentId = 10000; // below 65536: LOWORD(wParam) is 16 bits
constexpr int kHomePurgeId = 10001;
constexpr int kHomeProgramsId = 10002;

void HomeViewRefresh(HWND hwnd, GuiWindowState* state);
void HomeViewShow(GuiWindowState* state, bool show);
void HomeViewRelayout(GuiWindowState* state);
void HomeViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void HomeViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; `from` must be one of our controls on our tab or the message is ignored.
bool HomeViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void HomeViewScroll(GuiWindowState* state, int delta_px);
void HomeViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
