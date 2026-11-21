#pragma once
// "Juegos" screen: one row per Steam game with its state; "Ver cambios" opens one row with the file, the backup copy and
// the exact lines a profile writes, and the buttons Aplicar perfil, Volver al archivo original, Me fue mal en este juego
// and Volver a incluir. Native owner-drawn buttons, reachable with Tab.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kGamesRowBaseId = 9700; // row i: base + i (the button that opens or closes its details)
constexpr size_t kGamesMaxRows = 200; // rows shown; the screen says so when the library is bigger
constexpr int kGamesApplyId = 9900;
constexpr int kGamesRestoreId = 9901;
constexpr int kGamesBadId = 9902;
constexpr int kGamesIncludeId = 9903;
constexpr int kGamesRescanId = 9904;
constexpr int kGamesDxvkId = 9905;
constexpr int kGamesLastId = 9999;
static_assert(kGamesRowBaseId > 9600 && kGamesLastId < 65536, "Juegos ids sit above Requisitos and fit the 16-bit LOWORD of wParam");
static_assert(kGamesRowBaseId + kGamesMaxRows <= kGamesApplyId, "row ids overlap the action ids");

void GamesViewRefresh(HWND hwnd, GuiWindowState* state);
void GamesViewShow(GuiWindowState* state, bool show);
void GamesViewRelayout(GuiWindowState* state);
void GamesViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void GamesViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; `from` must be one of our controls on our tab or the message is ignored.
bool GamesViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void GamesViewScroll(GuiWindowState* state, int delta_px);
void GamesViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
