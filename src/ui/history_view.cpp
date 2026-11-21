#include "ui/history_view.hpp"

#include <algorithm>

#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kRowH = 76;
constexpr int kBtnH = 36;
constexpr int kBtnW = 132;

struct Layout {
    int title_y = 0;
    RECT restore_all{};
    RECT card{};
    std::vector<RECT> rows;
    int total = 0; // unscrolled height
};

std::wstring UndoName(const session::HistoryRow& r) { return std::wstring(text::kUndo) + L": " + Utf8ToWide(r.what); }

Layout BuildLayout(const GuiWindowState* st, const RECT& content) {
    Layout l;
    const int top = content.top - st->history_scroll;
    l.title_y = top;
    l.restore_all = {content.right - 250, top, content.right, top + kBtnH};
    int y = top + 70;
    const int n = static_cast<int>(st->history_model.rows.size());
    const int card_h = n == 0 ? 110 : n * kRowH;
    l.card = {content.left, y, content.right, y + card_h};
    for (int i = 0; i < n; ++i) l.rows.push_back({content.left, y + i * kRowH, content.right, y + (i + 1) * kRowH});
    l.total = (y + card_h + 16) - top;
    return l;
}

RECT UndoButtonRect(const RECT& row) {
    return {row.right - 16 - kBtnW, row.top + (kRowH - kBtnH) / 2, row.right - 16, row.top + (kRowH - kBtnH) / 2 + kBtnH};
}

void DestroyControls(GuiWindowState* st) {
    for (HWND h : st->history_controls) {
        if (h) DestroyWindow(h);
    }
    st->history_controls.clear();
}

} // namespace

void HistoryViewScroll(GuiWindowState* st, int delta_px) {
    st->history_scroll += delta_px;
    HistoryViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void HistoryViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->history_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    Layout l = BuildLayout(st, content);
    const int max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->history_scroll > max_scroll || st->history_scroll < 0) {
        st->history_scroll = std::clamp(st->history_scroll, 0, max_scroll);
        l = BuildLayout(st, content);
    }
    PlaceClipped(st->history_controls[0], l.restore_all, content);
    for (size_t i = 0; i < l.rows.size() && 1 + i < st->history_controls.size(); ++i) {
        PlaceClipped(st->history_controls[1 + i], UndoButtonRect(l.rows[i]), content);
    }
}

