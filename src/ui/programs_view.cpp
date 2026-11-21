#include "ui/programs_view.hpp"

#include <algorithm>

#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kKinds = 7;
constexpr int kChoices = 3;
constexpr int kKindRowH = 64;
constexpr int kHeadH = 38;
constexpr int kRowH = 56;
constexpr int kAddH = 84; // a hint line above the field, then the field and its button
constexpr int kCtlH = 34;
constexpr int kChoiceW = 108;

const ProcessAction kChoiceAction[kChoices] = {ProcessAction::Throttle, ProcessAction::EcoOnly, ProcessAction::DoNotTouch};
const wchar_t* const kChoiceLabel[kChoices] = {L"Frenar", L"Solo ahorro", L"No tocar"};
// Selected fill and text, from the design: amber, blue, green.
const COLORREF kChoiceFill[kChoices] = {RGB(58, 46, 18), RGB(30, 42, 68), RGB(15, 46, 36)};
const COLORREF kChoiceText[kChoices] = {RGB(253, 231, 184), RGB(219, 230, 253), RGB(167, 243, 208)};

int ChoiceOf(ProcessAction a) {
    for (int o = 0; o < kChoices; ++o) {
        if (kChoiceAction[o] == a) return o;
    }
    return -1; // BoostAsGame has no button
}

size_t ExcShown(const GuiWindowState* st) { return std::min(st->programs_model.exceptions.size(), kProgMaxRows); }
size_t GamesShown(const GuiWindowState* st) { return std::min(st->programs_model.games.size(), kProgMaxRows); }

// Index of each control in programs_controls.
size_t KindIdx(int c, int o) { return static_cast<size_t>(c * kChoices + o); }
size_t ExcIdx(size_t i) { return kKinds * kChoices + 2 * i; }
size_t ExcEditIdx(size_t n) { return kKinds * kChoices + 2 * n; }
size_t GameIdx(size_t n, size_t j) { return kKinds * kChoices + 2 * n + 2 + j; }
size_t GameEditIdx(size_t n, size_t m) { return kKinds * kChoices + 2 * n + 2 + m; }
size_t ControlCount(size_t n, size_t m) { return kKinds * kChoices + 2 * n + 2 + m + 2; }

std::wstring KindName(const GuiWindowState* st, int c, int o) {
    const auto& row = st->programs_model.categories[static_cast<size_t>(c)];
    std::wstring name = std::wstring(kChoiceLabel[o]) + L": " + Utf8ToWide(row.title);
    if (ChoiceOf(row.action) == o) name += text::kProgSelected;
    return name;
}
std::wstring ActionName(const GuiWindowState* st, size_t i) {
    const auto& e = st->programs_model.exceptions[i];
    return Utf8ToWide(e.exe) + L": " + Utf8ToWide(session::ActionLabel(e.action)) + L". Pulsa para cambiar la acción";
}
std::wstring RemoveName(const std::string& exe) { return std::wstring(text::kProgRemove) + L" " + Utf8ToWide(exe); }

struct Item {
    enum Kind { Card, Header, Label, Line, Empty, Text, EditFrame } kind = Card;
    RECT rc{};
    std::wstring a;
    std::wstring b;
    int id = 0;
};

struct Layout {
    int title_y = 0;
    std::vector<Item> items;
    std::vector<RECT> ctl;
    int total = 0;
};

RECT Centered(int left, int right, int row_top, int row_h, int h) { return {left, row_top + (row_h - h) / 2, right, row_top + (row_h - h) / 2 + h}; }

