#pragma once
// "Ajustes del sistema" screen: reads SettingsScreenModel (session/) and draws it. Switches and (i) buttons are native
// BUTTON controls (owner-drawn), so they have keyboard focus, Tab order and an accessible name; cards and labels are GDI.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kSettingsExperimentalId = 4999;
constexpr int kSettingsRowBaseId = 5000; // per row k: switch = base + 2k, (i) = base + 2k + 1

void SettingsViewRefresh(HWND hwnd, GuiWindowState* state);  // polls the model and rebuilds the controls if the rows changed
void SettingsViewShow(GuiWindowState* state, bool show);      // show or hide the controls with the tab
void SettingsViewRelayout(GuiWindowState* state);             // place and clip the controls (resize, scroll, (i) open)
void SettingsViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void SettingsViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours. `from` is the control that sent it: anything not created here (a posted fake) is ignored.
bool SettingsViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from);
void SettingsViewScroll(GuiWindowState* state, int delta_px);

} // namespace vegaflow::ui
