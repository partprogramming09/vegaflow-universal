#include "ui/games_view.hpp"

#include <algorithm>

#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kRowH = 64;
constexpr int kBtnH = 34;
constexpr int kToggleW = 132;
constexpr int kChipW = 128;
constexpr int kHeadTop = 40; // title row, then the subtitle (wrapped), then the card

struct RowBox {
    RECT row{};      // the collapsed part
    RECT body{};     // empty unless this row is open
    int detail_y = 0;
    int detail_h = 0;
    int file_y = 0;
    int backup_y = 0;
    int backup_h = 0;
    int changes_y = 0;
    std::vector<int> change_h;
    int dxvk_y = 0;
    int dxvk_h = 0;
    int buttons_y = 0;
};

struct Layout {
    RECT rescan{};
    RECT card{};
    std::vector<RowBox> rows;
    int note_y = 0;
    int head_h = 0; // title row + subtitle + gap: where the card starts
    int sub_h = 0;
    int total = 0; // unscrolled height
};

size_t ShownRows(const GuiWindowState* st) { return std::min(st->games_model.rows.size(), kGamesMaxRows); }

// Wrapped text needs a DC to be measured; a screen DC with the window's fonts gives the same heights as painting.
Layout BuildLayout(GuiWindowState* st, const RECT& content) {
    Layout l;
    const int top = content.top - st->games_scroll;
    l.rescan = {content.right - 220, top, content.right, top + kBtnH};
    const int width = content.right - content.left;
    HDC dc = GetDC(st->hwnd);
    l.sub_h = TextHeight(dc, st->hFontRegular, text::kGamesSubtitle, width);
    l.head_h = std::max(kHeadTop + l.sub_h + 12, 78);
    int y = top + l.head_h;
    const int card_top = y;
    for (size_t i = 0; i < ShownRows(st); ++i) {
        const auto& g = st->games_model.rows[i];
        RowBox b;
        b.row = {content.left, y, content.right, y + kRowH};
        y += kRowH;
        if (g.app_id == st->games_open_app_id) {
            const int inner = width - 40;
            const int body_top = y;
            y += 8;
            b.detail_y = y;
            b.detail_h = TextHeight(dc, st->hFontSmall, Utf8ToWide(g.detail), inner) + 6;
            y += b.detail_h;
            b.file_y = y;
            y += g.file_hint.empty() ? 0 : 22;
            b.backup_y = y;
            b.backup_h = g.backup_hint.empty() ? 0 : TextHeight(dc, st->hFontSmall, Utf8ToWide(g.backup_hint), inner) + 6;
            y += b.backup_h;
            b.dxvk_y = y;
            b.dxvk_h = g.dxvk_hint.empty() ? 0 : TextHeight(dc, st->hFontSmall, Utf8ToWide(g.dxvk_hint), inner) + 6;
            y += b.dxvk_h + 8;
            b.buttons_y = y; // the buttons come before the long list of lines, so they are not a scroll away
            y += kBtnH + 14;
            b.changes_y = y;
            if (!g.changes.empty()) y += 26;
            for (const auto& c : g.changes) {
                b.change_h.push_back(TextHeight(dc, st->hFontSmall, Utf8ToWide(c), inner - 16) + 2);
                y += b.change_h.back();
            }
            y += 12;
            b.body = {content.left, body_top, content.right, y};
        }
        l.rows.push_back(std::move(b));
    }
    ReleaseDC(st->hwnd, dc);
    const int card_h = l.rows.empty() ? 110 : y - card_top;
    l.card = {content.left, card_top, content.right, card_top + card_h};
    y = card_top + card_h + 16;
    l.note_y = y;
    if (st->games_model.rows.size() > kGamesMaxRows) y += 24;
    l.total = y - top;
    return l;
}

const session::GameRow* OpenRow(const GuiWindowState* st) {
    for (const auto& g : st->games_model.rows) {
        if (g.app_id == st->games_open_app_id) return &g;
    }
    return nullptr;
}

const wchar_t* ActionText(int id);
const wchar_t* ActionLabel(int id, const session::GameRow* g) {
    if (id == kGamesDxvkId && g && g->dxvk_installed) return text::kGamesDxvkRemove;
    return ActionText(id);
}

