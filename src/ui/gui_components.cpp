#include "ui/gui_components.hpp"

#include <algorithm>
#include <cmath>

namespace vegaflow {

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring wstr(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), &wstr[0], len);
    return wstr;
}

void DrawRoundedCard(HDC hdc, const RECT& rc, COLORREF bgCol, COLORREF borderCol, int radius) {
    HBRUSH hbr = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hbr));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbr);
    DeleteObject(hPen);
}

void DrawTextAt(HDC hdc, int x, int y, const std::wstring& text, COLORREF color, HFONT hFont) {
    HFONT hOldFont = nullptr;
    if (hFont) {
        hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    }
    SetTextColor(hdc, color);
    TextOutW(hdc, x, y, text.c_str(), static_cast<int>(text.length()));
    if (hOldFont) {
        SelectObject(hdc, hOldFont);
    }
}

void DrawTextRect(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF color, HFONT hFont, UINT format) {
    HFONT hOldFont = nullptr;
    if (hFont) {
        hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont));
    }
    SetTextColor(hdc, color);
    RECT rcCalc = rc;
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.length()), &rcCalc, format);
    if (hOldFont) {
        SelectObject(hdc, hOldFont);
    }
}

void DrawBadge(HDC hdc, const RECT& rc, const std::wstring& text, COLORREF textCol, COLORREF bgCol, COLORREF borderCol, HFONT hFont) {
    DrawRoundedCard(hdc, rc, bgCol, borderCol, 6);
    DrawTextRect(hdc, rc, text, textCol, hFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawProgressBar(HDC hdc, const RECT& rc, double fraction, COLORREF fillCol, COLORREF bgCol, COLORREF borderCol) {
    HBRUSH hbrBg = CreateSolidBrush(bgCol);
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hbrBg));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPenBorder));

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbrBg);
    DeleteObject(hPenBorder);

    fraction = std::clamp(fraction, 0.0, 1.0);
    int totalWidth = (rc.right - rc.left) - 2;
    int fillWidth = static_cast<int>(totalWidth * fraction);
    if (fillWidth > 2) {
        RECT rcFill = { rc.left + 1, rc.top + 1, rc.left + 1 + fillWidth, rc.bottom - 1 };
        HBRUSH hbrFill = CreateSolidBrush(fillCol);
        FillRect(hdc, &rcFill, hbrFill);
        DeleteObject(hbrFill);
    }
}

void DrawMultiSegmentBar(HDC hdc, const RECT& rc, const std::vector<std::pair<double, COLORREF>>& segments, COLORREF bgCol, COLORREF borderCol) {
    HBRUSH hbrBg = CreateSolidBrush(bgCol);
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, borderCol);
    HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hbrBg));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPenBorder));

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbrBg);
    DeleteObject(hPenBorder);

    int totalWidth = (rc.right - rc.left) - 2;
    if (totalWidth <= 0) return;

    int currentX = rc.left + 1;
    for (const auto& seg : segments) {
        double fraction = std::clamp(seg.first, 0.0, 1.0);
        int segWidth = static_cast<int>(totalWidth * fraction);
        if (segWidth > 0 && currentX < rc.right - 1) {
            int rightX = std::min<int>(rc.right - 1, currentX + segWidth);
            RECT rcSeg = { currentX, rc.top + 1, rightX, rc.bottom - 1 };
            HBRUSH hbrSeg = CreateSolidBrush(seg.second);
            FillRect(hdc, &rcSeg, hbrSeg);
            DeleteObject(hbrSeg);
            currentX = rightX;
        }
    }
}

