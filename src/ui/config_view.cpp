#include "ui/config_view.hpp"

#include <algorithm>

#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kRowH = 76;
constexpr int kHeadH = 38;
constexpr int kCtlH = 36;

using session::ConfigRow;
using session::ConfigRowKind;

std::vector<const ConfigRow*> FlatRows(const GuiWindowState* st) {
    std::vector<const ConfigRow*> rows;
    for (const auto& g : st->config_model.groups) {
        for (const auto& r : g.rows) rows.push_back(&r);
    }
    return rows;
}

std::wstring ToggleName(const ConfigRow& r) { return Utf8ToWide(r.name) + (r.on ? L": activado" : L": apagado"); }

struct Slot {
    enum Kind { Group, Row, Backups } kind = Group;
    RECT rc{};
    int group = -1;
    int flat = -1; // index in FlatRows, -1 for none
};

struct Layout {
    int title_y = 0;
    RECT reset{};
    std::vector<Slot> slots;
    RECT restore_all_btn{};
    int total = 0;
};

Layout BuildLayout(HDC hdc, const GuiWindowState* st, const RECT& content) {
    Layout l;
    const int top = content.top - st->config_scroll;
    l.title_y = top;
    l.reset = {content.right - 250, top, content.right, top + kCtlH};
    const int sub_h = TextHeight(hdc, st->hFontRegular, text::kConfigSubtitle, content.right - content.left - 270);
    int y = top + 36 + std::max(sub_h, 0) + 18;
    int flat = 0;
    for (size_t g = 0; g < st->config_model.groups.size(); ++g) {
        const auto& grp = st->config_model.groups[g];
        const int card_top = y;
        Slot head;
        head.kind = Slot::Group;
        head.rc = {content.left, y, content.right, y + kHeadH};
        head.group = static_cast<int>(g);
        l.slots.push_back(head);
        y += kHeadH;
        for (size_t r = 0; r < grp.rows.size(); ++r) {
            Slot row;
            row.kind = Slot::Row;
            row.rc = {content.left, y, content.right, y + kRowH};
            row.group = static_cast<int>(g);
            row.flat = flat++;
            l.slots.push_back(row);
            y += kRowH;
        }
        l.slots.push_back({Slot::Group, {content.left, card_top, content.right, y}, -2, -1}); // the card outline (group -2)
        y += 16;
    }
    Slot backups;
    backups.kind = Slot::Backups;
    backups.rc = {content.left, y, content.right, y + kHeadH + kRowH};
    l.slots.push_back(backups);
    l.restore_all_btn = {content.right - 16 - 132, y + kHeadH + (kRowH - kCtlH) / 2, content.right - 16, y + kHeadH + (kRowH - kCtlH) / 2 + kCtlH};
    y += kHeadH + kRowH + 16;
    l.total = y - top;
    return l;
}

RECT ToggleRect(const RECT& row) { return {row.right - 16 - 132, row.top + (kRowH - kCtlH) / 2, row.right - 16, row.top + (kRowH - kCtlH) / 2 + kCtlH}; }
RECT EditFrame(const RECT& row) {
    return {row.right - 16 - 70 - 8 - 96, row.top + (kRowH - kCtlH) / 2, row.right - 16 - 70 - 8, row.top + (kRowH - kCtlH) / 2 + kCtlH};
}
RECT EditInner(const RECT& frame) { return {frame.left + 8, frame.top + 8, frame.right - 8, frame.bottom - 8}; }

HWND MakeEdit(HWND parent, int id, const std::wstring& text, HFONT font) {
    HWND h = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (h) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(h, EM_LIMITTEXT, 12, 0);
    }
    return h;
}

std::string EditText(HWND h) {
    wchar_t buf[64] = {};
    GetWindowTextW(h, buf, 64);
    std::string out;
    for (const wchar_t* p = buf; *p; ++p) out.push_back(*p < 128 ? static_cast<char>(*p) : '?'); // digits and separators only matter
    return out;
}

} // namespace

void ConfigViewDestroy(GuiWindowState* st) {
    for (HWND h : st->config_controls) {
        if (h) DestroyWindow(h);
    }
    st->config_controls.clear();
    st->config_structure = 0;
    if (st->config_edit_brush) {
        DeleteObject(st->config_edit_brush);
        st->config_edit_brush = nullptr;
    }
}

