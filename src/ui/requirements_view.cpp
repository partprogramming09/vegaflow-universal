#include "ui/requirements_view.hpp"

#include <algorithm>

#include "session/requirements_screen.hpp"
#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kHeadH = 38;
constexpr int kBackH = 34;

session::RequirementsScreenModel Model(const GuiWindowState* st) {
    return session::BuildRequirementsScreen(st->data.system_info, st->data.is_elevated, st->data.has_steam_games);
}

struct Look {
    COLORREF text, fill;
};
Look LookOf(session::RequirementState s) {
    switch (s) {
        case session::RequirementState::Meets: return {RGB(167, 243, 208), RGB(15, 46, 36)};
        case session::RequirementState::Missing: return {RGB(252, 165, 165), RGB(58, 28, 31)};
        case session::RequirementState::Limited: return {RGB(253, 231, 184), RGB(58, 46, 18)};
        default: return {RGB(229, 231, 235), RGB(42, 47, 58)}; // unknown, optional
    }
}

RECT BackRect(const RECT& content, int scroll) { return {content.left, content.top - scroll, content.left + 220, content.top - scroll + kBackH}; }

} // namespace

void RequirementsViewDestroy(GuiWindowState* st) {
    for (HWND h : st->requirements_controls) {
        if (h) DestroyWindow(h);
    }
    st->requirements_controls.clear();
}

void RequirementsViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->requirements_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    PlaceClipped(st->requirements_controls[0], BackRect(content, st->requirements_scroll), content);
}

void RequirementsViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->requirements_controls.empty()) st->requirements_controls.push_back(MakeOwnerButton(hwnd, kRequirementsBackId, text::kReqBack));
    RequirementsViewRelayout(st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void RequirementsViewShow(GuiWindowState* st, bool show) {
    if (show) {
        RequirementsViewRelayout(st);
        return;
    }
    for (HWND h : st->requirements_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void RequirementsViewScroll(GuiWindowState* st, int delta_px) {
    st->requirements_scroll = std::clamp(st->requirements_scroll + delta_px, 0, st->requirements_max_scroll);
    RequirementsViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void RequirementsViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const auto m = Model(st);
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const int width = content.right - content.left;
    const int top = content.top - st->requirements_scroll;
    int y = top + kBackH + 12;
    DrawTextAt(hdc, content.left, y, text::kReqTitle, CLR_TEXT_TITLE, st->hFontHeader);
    y += 40;

    auto header = [&](const RECT& card, const wchar_t* title) {
        RECT r = {card.left + 1, card.top + 1, card.right - 1, card.top + kHeadH};
        HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
        FillRect(hdc, &r, br);
        DeleteObject(br);
        DrawTextAt(hdc, card.left + 20, card.top + 10, title, CLR_TEXT_TITLE, st->hFontBold);
    };

    // Requirements
    constexpr int kChipW = 120;
    std::vector<int> heights;
    int card_h = kHeadH;
    for (const auto& r : m.rows) {
        const int h = std::max(56, 36 + TextHeight(hdc, st->hFontSmall, Utf8ToWide(r.detail), width - 40 - kChipW - 16) + 12);
        heights.push_back(h);
        card_h += h;
    }
    RECT card = {content.left, y, content.right, y + card_h};
    DrawRoundedCard(hdc, card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    header(card, text::kReqNeedsTitle);
    int ry = y + kHeadH;
    for (size_t i = 0; i < m.rows.size(); ++i) {
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, content.left + 1, ry, nullptr);
        LineTo(hdc, content.right - 1, ry);
        SelectObject(hdc, old);
        DeleteObject(pen);
        DrawTextAt(hdc, content.left + 20, ry + 10, Utf8ToWide(m.rows[i].what), CLR_TEXT_TITLE, st->hFontBold);
        RECT d = {content.left + 20, ry + 32, content.right - 20 - kChipW - 16, ry + heights[i]};
        DrawTextRect(hdc, d, Utf8ToWide(m.rows[i].detail), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        const Look look = LookOf(m.rows[i].state);
        const RECT chip = {content.right - 20 - kChipW, ry + 10, content.right - 20, ry + 32};
        DrawBadge(hdc, chip, Utf8ToWide(session::StateLabel(m.rows[i].state)), look.text, look.fill, look.text, st->hFontSmallBold);
        ry += heights[i];
    }
    y = card.bottom + 16;

    // Warnings
    int wh = kHeadH + 12;
    std::vector<int> wheights;
    for (const auto& w : m.warnings) {
        wheights.push_back(TextHeight(hdc, st->hFontRegular, Utf8ToWide(w), width - 40 - 20) + 10);
        wh += wheights.back();
    }
    card = {content.left, y, content.right, y + wh + 6};
    DrawRoundedCard(hdc, card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    header(card, text::kReqWarnTitle);
    int wy = y + kHeadH + 12;
    for (size_t i = 0; i < m.warnings.size(); ++i) {
        DrawTextAt(hdc, content.left + 20, wy, L"•", CLR_TEXT_BODY, st->hFontRegular);
        RECT t = {content.left + 40, wy, content.right - 20, wy + wheights[i]};
        DrawTextRect(hdc, t, Utf8ToWide(m.warnings[i]), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        wy += wheights[i];
    }
    y = card.bottom + 16;

    st->requirements_max_scroll = std::max(0, (y - top) - static_cast<int>(content.bottom - content.top));
    const int clamped = std::clamp(st->requirements_scroll, 0, st->requirements_max_scroll);
    if (clamped != st->requirements_scroll) {
        st->requirements_scroll = clamped;
        RequirementsViewRelayout(st);
        InvalidateRect(st->hwnd, nullptr, FALSE);
    }
    RestoreDC(hdc, saved);
}

void RequirementsViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    DrawButtonFrame(d->hDC, d->rcItem, CLR_BG_MAIN, (d->itemState & ODS_SELECTED) != 0);
    SetBkMode(d->hDC, TRANSPARENT);
    DrawTextRect(d->hDC, d->rcItem, text::kReqBack, CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

bool RequirementsViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id != kRequirementsBackId) return false;
    if (st->current_tab != 8 || !from || GetDlgCtrlID(from) != id || st->requirements_controls.empty() || st->requirements_controls[0] != from) return true;
    if (notify_code == BN_SETFOCUS) {
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) RequirementsViewScroll(st, delta);
        return true;
    }
    if (notify_code == BN_CLICKED) SelectTab(hwnd, st, 5);
    return true;
}

} // namespace vegaflow::ui
