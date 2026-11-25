#pragma once
// "Inicio" screen: is a game open, the machine in three lines, the shared graphics memory and the background programs.
// Three native buttons (all reachable with Tab): the machine's detail, free memory now, and which programs are slowed.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kHomeEquipmentId = 10000; // below 65536: LOWORD(wParam) is 16 bits
constexpr int kHomePurgeId = 10001;
constexpr int kHomeProgramsId = 10002;
constexpr int kHomeUndoAllId = 10003;
constexpr int kHomeReportId = 10004;
constexpr int kHomeDoctorId = 10005; // E2.7
constexpr int kHomePauseId = 10006;  // E2.7: only shown when the model offers the pause
static_assert(kHomePauseId < 65536 && kHomeEquipmentId > 9999, "Inicio ids sit above Juegos (ui/games_view.hpp) and fit the 16-bit LOWORD of wParam");

void HomeViewRefresh(HWND hwnd, GuiWindowState* state);
void HomeViewShow(GuiWindowState* state, bool show);
void HomeViewRelayout(GuiWindowState* state);
void HomeViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void HomeViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; `from` must be one of our controls on our tab or the message is ignored.
bool HomeViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
// F4.6: copies the report to the clipboard and leaves the answer in the status bar (the Inicio button and the tray menu).
void CopyReportAndNote(HWND hwnd, GuiWindowState* state);
void HomeViewScroll(GuiWindowState* state, int delta_px);
void HomeViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
