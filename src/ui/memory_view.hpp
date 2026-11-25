#pragma once
// "Memoria UMA y núcleos" tab: the memory numbers, and the thread map as ONE focusable control (arrows move between
// threads) with three native buttons (all reachable with Tab). The model lives in session/memory_screen.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

constexpr int kMemGridId = 10100; // the thread map; the three buttons keep their ID_ACT_* ids (202, 207, 208)

void MemoryViewRefresh(HWND hwnd, GuiWindowState* state);
void MemoryViewShow(GuiWindowState* state, bool show);
void MemoryViewRelayout(GuiWindowState* state);
void MemoryViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void MemoryViewDrawItem(const DRAWITEMSTRUCT* item, GuiWindowState* state);
bool MemoryViewOwnsId(int id);
// true when the id is ours; on a click `*act_id` is the ID_ACT_* the window must run (the buttons act through the existing handler).
bool MemoryViewCommand(HWND hwnd, GuiWindowState* state, int id, int notify_code, HWND from, int* act_id);
void MemoryViewScroll(GuiWindowState* state, int delta_px);
void MemoryViewDestroy(GuiWindowState* state);

} // namespace vegaflow::ui