Layout BuildLayout(HDC hdc, const GuiWindowState* st, const RECT& content) {
    Layout l;
    const size_t n = ExcShown(st);
    const size_t m = GamesShown(st);
    l.ctl.assign(ControlCount(n, m), RECT{});
    const int top = content.top - st->programs_scroll;
    const int width = content.right - content.left;
    l.title_y = top;
    const int sub_h = TextHeight(hdc, st->hFontRegular, text::kProgSubtitle, width);
    const int legend_h = TextHeight(hdc, st->hFontSmall, text::kProgLegend, width);
    int y = top + 34 + sub_h + 8;
    l.items.push_back({Item::Text, {content.left, y, content.right, y + legend_h}, text::kProgLegend, L"", 0});
    y += legend_h + 16;

    auto card = [&](int card_top, int card_bottom) { l.items.push_back({Item::Card, {content.left, card_top, content.right, card_bottom}, L"", L"", 0}); };
    auto header = [&](int& yy, const wchar_t* title) {
        l.items.push_back({Item::Header, {content.left, yy, content.right, yy + kHeadH}, title, L"", 0});
        yy += kHeadH;
    };

    // Kinds
    int card_top = y;
    header(y, text::kProgCategoriesTitle);
    for (int c = 0; c < kKinds; ++c) {
        const auto& row = st->programs_model.categories[static_cast<size_t>(c)];
        const RECT rc = {content.left, y, content.right, y + kKindRowH};
        l.items.push_back({Item::Label, rc, Utf8ToWide(row.title), Utf8ToWide(row.apps), 0});
        const int group_left = content.right - 16 - kChoices * kChoiceW - (kChoices - 1) * 6;
        for (int o = 0; o < kChoices; ++o) {
            const int x = group_left + o * (kChoiceW + 6);
            l.ctl[KindIdx(c, o)] = Centered(x, x + kChoiceW, y, kKindRowH, kCtlH);
        }
        y += kKindRowH;
    }
    card(card_top, y);
    y += 16;

    // Exceptions
    card_top = y;
    header(y, text::kProgExceptionsTitle);
    l.items.push_back({Item::Line, {content.left, y, content.right, y}, L"", L"", 0});
    RECT help_rc = {content.left + 20, y + 8, content.right - 20, y + 30};
    l.items.push_back({Item::Text, help_rc, text::kProgExceptionsHelp, L"", 0});
    y += 34;
    if (n == 0) {
        l.items.push_back({Item::Empty, {content.left + 20, y + 4, content.right - 20, y + 36}, Utf8ToWide(st->programs_model.no_exceptions_text), L"", 0});
        y += 40;
    }
    for (size_t i = 0; i < n; ++i) {
        const RECT rc = {content.left, y, content.right, y + kRowH};
        l.items.push_back({Item::Label, rc, Utf8ToWide(st->programs_model.exceptions[i].exe), text::kProgExceptionRowHelp, 0});
        const int remove_l = content.right - 16 - 90;
        l.ctl[ExcIdx(i) + 1] = Centered(remove_l, remove_l + 90, y, kRowH, kCtlH);
        l.ctl[ExcIdx(i)] = Centered(remove_l - 8 - 130, remove_l - 8, y, kRowH, kCtlH);
        y += kRowH;
    }
    {
        const RECT rc = {content.left, y, content.right, y + kAddH};
        l.items.push_back({Item::Line, rc, L"", L"", 0});
        const int btn_l = content.right - 16 - 170;
        l.items.push_back({Item::Text, {content.left + 20, y + 10, content.right - 20, y + 30}, text::kProgAddExceptionField, L"", 0});
        l.ctl[ExcEditIdx(n) + 1] = {btn_l, y + 40, btn_l + 170, y + 40 + kCtlH};
        const RECT frame = {content.left + 20, y + 40, btn_l - 12, y + 40 + kCtlH};
        l.items.push_back({Item::EditFrame, frame, L"", L"", kProgExcEditId});
        l.ctl[ExcEditIdx(n)] = {frame.left + 8, frame.top + 8, frame.right - 8, frame.bottom - 8};
        y += kAddH;
    }
    card(card_top, y);
    y += 16;

    // Games
    card_top = y;
    header(y, text::kProgGamesTitle);
    help_rc = {content.left + 20, y + 8, content.right - 20, y + 30};
    l.items.push_back({Item::Text, help_rc, text::kProgGamesHelp, L"", 0});
    y += 34;
    if (m == 0) {
        l.items.push_back({Item::Empty, {content.left + 20, y + 4, content.right - 20, y + 36}, Utf8ToWide(st->programs_model.no_games_text), L"", 0});
        y += 40;
    }
    for (size_t j = 0; j < m; ++j) {
        const RECT rc = {content.left, y, content.right, y + kRowH};
        l.items.push_back({Item::Label, rc, Utf8ToWide(st->programs_model.games[j]), L"", 0});
        const int remove_l = content.right - 16 - 90;
        l.ctl[GameIdx(n, j)] = Centered(remove_l, remove_l + 90, y, kRowH, kCtlH);
        y += kRowH;
    }
    {
        const RECT rc = {content.left, y, content.right, y + kAddH};
        l.items.push_back({Item::Line, rc, L"", L"", 0});
        const int btn_l = content.right - 16 - 170;
        l.items.push_back({Item::Text, {content.left + 20, y + 10, content.right - 20, y + 30}, text::kProgAddGameField, L"", 0});
        l.ctl[GameEditIdx(n, m) + 1] = {btn_l, y + 40, btn_l + 170, y + 40 + kCtlH};
        const RECT frame = {content.left + 20, y + 40, btn_l - 12, y + 40 + kCtlH};
        l.items.push_back({Item::EditFrame, frame, L"", L"", kProgGameEditId});
        l.ctl[GameEditIdx(n, m)] = {frame.left + 8, frame.top + 8, frame.right - 8, frame.bottom - 8};
        y += kAddH;
    }
    card(card_top, y);
    y += 16;

    const int foot_h = TextHeight(hdc, st->hFontSmall, text::kProgFooter, width);
    l.items.push_back({Item::Text, {content.left, y, content.right, y + foot_h}, text::kProgFooter, L"", 0});
    y += foot_h + 16;
    l.total = y - top;
    return l;
}