void HistoryViewShow(GuiWindowState* st, bool show) {
    if (show) {
        HistoryViewRelayout(st);
        return;
    }
    for (HWND h : st->history_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void HistoryViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    const auto old_rows = st->history_model.rows;
    if (st->callbacks.on_PollHistory) st->history_model = st->callbacks.on_PollHistory();
    std::vector<std::string> signature;
    for (const auto& r : st->history_model.rows) signature.push_back(std::to_string(r.id) + ":" + r.state);
    if (signature == st->history_signature && !st->history_controls.empty()) return; // nothing changed: no repaint, no flicker
    const HWND focused = GetFocus();
    // Remember the focused row by its journal id: positions shift when a new change arrives at the top.
    uint64_t focused_row_id = 0;
    bool focus_on_restore_all = false;
    for (size_t i = 0; i < st->history_controls.size(); ++i) {
        if (st->history_controls[i] && st->history_controls[i] == focused) {
            if (i == 0) focus_on_restore_all = true;
            else if (i - 1 < old_rows.size()) focused_row_id = old_rows[i - 1].id;
        }
    }
    DestroyControls(st);
    st->history_controls.push_back(MakeOwnerButton(hwnd, kHistoryRestoreAllId, text::kHistoryRestoreAll));
    std::vector<int> used;
    for (size_t i = 0; i < st->history_model.rows.size(); ++i) {
        const auto& r = st->history_model.rows[i];
        if (!r.can_undo) {
            st->history_controls.push_back(nullptr);
            continue;
        }
        int id = kHistoryRowBaseId + static_cast<int>(r.id % 1000);
        while (std::find(used.begin(), used.end(), id) != used.end()) id = id == kHistoryLastId ? kHistoryRowBaseId : id + 1;
        used.push_back(id);
        st->history_controls.push_back(MakeOwnerButton(hwnd, id, UndoName(r)));
    }
    st->history_signature = signature;
    HistoryViewRelayout(st);
    if (focused_row_id != 0 || focus_on_restore_all) { // keep the keyboard where it was; after an undo that row has no button, so go to the first
        HWND target = st->history_controls[0];
        for (size_t i = 0; i < st->history_model.rows.size(); ++i) {
            if (st->history_model.rows[i].id == focused_row_id && st->history_controls[1 + i]) target = st->history_controls[1 + i];
        }
        SetFocus(target); // after the relayout: a button still at its creation size would make the scroll jump
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void HistoryViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const Layout l = BuildLayout(st, content);
    DrawTextAt(hdc, content.left, l.title_y, text::kHistoryTitle, CLR_TEXT_TITLE, st->hFontHeader);
    DrawTextAt(hdc, content.left, l.title_y + 28, text::kHistorySubtitle, CLR_TEXT_BODY, st->hFontRegular);
    DrawRoundedCard(hdc, l.card, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    if (st->history_model.rows.empty()) {
        RECT r = {l.card.left + 24, l.card.top + 20, l.card.right - 24, l.card.bottom - 10};
        DrawTextRect(hdc, r, Utf8ToWide(st->history_model.empty_text), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    for (size_t i = 0; i < l.rows.size(); ++i) {
        const RECT& rc = l.rows[i];
        const auto& row = st->history_model.rows[i];
        if (i > 0) {
            HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, rc.left + 1, rc.top, nullptr);
            LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, old);
            DeleteObject(pen);
        }
        const RECT btn = UndoButtonRect(rc);
        DrawTextAt(hdc, rc.left + 20, rc.top + 10, Utf8ToWide(row.when), CLR_TEXT_MUTED, st->hFontSmall);
        RECT what = {rc.left + 20, rc.top + 28, btn.left - 130, rc.top + 48};
        DrawTextRect(hdc, what, Utf8ToWide(row.what), CLR_TEXT_TITLE, st->hFontBold, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        RECT ba = {rc.left + 20, rc.top + 50, btn.left - 130, rc.top + 70};
        const std::wstring line = row.manual_hint.empty() ? std::wstring(text::kBefore) + Utf8ToWide(row.before) + text::kNow + Utf8ToWide(row.after)
                                                          : Utf8ToWide(row.manual_hint);
        DrawTextRect(hdc, ba, line, CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        const bool active = row.state == "Activo";
        const std::wstring state = Utf8ToWide(row.state);
        const int sw = TextWidth(hdc, st->hFontSmallBold, state) + 20;
        RECT chip = {btn.left - 12 - sw, rc.top + (kRowH - 20) / 2, btn.left - 12, rc.top + (kRowH - 20) / 2 + 20};
        DrawBadge(hdc, chip, state, active ? CLR_ACCENT_GREEN : CLR_TEXT_MUTED, active ? RGB(15, 46, 36) : RGB(42, 47, 58),
                  active ? CLR_ACCENT_GREEN : CLR_CARD_BORDER, st->hFontSmallBold);
        if (!row.can_undo) {
            DrawTextRect(hdc, btn, text::kNoUndoNeeded, CLR_TEXT_MUTED, st->hFontRegular, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    RestoreDC(hdc, saved);
}

void HistoryViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    HDC hdc = d->hDC;
    const RECT rc = d->rcItem;
    const bool all = static_cast<int>(d->CtlID) == kHistoryRestoreAllId;
    DrawButtonFrame(hdc, rc, all ? CLR_BG_MAIN : CLR_CARD_BG, (d->itemState & ODS_SELECTED) != 0);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextRect(hdc, rc, all ? text::kHistoryRestoreAll : text::kUndo, CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) DrawFocusRing(hdc, rc);
}

bool HistoryViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id < kHistoryRestoreAllId || id > kHistoryLastId) return false;
    // Only our own controls, on our own tab, may act: WM_COMMAND is let through the UIPI filter for the tray, so a posted
    // fake (no sender, a foreign window, or an id that is not the sender's) must not reach the system.
    if (st->current_tab != 4 || !from || GetDlgCtrlID(from) != id) return true;
    if (std::find(st->history_controls.begin(), st->history_controls.end(), from) == st->history_controls.end()) return true;
    if (notify_code == BN_SETFOCUS) {
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) HistoryViewScroll(st, delta);
        return true;
    }
    if (notify_code != BN_CLICKED) return true;
    if (id == kHistoryRestoreAllId) {
        if (st->callbacks.on_RestoreAllDefaults) st->data.last_action_status = st->callbacks.on_RestoreAllDefaults();
    } else {
        const auto it = std::find(st->history_controls.begin(), st->history_controls.end(), from);
        const size_t i = static_cast<size_t>(it - st->history_controls.begin()) - 1; // controls[0] is restore-all
        if (i < st->history_model.rows.size() && st->history_model.rows[i].can_undo && st->callbacks.on_UndoChange) {
            st->data.last_action_status = st->callbacks.on_UndoChange(st->history_model.rows[i].id);
        }
    }
    HistoryViewRefresh(hwnd, st);
    return true;
}

} // namespace vegaflow::ui
