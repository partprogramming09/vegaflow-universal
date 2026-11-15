#pragma once
// "Tu equipo" screen: read-only. Draws the thread map, the graphics adapter and the memory-protection level.

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

void EquipmentViewPaint(HDC hdc, GuiWindowState* state, const RECT& content);
void EquipmentViewScroll(GuiWindowState* state, int delta_px);

} // namespace vegaflow::ui
