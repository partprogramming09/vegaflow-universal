#pragma once
// Shared pieces of the screens that use native (owner-drawn) BUTTON controls: creation, clipped placement inside
// the scrolling content area, scroll-to-focus, and the button frame with its keyboard focus ring.

#include <string>

#include "ui/gui_components.hpp"

namespace vegaflow::ui {

RECT ScreenContentRect(HWND hwnd); // the area right of the sidebar and above the status bar
int TextHeight(HDC hdc, HFONT font, const std::wstring& text, int width);
int TextWidth(HDC hdc, HFONT font, const std::wstring& text);

HWND MakeOwnerButton(HWND parent, int id, const std::wstring& accessible_name);
// Moves the control to `r` and clips it to `content`, so a scrolled control never paints over the sidebar or the
// status bar, yet stays focusable (a hidden control would be skipped by Tab).
void PlaceClipped(HWND control, const RECT& r, const RECT& content);
// How far the content must scroll so that `control` is fully visible; 0 when it already is.
int ScrollDeltaToReveal(HWND parent, HWND control);

void DrawButtonFrame(HDC hdc, const RECT& rc, COLORREF surface, bool pressed);
void DrawFocusRing(HDC hdc, const RECT& rc);
// Switch face: track and knob, then the state in words (colour is never the only cue).
void DrawSwitchFace(HDC hdc, const RECT& rc, bool on, const std::wstring& label, HFONT font);

} // namespace vegaflow::ui
