#pragma once
// The sidebar as native buttons: Tab and Shift+Tab reach every entry, Up/Down/Home/End move between them, Enter and Space open
// the focused one. The logic (order, wrapping) is in session/sidebar_nav.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

void SidebarViewCreate(HWND hwnd, GuiWindowState* state);
void SidebarViewRelayout(GuiWindowState* state);
// The active entry changed: native children are not repainted by the parent's invalidation.
void SidebarViewSync(GuiWindowState* state);
bool SidebarViewOwnsId(int id);
void SidebarViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
// true when the id is ours; on a click `*act_id` is the id the window must run (ID_TAB_n or ID_ACT_MINIMIZE_TRAY).
bool SidebarViewCommand(GuiWindowState* state, int id, int notify_code, HWND from, int* act_id);
// Keys the message loop routes here before IsDialogMessage: arrows between entries and Enter. true = handled.
bool SidebarViewKey(GuiWindowState* state, HWND focus, WPARAM key);
void SidebarViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
