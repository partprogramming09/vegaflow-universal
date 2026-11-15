#pragma once
// "Configuración" screen: switches (owner-drawn buttons) and number fields (native EDIT) for the settings of config.json.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kConfigResetId = 8000;
constexpr int kConfigRowBaseId = 8001; // row k (in screen order): base + k
constexpr int kConfigRestoreAllId = 8900;
constexpr int kConfigRequirementsId = 8901;
static_assert(kConfigRowBaseId < kConfigRestoreAllId && kConfigRequirementsId < 9001, "Configuración ids stay below Programas (ui/programs_view.hpp)");

void ConfigViewRefresh(HWND hwnd, GuiWindowState* state, bool force_text = false);
void ConfigViewShow(GuiWindowState* state, bool show);
void ConfigViewCommitPending(HWND hwnd, GuiWindowState* state);
void ConfigViewRelayout(GuiWindowState* state);
void ConfigViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void ConfigViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; `from` must be one of our controls (or the message is IDOK from Enter in a field).
bool ConfigViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
LRESULT ConfigViewColorEdit(HDC hdc, GuiWindowState* state); // WM_CTLCOLOREDIT
void ConfigViewScroll(GuiWindowState* state, int delta_px);
void ConfigViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