HWND MakeEdit(HWND parent, int id, const std::wstring& text, HFONT font) {
    HWND h = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (h) {
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(h, EM_LIMITTEXT, 260, 0);
    }
    return h;
}

std::wstring EditWide(HWND h) {
    wchar_t buf[300] = {};
    if (h) GetWindowTextW(h, buf, 300);
    return buf;
}

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

int IdOf(HWND h) { return h ? GetDlgCtrlID(h) : 0; }

HWND FindById(const GuiWindowState* st, int id) {
    for (HWND h : st->programs_controls) {
        if (h && IdOf(h) == id) return h;
    }
    return nullptr;
}

void Build(HWND hwnd, GuiWindowState* st) {
    const size_t n = ExcShown(st);
    const size_t m = GamesShown(st);
    auto& c = st->programs_controls;
    for (int k = 0; k < kKinds; ++k) {
        for (int o = 0; o < kChoices; ++o) c.push_back(MakeOwnerButton(hwnd, kProgKindBaseId + k * kChoices + o, KindName(st, k, o)));
    }
    for (size_t i = 0; i < n; ++i) {
        c.push_back(MakeOwnerButton(hwnd, kProgExcBaseId + static_cast<int>(2 * i), ActionName(st, i)));
        c.push_back(MakeOwnerButton(hwnd, kProgExcBaseId + static_cast<int>(2 * i) + 1, RemoveName(st->programs_model.exceptions[i].exe)));
    }
    c.push_back(MakeEdit(hwnd, kProgExcEditId, L"", st->hFontRegular));
    c.push_back(MakeOwnerButton(hwnd, kProgExcAddId, text::kProgAddException));
    for (size_t j = 0; j < m; ++j) c.push_back(MakeOwnerButton(hwnd, kProgGameBaseId + static_cast<int>(j), RemoveName(st->programs_model.games[j])));
    c.push_back(MakeEdit(hwnd, kProgGameEditId, L"", st->hFontRegular));
    c.push_back(MakeOwnerButton(hwnd, kProgGameAddId, text::kProgAddGame));
    st->programs_exceptions = n;
    st->programs_games = m;
}

void SetNameIfChanged(HWND h, const std::wstring& want) {
    wchar_t cur[400] = {};
    GetWindowTextW(h, cur, 400);
    if (want != cur) SetWindowTextW(h, want.c_str());
}

} // namespace

