#include "ui/native_controls.hpp"

namespace vegaflow::ui {

RECT ScreenContentRect(HWND hwnd) {
    RECT c{};
    GetClientRect(hwnd, &c);
    return {SIDEBAR_WIDTH + 16, 16, c.right - 16, c.bottom - STATUSBAR_HEIGHT - 16};
}

int TextHeight(HDC hdc, HFONT font, const std::wstring& text, int width) {
    HGDIOBJ old = SelectObject(hdc, font);
    RECT r = {0, 0, width > 40 ? width : 40, 0};
    DrawTextW(hdc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    return static_cast<int>(r.bottom - r.top);
}

int TextWidth(HDC hdc, HFONT font, const std::wstring& text) {
    HGDIOBJ old = SelectObject(hdc, font);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(hdc, old);
    return static_cast<int>(sz.cx);
}

HWND MakeOwnerButton(HWND parent, int id, const std::wstring& accessible_name) {
    return CreateWindowExW(0, L"BUTTON", accessible_name.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, 0, 0, 10,
                           10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

void PlaceClipped(HWND control, const RECT& r, const RECT& content) {
    if (!control) return;
    SetWindowPos(control, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RECT vis{};
    if (!IntersectRect(&vis, &r, &content)) vis = {0, 0, 0, 0};
    else OffsetRect(&vis, -r.left, -r.top);
    HRGN rgn = CreateRectRgn(vis.left, vis.top, vis.right, vis.bottom);
    if (!SetWindowRgn(control, rgn, TRUE)) DeleteObject(rgn); // on success the system owns the region
}

int ScrollDeltaToReveal(HWND parent, HWND control) {
    RECT r{};
    GetWindowRect(control, &r);
    MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&r), 2);
    const RECT content = ScreenContentRect(parent);
    if (r.top < content.top) return static_cast<int>(r.top - content.top) - 8;
    if (r.bottom > content.bottom) return static_cast<int>(r.bottom - content.bottom) + 8;
    return 0;
}

void DrawButtonFrame(HDC hdc, const RECT& rc, COLORREF surface, bool pressed) {
    HBRUSH bg = CreateSolidBrush(surface);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    const RECT body = {rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1};
    DrawRoundedCard(hdc, body, pressed ? CLR_BTN_HOVER : CLR_BTN_BG, CLR_CONTROL_BORDER, 6);
}

void DrawFocusRing(HDC hdc, const RECT& rc) {
    // 2 px, 3:1 or better on every surface (Test 62).
    HPEN ring = CreatePen(PS_SOLID, 2, CLR_FOCUS_RING);
    HGDIOBJ old_pen = SelectObject(hdc, ring);
    HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 8, 8);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(ring);
}

void DrawSwitchFace(HDC hdc, const RECT& rc, bool on, const std::wstring& label, HFONT font) {
    const int tx = rc.left + 10;
    const int ty = (rc.top + rc.bottom) / 2 - 10;
    const RECT track = {tx, ty, tx + 34, ty + 20};
    DrawRoundedCard(hdc, track, on ? CLR_ACCENT_CYAN : CLR_CONTROL_BORDER, on ? CLR_ACCENT_CYAN : CLR_CONTROL_BORDER, 10);
    const int kx = on ? tx + 34 - 18 : tx + 2;
    HBRUSH knob = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ old_brush = SelectObject(hdc, knob);
    HGDIOBJ old_pen = SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, kx, ty + 2, kx + 16, ty + 18);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(knob);
    SetBkMode(hdc, TRANSPARENT);
    const RECT tr = {tx + 34 + 8, rc.top, rc.right - 6, rc.bottom};
    DrawTextRect(hdc, tr, label, CLR_TEXT_TITLE, font, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

} // namespace vegaflow::ui