void ConfigViewScroll(GuiWindowState* st, int delta_px) {
    st->config_scroll += delta_px;
    ConfigViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void ConfigViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->config_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    HDC hdc = GetDC(st->hwnd);
    Layout l = BuildLayout(hdc, st, content);
    const int max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->config_scroll > max_scroll || st->config_scroll < 0) {
        st->config_scroll = std::clamp(st->config_scroll, 0, max_scroll);
        l = BuildLayout(hdc, st, content);
    }
    ReleaseDC(st->hwnd, hdc);
    const auto rows = FlatRows(st);
    PlaceClipped(st->config_controls[0], l.reset, content);
    for (const Slot& s : l.slots) {
        if (s.kind != Slot::Row) continue;
        const size_t k = static_cast<size_t>(s.flat);
        if (1 + k >= st->config_controls.size()) continue;
        if (rows[k]->kind == ConfigRowKind::Toggle) PlaceClipped(st->config_controls[1 + k], ToggleRect(s.rc), content);
        else PlaceClipped(st->config_controls[1 + k], EditInner(EditFrame(s.rc)), content);
    }
    PlaceClipped(st->config_controls.back(), l.restore_all_btn, content);
}

void ConfigViewShow(GuiWindowState* st, bool show) {
    if (show) {
        ConfigViewRelayout(st);
        return;
    }
    st->config_focus_id = 0; // the keyboard leaves with the tab; a stale id would draw a focus ring on return
    for (HWND h : st->config_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void ConfigViewRefresh(HWND hwnd, GuiWindowState* st, bool force_text) {
    st->hwnd = hwnd;
    if (st->callbacks.on_PollConfig) st->config_model = st->callbacks.on_PollConfig();
    const auto rows = FlatRows(st);
    bool changed = false;
    if (st->config_controls.empty() || st->config_structure != rows.size()) {
        ConfigViewDestroy(st);
        st->config_controls.push_back(MakeOwnerButton(hwnd, kConfigResetId, text::kConfigReset));
        for (size_t k = 0; k < rows.size(); ++k) {
            const int id = kConfigRowBaseId + static_cast<int>(k);
            st->config_controls.push_back(rows[k]->kind == ConfigRowKind::Toggle ? MakeOwnerButton(hwnd, id, ToggleName(*rows[k]))
                                                                                  : MakeEdit(hwnd, id, Utf8ToWide(rows[k]->value), st->hFontRegular));
        }
        st->config_controls.push_back(MakeOwnerButton(hwnd, kConfigRestoreAllId, text::kConfigRestoreAllName));
        st->config_structure = rows.size();
        changed = true;
    } else {
        for (size_t k = 0; k < rows.size(); ++k) {
            HWND h = st->config_controls[1 + k];
            if (!h) continue;
            const std::wstring want = rows[k]->kind == ConfigRowKind::Toggle ? ToggleName(*rows[k]) : Utf8ToWide(rows[k]->value);
            if (rows[k]->kind == ConfigRowKind::Number && GetFocus() == h && !force_text) continue; // never overwrite what is being typed
            wchar_t cur[256] = {};
            GetWindowTextW(h, cur, 256);
            if (want != cur) {
                SetWindowTextW(h, want.c_str());
                changed = true;
            }
        }
    }
    if (!changed) return;
    ConfigViewRelayout(st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ConfigViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const Layout l = BuildLayout(hdc, st, content);
    const auto rows = FlatRows(st);
    DrawTextAt(hdc, content.left, l.title_y, text::kConfigTitle, CLR_TEXT_TITLE, st->hFontHeader);
    RECT sub = {content.left, l.title_y + 30, content.right - 270, l.title_y + 30 + 200};
    DrawTextRect(hdc, sub, text::kConfigSubtitle, CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    for (const Slot& s : l.slots) {
        if (s.kind == Slot::Group && s.group == -2) {
            DrawRoundedCard(hdc, s.rc, CLR_CARD_BG, CLR_CARD_BORDER, 8);
        }
    }
    auto header = [&](const RECT& rc, const std::wstring& title, const std::wstring& note) {
        RECT r = {rc.left + 1, rc.top + 1, rc.right - 1, rc.top + kHeadH};
        HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
        FillRect(hdc, &r, br);
        DeleteObject(br);
        DrawTextAt(hdc, rc.left + 20, rc.top + 10, title, CLR_TEXT_TITLE, st->hFontBold);
        if (!note.empty()) {
            const int w = TextWidth(hdc, st->hFontSmall, note);
            DrawTextAt(hdc, rc.right - 20 - w, rc.top + 12, note, CLR_TEXT_MUTED, st->hFontSmall);
        }
    };
    auto separator = [&](const RECT& rc) {
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left + 1, rc.top, nullptr);
        LineTo(hdc, rc.right - 1, rc.top);
        SelectObject(hdc, old);
        DeleteObject(pen);
    };
    auto label = [&](const RECT& rc, const std::wstring& name, const std::wstring& help, const std::wstring& chip, int right_limit) {
        DrawTextAt(hdc, rc.left + 20, rc.top + 12, name, CLR_TEXT_TITLE, st->hFontBold);
        if (!chip.empty()) {
            const int x = rc.left + 20 + TextWidth(hdc, st->hFontBold, name) + 12;
            const RECT chip_rc = {x, rc.top + 12, x + TextWidth(hdc, st->hFontSmall, chip) + 20, rc.top + 32};
            DrawBadge(hdc, chip_rc, chip, CLR_ACCENT_RED, RGB(58, 28, 31), CLR_ACCENT_RED, st->hFontSmall);
        }
        RECT h = {rc.left + 20, rc.top + 36, right_limit, rc.bottom - 6};
        DrawTextRect(hdc, h, help, CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    };

    for (const Slot& s : l.slots) {
        if (s.kind == Slot::Group && s.group >= 0) {
            const auto& g = st->config_model.groups[static_cast<size_t>(s.group)];
            header(s.rc, Utf8ToWide(g.title), Utf8ToWide(g.note));
        } else if (s.kind == Slot::Row) {
            const ConfigRow& r = *rows[static_cast<size_t>(s.flat)];
            separator(s.rc);
            if (r.kind == ConfigRowKind::Toggle) {
                label(s.rc, Utf8ToWide(r.name), Utf8ToWide(r.help), Utf8ToWide(r.chip), ToggleRect(s.rc).left - 16);
            } else {
                const RECT frame = EditFrame(s.rc);
                label(s.rc, Utf8ToWide(r.name), Utf8ToWide(r.help), L"", frame.left - 16);
                const int id = kConfigRowBaseId + s.flat;
                DrawRoundedCard(hdc, frame, CLR_BTN_BG, CLR_CONTROL_BORDER, 6);
                if (st->config_focus_id == id) DrawFocusRing(hdc, frame);
                RECT unit = {frame.right + 8, frame.top, s.rc.right - 12, frame.bottom};
                DrawTextRect(hdc, unit, Utf8ToWide(r.unit), CLR_TEXT_BODY, st->hFontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        } else if (s.kind == Slot::Backups) {
            DrawRoundedCard(hdc, s.rc, CLR_CARD_BG, CLR_CARD_BORDER, 8);
            header(s.rc, text::kConfigBackupsTitle, L"");
            const RECT row = {s.rc.left, s.rc.top + kHeadH, s.rc.right, s.rc.bottom};
            separator(row);
            label(row, text::kConfigRestoreAllName, text::kConfigRestoreAllHelp, L"", l.restore_all_btn.left - 16);
        }
    }
    RestoreDC(hdc, saved);
}

void ConfigViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    const int id = static_cast<int>(d->CtlID);
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    if (id == kConfigResetId || id == kConfigRestoreAllId) {
        DrawButtonFrame(d->hDC, d->rcItem, id == kConfigResetId ? CLR_BG_MAIN : CLR_CARD_BG, pressed);
        SetBkMode(d->hDC, TRANSPARENT);
        DrawTextRect(d->hDC, d->rcItem, id == kConfigResetId ? text::kConfigReset : text::kConfigRestoreAllButton, CLR_TEXT_TITLE, st->hFontBold,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        const auto rows = FlatRows(st);
        const size_t k = static_cast<size_t>(id - kConfigRowBaseId);
        const bool on = k < rows.size() && rows[k]->on;
        DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, pressed);
        DrawSwitchFace(d->hDC, d->rcItem, on, on ? text::kConfigOn : text::kConfigOff, st->hFontSmall);
    }
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

LRESULT ConfigViewColorEdit(HDC hdc, GuiWindowState* st) {
    if (!st->config_edit_brush) st->config_edit_brush = CreateSolidBrush(CLR_BTN_BG);
    SetTextColor(hdc, CLR_TEXT_TITLE);
    SetBkColor(hdc, CLR_BTN_BG);
    return reinterpret_cast<LRESULT>(st->config_edit_brush);
}

namespace {

bool rowsIsEdit(const GuiWindowState* st, int id) {
    const auto rows = FlatRows(st);
    const size_t k = static_cast<size_t>(id - kConfigRowBaseId);
    return k < rows.size() && rows[k]->kind == ConfigRowKind::Number;
}

// Sends a typed value to the daemon and shows what came back; the field then shows what is saved.
void CommitEdit(HWND hwnd, GuiWindowState* st, size_t k) {
    const auto rows = FlatRows(st);
    if (k >= rows.size() || 1 + k >= st->config_controls.size() || !st->config_controls[1 + k]) return;
    const std::string typed = EditText(st->config_controls[1 + k]);
    if (typed == rows[k]->value || !st->callbacks.on_SetConfigNumber) return; // nothing changed: no save
    st->data.last_action_status = st->callbacks.on_SetConfigNumber(rows[k]->id, typed);
    ConfigViewRefresh(hwnd, st, true);
}

} // namespace

// Saves a typed value still in the field that has the keyboard. Call it before the tab changes: hiding the field sends
// EN_KILLFOCUS, but by then the tab is not ours any more and ConfigViewCommand drops it.
void ConfigViewCommitPending(HWND hwnd, GuiWindowState* st) {
    if (st->current_tab != 5) return;
    HWND focus = GetFocus();
    const auto it = std::find(st->config_controls.begin(), st->config_controls.end(), focus);
    if (!focus || it == st->config_controls.end() || it == st->config_controls.begin()) return;
    const size_t k = static_cast<size_t>(it - st->config_controls.begin()) - 1;
    if (rowsIsEdit(st, kConfigRowBaseId + static_cast<int>(k))) CommitEdit(hwnd, st, k);
}

bool ConfigViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    const bool enter_in_field = (id == IDOK && notify_code == 0 && st->current_tab == 5);
    if (enter_in_field) {
        HWND focus = GetFocus();
        const auto it = std::find(st->config_controls.begin(), st->config_controls.end(), focus);
        if (focus && it != st->config_controls.end() && it != st->config_controls.begin() &&
            rowsIsEdit(st, kConfigRowBaseId + static_cast<int>(it - st->config_controls.begin()) - 1)) {
            CommitEdit(hwnd, st, static_cast<size_t>(it - st->config_controls.begin()) - 1);
        }
        return true;
    }
    if (id < kConfigResetId || id > kConfigRestoreAllId) return false;
    // Only our own controls, on our own tab, may act (a posted fake is not ours).
    if (st->current_tab != 5 || !from || GetDlgCtrlID(from) != id) return true;
    if (std::find(st->config_controls.begin(), st->config_controls.end(), from) == st->config_controls.end()) return true;
    const bool is_edit = id >= kConfigRowBaseId && id < kConfigRestoreAllId && rowsIsEdit(st, id);
    if (notify_code == BN_SETFOCUS || notify_code == EN_SETFOCUS) {
        if (is_edit) {
            st->config_focus_id = id;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) ConfigViewScroll(st, delta);
        return true;
    }
    if (is_edit) {
        if (notify_code == EN_KILLFOCUS) {
            if (st->config_focus_id == id) st->config_focus_id = 0;
            CommitEdit(hwnd, st, static_cast<size_t>(id - kConfigRowBaseId));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    }
    if (notify_code != BN_CLICKED) return true;
    if (id == kConfigResetId) {
        if (st->callbacks.on_ResetConfig) st->data.last_action_status = st->callbacks.on_ResetConfig();
        ConfigViewRefresh(hwnd, st, true);
    } else if (id == kConfigRestoreAllId) {
        if (st->callbacks.on_RestoreAllDefaults) st->data.last_action_status = st->callbacks.on_RestoreAllDefaults();
    } else {
        const auto rows = FlatRows(st);
        const size_t k = static_cast<size_t>(id - kConfigRowBaseId);
        if (k < rows.size() && st->callbacks.on_SetConfigToggle) {
            st->data.last_action_status = st->callbacks.on_SetConfigToggle(rows[k]->id, !rows[k]->on);
            ConfigViewRefresh(hwnd, st, true);
        }
    }
    return true;
}

} // namespace vegaflow::ui