void ProgramsViewDestroy(GuiWindowState* st) {
    for (HWND h : st->programs_controls) {
        if (h) DestroyWindow(h);
    }
    st->programs_controls.clear();
    st->programs_exceptions = 0;
    st->programs_games = 0;
}

void ProgramsViewScroll(GuiWindowState* st, int delta_px) {
    st->programs_scroll += delta_px;
    ProgramsViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void ProgramsViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->programs_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    HDC hdc = GetDC(st->hwnd);
    Layout l = BuildLayout(hdc, st, content);
    const int max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->programs_scroll > max_scroll || st->programs_scroll < 0) {
        st->programs_scroll = std::clamp(st->programs_scroll, 0, max_scroll);
        l = BuildLayout(hdc, st, content);
    }
    ReleaseDC(st->hwnd, hdc);
    for (size_t k = 0; k < st->programs_controls.size() && k < l.ctl.size(); ++k) PlaceClipped(st->programs_controls[k], l.ctl[k], content);
}

void ProgramsViewShow(GuiWindowState* st, bool show) {
    if (show) {
        ProgramsViewRelayout(st);
        return;
    }
    st->programs_focus_id = 0;
    for (HWND h : st->programs_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void ProgramsViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->callbacks.on_PollPrograms) st->programs_model = st->callbacks.on_PollPrograms();
    if (st->programs_model.categories.size() != kKinds) return; // no model yet
    const bool rebuild = st->programs_controls.empty() || st->programs_exceptions != ExcShown(st) || st->programs_games != GamesShown(st);
    if (rebuild) {
        // Keep what is being typed and where the keyboard was: a row added or removed rebuilds the controls.
        const std::wstring typed_exc = EditWide(FindById(st, kProgExcEditId));
        const std::wstring typed_game = EditWide(FindById(st, kProgGameEditId));
        const int had_focus = IdOf(GetFocus()) != 0 && FindById(st, IdOf(GetFocus())) ? IdOf(GetFocus()) : 0;
        const bool visible = !st->programs_controls.empty() && st->programs_controls[0] && IsWindowVisible(st->programs_controls[0]);
        ProgramsViewDestroy(st);
        Build(hwnd, st);
        SetWindowTextW(FindById(st, kProgExcEditId), typed_exc.c_str());
        SetWindowTextW(FindById(st, kProgGameEditId), typed_game.c_str());
        ProgramsViewRelayout(st);
        if (!visible) ProgramsViewShow(st, false);
        if (had_focus != 0 && visible) {
            HWND again = FindById(st, had_focus);
            if (!again) again = FindById(st, had_focus >= kProgGameBaseId ? kProgGameEditId : kProgExcEditId); // the row is gone: land on its list's field
            if (again) SetFocus(again);
        }
    } else {
        for (int c = 0; c < kKinds; ++c) {
            for (int o = 0; o < kChoices; ++o) SetNameIfChanged(st->programs_controls[KindIdx(c, o)], KindName(st, c, o));
        }
        const size_t n = ExcShown(st);
        for (size_t i = 0; i < n; ++i) {
            SetNameIfChanged(st->programs_controls[ExcIdx(i)], ActionName(st, i));
            SetNameIfChanged(st->programs_controls[ExcIdx(i) + 1], RemoveName(st->programs_model.exceptions[i].exe));
        }
        for (size_t j = 0; j < GamesShown(st); ++j) SetNameIfChanged(st->programs_controls[GameIdx(n, j)], RemoveName(st->programs_model.games[j]));
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ProgramsViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    if (st->programs_model.categories.size() != kKinds) return;
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const Layout l = BuildLayout(hdc, st, content);
    DrawTextAt(hdc, content.left, l.title_y, text::kProgTitle, CLR_TEXT_TITLE, st->hFontHeader);
    RECT sub = {content.left, l.title_y + 30, content.right, l.title_y + 30 + 200};
    DrawTextRect(hdc, sub, text::kProgSubtitle, CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    for (const Item& it : l.items) {
        if (it.kind == Item::Card) DrawRoundedCard(hdc, it.rc, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    }
    for (const Item& it : l.items) {
        switch (it.kind) {
            case Item::Header: {
                RECT r = {it.rc.left + 1, it.rc.top + 1, it.rc.right - 1, it.rc.bottom};
                HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
                FillRect(hdc, &r, br);
                DeleteObject(br);
                DrawTextAt(hdc, it.rc.left + 20, it.rc.top + 10, it.a, CLR_TEXT_TITLE, st->hFontBold);
                break;
            }
            case Item::Label: {
                HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
                HGDIOBJ old = SelectObject(hdc, pen);
                MoveToEx(hdc, it.rc.left + 1, it.rc.top, nullptr);
                LineTo(hdc, it.rc.right - 1, it.rc.top);
                SelectObject(hdc, old);
                DeleteObject(pen);
                const int right = it.rc.right - 16 - kChoices * kChoiceW - (kChoices - 1) * 6 - 16;
                RECT nm = {it.rc.left + 20, it.rc.top + 10, std::min<int>(right, it.rc.right - 270), it.rc.top + 30};
                DrawTextRect(hdc, nm, it.a, CLR_TEXT_TITLE, st->hFontBold, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                RECT h = {it.rc.left + 20, it.rc.top + 32, std::min<int>(right, it.rc.right - 270), it.rc.bottom - 4};
                if (!it.b.empty()) DrawTextRect(hdc, h, it.b, CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }
            case Item::Line: {
                HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
                HGDIOBJ old = SelectObject(hdc, pen);
                MoveToEx(hdc, it.rc.left + 1, it.rc.top, nullptr);
                LineTo(hdc, it.rc.right - 1, it.rc.top);
                SelectObject(hdc, old);
                DeleteObject(pen);
                break;
            }
            case Item::Text:
                DrawTextRect(hdc, it.rc, it.a, CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            case Item::Empty:
                DrawTextRect(hdc, it.rc, it.a, CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            case Item::EditFrame:
                DrawRoundedCard(hdc, it.rc, CLR_BTN_BG, CLR_CONTROL_BORDER, 6);
                if (st->programs_focus_id == it.id) DrawFocusRing(hdc, it.rc);
                break;
            default: break;
        }
    }
    RestoreDC(hdc, saved);
}

void ProgramsViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    const int id = static_cast<int>(d->CtlID);
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    SetBkMode(d->hDC, TRANSPARENT);
    if (id >= kProgKindBaseId && id < kProgKindBaseId + kKinds * kChoices) {
        const int c = (id - kProgKindBaseId) / kChoices;
        const int o = (id - kProgKindBaseId) % kChoices;
        const bool selected = st->programs_model.categories.size() == kKinds && ChoiceOf(st->programs_model.categories[static_cast<size_t>(c)].action) == o;
        if (selected) {
            DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, false);
            DrawRoundedCard(d->hDC, RECT{d->rcItem.left + 1, d->rcItem.top + 1, d->rcItem.right - 1, d->rcItem.bottom - 1}, kChoiceFill[o], kChoiceText[o], 6);
        } else {
            DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, pressed);
        }
        DrawTextRect(d->hDC, d->rcItem, kChoiceLabel[o], selected ? kChoiceText[o] : CLR_TEXT_BODY, selected ? st->hFontBold : st->hFontSmall,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, pressed);
        std::wstring label = text::kProgRemove;
        if (id == kProgExcAddId) label = text::kProgAddException;
        else if (id == kProgGameAddId) label = text::kProgAddGame;
        else if (id >= kProgExcBaseId && id < kProgExcEditId && (id - kProgExcBaseId) % 2 == 0) {
            const size_t i = static_cast<size_t>(id - kProgExcBaseId) / 2;
            if (i < st->programs_model.exceptions.size()) label = Utf8ToWide(session::ActionLabel(st->programs_model.exceptions[i].action));
        }
        DrawTextRect(d->hDC, d->rcItem, label, CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

namespace {

ProcessAction NextAction(ProcessAction a) {
    const int o = ChoiceOf(a);
    return kChoiceAction[(o < 0 ? 0 : o + 1) % kChoices];
}

void Say(GuiWindowState* st, const std::string& msg) { st->data.last_action_status = msg; }

// Add from a field: send the text, and clear the field only if the list grew (a refusal keeps it to be fixed).
void AddFrom(HWND hwnd, GuiWindowState* st, int edit_id, bool exception) {
    HWND edit = FindById(st, edit_id);
    const auto& cb = exception ? st->callbacks.on_AddException : st->callbacks.on_AddManualGame;
    if (!edit || !cb) return;
    const size_t before = exception ? st->programs_model.exceptions.size() : st->programs_model.games.size();
    if (before >= kProgMaxRows) { // more rows than the list can show would be saved but never seen
        Say(st, "Se muestran hasta 100 filas. Quita alguna antes de añadir otra.");
        return;
    }
    Say(st, cb(ToUtf8(EditWide(edit))));
    ProgramsViewRefresh(hwnd, st);
    const size_t after = exception ? st->programs_model.exceptions.size() : st->programs_model.games.size();
    if (after != before) SetWindowTextW(FindById(st, edit_id), L"");
}

} // namespace

bool ProgramsViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id == IDOK && notify_code == 0 && st->current_tab == 6) {
        const int focus_id = IdOf(GetFocus());
        if (FindById(st, focus_id) && focus_id == kProgExcEditId) AddFrom(hwnd, st, kProgExcEditId, true);
        else if (FindById(st, focus_id) && focus_id == kProgGameEditId) AddFrom(hwnd, st, kProgGameEditId, false);
        return true;
    }
    if (id < kProgKindBaseId || id > kProgGameAddId) return false;
    // Only our own controls, on our own tab, may act (a posted fake is not ours).
    if (st->current_tab != 6 || !from || GetDlgCtrlID(from) != id) return true;
    if (std::find(st->programs_controls.begin(), st->programs_controls.end(), from) == st->programs_controls.end()) return true;
    const bool is_edit = id == kProgExcEditId || id == kProgGameEditId;
    if (notify_code == BN_SETFOCUS || notify_code == EN_SETFOCUS) {
        if (is_edit) {
            st->programs_focus_id = id;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) ProgramsViewScroll(st, delta);
        return true;
    }
    if (notify_code == EN_KILLFOCUS) {
        if (st->programs_focus_id == id) st->programs_focus_id = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (is_edit || notify_code != BN_CLICKED) return true;

    const auto& cb = st->callbacks;
    if (id < kProgKindBaseId + kKinds * kChoices) {
        const int c = (id - kProgKindBaseId) / kChoices;
        const int o = (id - kProgKindBaseId) % kChoices;
        if (cb.on_SetCategoryAction && st->programs_model.categories.size() == kKinds) {
            Say(st, cb.on_SetCategoryAction(st->programs_model.categories[static_cast<size_t>(c)].id, static_cast<int>(kChoiceAction[o])));
            ProgramsViewRefresh(hwnd, st);
        }
    } else if (id >= kProgExcBaseId && id < kProgExcEditId) {
        const size_t i = static_cast<size_t>(id - kProgExcBaseId) / 2;
        if (i >= st->programs_model.exceptions.size()) return true;
        const std::string exe = st->programs_model.exceptions[i].exe;
        if ((id - kProgExcBaseId) % 2 == 0) {
            if (cb.on_SetExceptionAction) Say(st, cb.on_SetExceptionAction(exe, static_cast<int>(NextAction(st->programs_model.exceptions[i].action))));
        } else if (cb.on_RemoveException) {
            Say(st, cb.on_RemoveException(exe));
        }
        ProgramsViewRefresh(hwnd, st);
    } else if (id == kProgExcAddId) {
        AddFrom(hwnd, st, kProgExcEditId, true);
    } else if (id >= kProgGameBaseId && id < kProgGameEditId) {
        const size_t j = static_cast<size_t>(id - kProgGameBaseId);
        if (j < st->programs_model.games.size() && cb.on_RemoveManualGame) {
            Say(st, cb.on_RemoveManualGame(st->programs_model.games[j]));
            ProgramsViewRefresh(hwnd, st);
        }
    } else if (id == kProgGameAddId) {
        AddFrom(hwnd, st, kProgGameEditId, false);
    }
    return true;
}

} // namespace vegaflow::ui