const wchar_t* ActionText(int id) {
    switch (id) {
        case kGamesApplyId: return text::kGamesApply;
        case kGamesRestoreId: return text::kGamesRestore;
        case kGamesBadId: return text::kGamesBad;
        case kGamesIncludeId: return text::kGamesInclude;
        case kGamesDxvkId: return text::kGamesDxvkInstall;
        default: return text::kGamesRescan;
    }
}

int ActionWidth(int id) {
    switch (id) {
        case kGamesApplyId: return 150;
        case kGamesRestoreId: return 230;
        case kGamesBadId: return 250;
        case kGamesDxvkId: return 170;
        default: return 170;
    }
}

HWND FindControl(const GuiWindowState* st, int id) {
    for (HWND h : st->games_controls) {
        if (h && GetDlgCtrlID(h) == id) return h;
    }
    return nullptr;
}

std::vector<int> ActionsFor(const session::GameRow& g) {
    std::vector<int> ids;
    if (g.can_apply) ids.push_back(kGamesApplyId);
    if (g.can_restore) ids.push_back(kGamesRestoreId);
    if (g.can_dxvk) ids.push_back(kGamesDxvkId);
    if (g.excluded) ids.push_back(kGamesIncludeId);
    else if (!g.exe.empty()) ids.push_back(kGamesBadId);
    return ids;
}

std::wstring ToggleName(const session::GameRow& g, bool open) { return std::wstring(open ? text::kGamesHide : text::kGamesShow) + L": " + Utf8ToWide(g.name); }

std::string Signature(const GuiWindowState* st) {
    std::string s = std::to_string(st->games_open_app_id);
    for (size_t i = 0; i < ShownRows(st); ++i) {
        const auto& g = st->games_model.rows[i];
        s += "|" + std::to_string(g.app_id) + ":" + std::to_string(static_cast<int>(g.status)) + (g.can_apply ? "a" : "") + (g.can_restore ? "r" : "") + (g.excluded ? "x" : "") + (g.dxvk_installed ? "d" : "") + (g.exe.empty() ? "n" : "") + (g.can_dxvk ? "v" : "");
    }
    return s;
}

COLORREF StatusText(session::GameStatus s) {
    switch (s) {
        case session::GameStatus::Applied: return RGB(167, 243, 208);
        case session::GameStatus::Protected:
        case session::GameStatus::Excluded: return RGB(253, 231, 184);
        default: return RGB(229, 231, 235);
    }
}
COLORREF StatusFill(session::GameStatus s) {
    switch (s) {
        case session::GameStatus::Applied: return RGB(15, 46, 36);
        case session::GameStatus::Protected:
        case session::GameStatus::Excluded: return RGB(58, 46, 18);
        default: return RGB(42, 47, 58);
    }
}

} // namespace

void GamesViewDestroy(GuiWindowState* st) {
    for (HWND h : st->games_controls) {
        if (h) DestroyWindow(h);
    }
    st->games_controls.clear();
}

void GamesViewScroll(GuiWindowState* st, int delta_px) {
    st->games_scroll += delta_px;
    GamesViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void GamesViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->games_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    Layout l = BuildLayout(st, content);
    const int max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->games_scroll > max_scroll || st->games_scroll < 0) {
        st->games_scroll = std::clamp(st->games_scroll, 0, max_scroll);
        l = BuildLayout(st, content);
    }
    PlaceClipped(FindControl(st, kGamesRescanId), l.rescan, content);
    for (size_t i = 0; i < l.rows.size(); ++i) {
        const RECT& r = l.rows[i].row;
        PlaceClipped(FindControl(st, kGamesRowBaseId + static_cast<int>(i)),
                     {r.right - 16 - kToggleW, r.top + (kRowH - kBtnH) / 2, r.right - 16, r.top + (kRowH - kBtnH) / 2 + kBtnH}, content);
        if (l.rows[i].body.bottom == 0 && l.rows[i].body.top == 0) continue;
        const auto& g = st->games_model.rows[i];
        int x = content.left + 20;
        for (int id : ActionsFor(g)) {
            const int w = ActionWidth(id);
            PlaceClipped(FindControl(st, id), {x, l.rows[i].buttons_y, x + w, l.rows[i].buttons_y + kBtnH}, content);
            x += w + 12;
        }
    }
}