void DrawActionButton(HDC hdc, const RECT& rc, const std::wstring& text, bool is_hovered, COLORREF accentCol, HFONT hFont, int id, GuiWindowState* state) {
    if (state) {
        state->hit_boxes.push_back({ id, rc });
    }

    COLORREF bgCol = is_hovered ? CLR_BTN_HOVER : CLR_BTN_BG;
    COLORREF borderCol = is_hovered ? accentCol : CLR_CARD_BORDER;
    COLORREF textCol = is_hovered ? RGB(255, 255, 255) : CLR_TEXT_TITLE;

    DrawRoundedCard(hdc, rc, bgCol, borderCol, 6);
    DrawTextRect(hdc, rc, text, textCol, hFont, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawTabButton(HDC hdc, const RECT& rc, const std::wstring& text, bool is_active, bool is_hovered, int id, GuiWindowState* state) {
    if (state) {
        state->hit_boxes.push_back({ id, rc });
    }

    COLORREF bgCol = is_active ? CLR_TAB_ACTIVE : (is_hovered ? CLR_TAB_HOVER : CLR_BG_SIDEBAR);
    COLORREF borderCol = is_active ? CLR_TAB_INDICATOR : (is_hovered ? CLR_CARD_BORDER : CLR_BG_SIDEBAR);
    COLORREF textCol = is_active ? CLR_TEXT_TITLE : (is_hovered ? CLR_TEXT_TITLE : CLR_TEXT_BODY);

    DrawRoundedCard(hdc, rc, bgCol, borderCol, 6);

    // Vertical indicator on the left if the tab is active
    if (is_active) {
        RECT rcIndicator = { rc.left + 3, rc.top + 6, rc.left + 7, rc.bottom - 6 };
        HBRUSH hbrInd = CreateSolidBrush(CLR_TAB_INDICATOR);
        FillRect(hdc, &rcIndicator, hbrInd);
        DeleteObject(hbrInd);
    }

    RECT rcText = { rc.left + 16, rc.top, rc.right - 8, rc.bottom };
    HFONT hFont = (is_active || is_hovered) ? (state ? state->hFontBold : nullptr) : (state ? state->hFontRegular : nullptr);
    DrawTextRect(hdc, rcText, text, textCol, hFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void DrawToggleSwitch(HDC hdc, const RECT& rc, bool is_on, bool is_hovered, int id, GuiWindowState* state) {
    if (state) {
        state->hit_boxes.push_back({ id, rc });
    }

    COLORREF trackCol;
    if (is_on) {
        trackCol = is_hovered ? RGB(16, 185, 129) : RGB(14, 150, 105);
    } else {
        trackCol = is_hovered ? RGB(48, 55, 70) : RGB(33, 38, 48);
    }
    COLORREF borderCol = is_hovered ? CLR_ACCENT_CYAN : CLR_CARD_BORDER;

    DrawRoundedCard(hdc, rc, trackCol, borderCol, 12);

    int h = rc.bottom - rc.top;
    int thumbSize = h - 6;
    RECT rcThumb;
    if (is_on) {
        rcThumb = { rc.right - 3 - thumbSize, rc.top + 3, rc.right - 3, rc.bottom - 3 };
    } else {
        rcThumb = { rc.left + 3, rc.top + 3, rc.left + 3 + thumbSize, rc.bottom - 3 };
    }

    HBRUSH hbrThumb = CreateSolidBrush(RGB(243, 244, 246));
    HPEN hPenThumb = CreatePen(PS_SOLID, 1, RGB(209, 213, 219));
    HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hbrThumb));
    HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPenThumb));
    RoundRect(hdc, rcThumb.left, rcThumb.top, rcThumb.right, rcThumb.bottom, thumbSize, thumbSize);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hbrThumb);
    DeleteObject(hPenThumb);

    RECT rcText;
    if (is_on) {
        rcText = { rc.left + 6, rc.top, rc.right - thumbSize - 4, rc.bottom };
        DrawTextRect(hdc, rcText, L"ON", RGB(255, 255, 255), state ? state->hFontSmallBold : nullptr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        rcText = { rc.left + thumbSize + 4, rc.top, rc.right - 6, rc.bottom };
        DrawTextRect(hdc, rcText, L"OFF", CLR_TEXT_MUTED, state ? state->hFontSmallBold : nullptr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

} // namespace vegaflow