void GamesViewShow(GuiWindowState* st, bool show) {
    if (show) {
        GamesViewRelayout(st);
        return;
    }
    for (HWND h : st->games_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void GamesViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->callbacks.on_PollGames) st->games_model = st->callbacks.on_PollGames();
    if (st->games_open_app_id != 0 && !OpenRow(st)) st->games_open_app_id = 0; // the game left the library
    const std::string signature = Signature(st);
    if (signature == st->games_signature && !st->games_controls.empty()) return; // nothing changed: no repaint, no flicker
    int focus_id = 0;
    const HWND focused = GetFocus();
    for (HWND h : st->games_controls) {
        if (h && h == focused) focus_id = GetDlgCtrlID(h);
    }
    GamesViewDestroy(st);
    st->games_controls.push_back(MakeOwnerButton(hwnd, kGamesRescanId, text::kGamesRescan));
    // Tab order follows what is on screen: each row's button, then the action buttons of the row that is open.
    for (size_t i = 0; i < ShownRows(st); ++i) {
        const auto& g = st->games_model.rows[i];
        const bool open = g.app_id == st->games_open_app_id;
        st->games_controls.push_back(MakeOwnerButton(hwnd, kGamesRowBaseId + static_cast<int>(i), ToggleName(g, open)));
        if (!open) continue;
        for (int id : ActionsFor(g)) st->games_controls.push_back(MakeOwnerButton(hwnd, id, ActionLabel(id, &g)));
    }
    st->games_signature = signature;
    GamesViewRelayout(st);
    if (focus_id != 0) { // keep the keyboard where it was; a button that is gone sends it to the row's own button
        HWND target = FindControl(st, focus_id);
        if (!target) {
            for (size_t i = 0; i < ShownRows(st) && !target; ++i) {
                if (st->games_model.rows[i].app_id == st->games_open_app_id) target = FindControl(st, kGamesRowBaseId + static_cast<int>(i));
            }
        }
        SetFocus(target ? target : st->games_controls[0]);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void GamesViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const Layout l = BuildLayout(st, content);
    DrawTextAt(hdc, content.left, l.card.top - l.head_h, text::kGamesTitle, CLR_TEXT_TITLE, st->hFontHeader);
    DrawTextRect(hdc, {content.left, l.card.top - l.head_h + kHeadTop, content.right, l.card.top - 8}, text::kGamesSubtitle, CLR_TEXT_BODY, st->hFontRegular,
                 DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    DrawRoundedCard(hdc, l.card, CLR_CARD_BG, CLR_CARD_BORDER, 8);

    if (st->games_model.rows.empty()) {
        RECT r = {l.card.left + 24, l.card.top + 20, l.card.right - 24, l.card.bottom - 10};
        DrawTextRect(hdc, r, Utf8ToWide(st->games_model.empty_text), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    auto line = [&](int y) {
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, content.left + 1, y, nullptr);
        LineTo(hdc, content.right - 1, y);
        SelectObject(hdc, old);
        DeleteObject(pen);
    };
    for (size_t i = 0; i < l.rows.size(); ++i) {
        const RowBox& b = l.rows[i];
        const auto& g = st->games_model.rows[i];
        if (i > 0) line(b.row.top);
        const int text_right = b.row.right - 16 - kToggleW - 12 - kChipW - 12;
        RECT name = {b.row.left + 20, b.row.top + 10, text_right, b.row.top + 30};
        DrawTextRect(hdc, name, Utf8ToWide(g.name), CLR_TEXT_TITLE, st->hFontBold, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        RECT sub = {b.row.left + 20, b.row.top + 34, text_right, b.row.top + 56};
        DrawTextRect(hdc, sub, Utf8ToWide(g.engine + " · " + g.detail), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        const RECT chip = {b.row.right - 16 - kToggleW - 12 - kChipW, b.row.top + (kRowH - 22) / 2, b.row.right - 16 - kToggleW - 12, b.row.top + (kRowH - 22) / 2 + 22};
        DrawBadge(hdc, chip, Utf8ToWide(session::StatusLabel(g.status)), StatusText(g.status), StatusFill(g.status), StatusText(g.status), st->hFontSmallBold);
        if (b.body.bottom == 0 && b.body.top == 0) continue;
        const int left = content.left + 20;
        const int right = content.right - 20;
        DrawTextRect(hdc, {left, b.detail_y, right, b.detail_y + b.detail_h}, Utf8ToWide(g.detail), CLR_TEXT_BODY, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        if (!g.file_hint.empty()) {
            DrawTextRect(hdc, {left, b.file_y, right, b.file_y + 20}, std::wstring(text::kGamesFile) + Utf8ToWide(g.file_hint), CLR_TEXT_BODY, st->hFontSmall,
                         DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        }
        if (b.backup_h > 0) {
            DrawTextRect(hdc, {left, b.backup_y, right, b.backup_y + b.backup_h}, Utf8ToWide(g.backup_hint), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        if (b.dxvk_h > 0) {
            DrawTextRect(hdc, {left, b.dxvk_y, right, b.dxvk_y + b.dxvk_h}, Utf8ToWide(g.dxvk_hint), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        if (!g.changes.empty()) {
            DrawTextAt(hdc, left, b.changes_y + 2, text::kGamesChanges, CLR_TEXT_TITLE, st->hFontSmallBold);
            int y = b.changes_y + 26;
            for (size_t k = 0; k < g.changes.size(); ++k) {
                DrawTextRect(hdc, {left + 16, y, right, y + b.change_h[k]}, Utf8ToWide(g.changes[k]), CLR_TEXT_BODY, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                y += b.change_h[k];
            }
        }
    }
    if (st->games_model.rows.size() > kGamesMaxRows) DrawTextAt(hdc, content.left, l.note_y, text::kGamesTooMany, CLR_TEXT_MUTED, st->hFontSmall);
    RestoreDC(hdc, saved);
}

void GamesViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    const int id = static_cast<int>(d->CtlID);
    const bool row = id >= kGamesRowBaseId && id < kGamesApplyId;
    DrawButtonFrame(d->hDC, d->rcItem, row ? CLR_CARD_BG : CLR_BG_MAIN, (d->itemState & ODS_SELECTED) != 0);
    SetBkMode(d->hDC, TRANSPARENT);
    std::wstring label;
    if (row) {
        const size_t i = static_cast<size_t>(id - kGamesRowBaseId);
        label = (i < ShownRows(st) && st->games_model.rows[i].app_id == st->games_open_app_id) ? text::kGamesHide : text::kGamesShow;
    } else {
        label = ActionLabel(id, OpenRow(st));
    }
    DrawTextRect(d->hDC, d->rcItem, label, CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

bool GamesViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id < kGamesRowBaseId || id > kGamesLastId) return false;
    // Only our own controls, on our own tab, may act: a posted fake (no sender, a foreign window, or an id that is not the
    // sender's) must not reach the system.
    if (st->current_tab != 3 || !from || GetDlgCtrlID(from) != id) return true;
    if (std::find(st->games_controls.begin(), st->games_controls.end(), from) == st->games_controls.end()) return true;
    if (notify_code == BN_SETFOCUS) {
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) GamesViewScroll(st, delta);
        return true;
    }
    if (notify_code != BN_CLICKED) return true;
    auto& cb = st->callbacks;
    if (id == kGamesRescanId) {
        if (cb.on_RescanSteamLibrary) st->data.last_action_status = cb.on_RescanSteamLibrary();
    } else if (id < kGamesApplyId) {
        const size_t i = static_cast<size_t>(id - kGamesRowBaseId);
        if (i < ShownRows(st)) {
            const uint32_t app = st->games_model.rows[i].app_id;
            st->games_open_app_id = (st->games_open_app_id == app) ? 0 : app;
        }
    } else if (const auto* g = OpenRow(st)) { // the action buttons belong to the open row, and only the ones its state allows
        const uint32_t app = g->app_id;
        if (id == kGamesApplyId && g->can_apply && cb.on_ApplyGameEnginePreset) st->data.last_action_status = cb.on_ApplyGameEnginePreset(app);
        else if (id == kGamesRestoreId && g->can_restore && cb.on_RestoreGameEngineConfig) st->data.last_action_status = cb.on_RestoreGameEngineConfig(app);
        else if (id == kGamesBadId && !g->excluded && cb.on_ExcludeGame) st->data.last_action_status = cb.on_ExcludeGame(app);
        else if (id == kGamesDxvkId && g->can_dxvk && cb.on_ToggleGameDxvk) st->data.last_action_status = cb.on_ToggleGameDxvk(app);
        else if (id == kGamesIncludeId && g->excluded && cb.on_IncludeGame) st->data.last_action_status = cb.on_IncludeGame(app);
    }
    GamesViewRefresh(hwnd, st);
    return true;
}

} // namespace vegaflow::ui
