#include "ui/settings_view.hpp"

#include <algorithm>

#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

enum class Kind { Title, Intro, Recommended, GpuCard, Banner, FamCard, FamHeader, Row, Footer };

struct Item {
    Kind kind = Kind::Title;
    RECT rc{};
    int fam = -1;
    int row = -1;   // index inside the family
    int flat = -1;  // index across all families (control ids)
    int tip_h = 0;  // height of the open (i) panel, 0 when closed
};

constexpr int kRowH = 68;
constexpr int kCtl = 36; // control height: well above the 24 px minimum

RECT ContentRect(HWND hwnd) {
    RECT c{};
    GetClientRect(hwnd, &c);
    return {SIDEBAR_WIDTH + 16, 16, c.right - 16, c.bottom - STATUSBAR_HEIGHT - 16};
}

int TextHeight(HDC hdc, HFONT font, const std::wstring& text, int width) {
    HGDIOBJ old = SelectObject(hdc, font);
    RECT r = {0, 0, std::max(width, 40), 0};
    DrawTextW(hdc, text.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    return r.bottom - r.top;
}

int TextWidth(HDC hdc, HFONT font, const std::wstring& text) {
    HGDIOBJ old = SelectObject(hdc, font);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

const wchar_t* GpuKind(const DeviceFacts& d) {
    switch (d.connection) {
    case GpuConnection::Integrated: return text::kGpuIntegrated;
    case GpuConnection::Discrete: return text::kGpuDiscrete;
    case GpuConnection::External: return text::kGpuExternal;
    case GpuConnection::Unknown: break;
    }
    return text::kGpuUnknown;
}

std::vector<const SettingRow*> FlatRows(const GuiWindowState* st) {
    std::vector<const SettingRow*> rows;
    for (const auto& f : st->settings_model.families) {
        for (const auto& r : f.rows) rows.push_back(&r);
    }
    return rows;
}

// Lays out the whole screen in scrolled coordinates. `total` receives the unscrolled height.
std::vector<Item> BuildLayout(HDC hdc, const GuiWindowState* st, const RECT& content, int* total) {
    std::vector<Item> items;
    const int x0 = content.left;
    const int x1 = content.right;
    const int w = x1 - x0;
    const int top = content.top - st->settings_scroll;
    int y = top;
    auto add = [&](Kind k, int h) -> Item& {
        Item it;
        it.kind = k;
        it.rc = {x0, y, x1, y + h};
        items.push_back(it);
        y += h;
        return items.back();
    };
    add(Kind::Title, 36);
    add(Kind::Intro, TextHeight(hdc, st->hFontRegular, text::kSettingsIntro, w) + 10);
    add(Kind::Recommended, TextHeight(hdc, st->hFontRegular, text::kSettingsRecommended, w) + 14);
    add(Kind::GpuCard, 76);
    y += 12;
    if (st->settings_show_experimental) {
        const int h = TextHeight(hdc, st->hFontRegular, text::kExperimentalBody, w - 32) + 46;
        add(Kind::Banner, h);
        y += 12;
    }
    int flat = 0;
    for (size_t f = 0; f < st->settings_model.families.size(); ++f) {
        const auto& fam = st->settings_model.families[f];
        const size_t card_index = items.size();
        add(Kind::FamCard, 0);
        Item& header = add(Kind::FamHeader, 38);
        header.fam = static_cast<int>(f);
        for (size_t r = 0; r < fam.rows.size(); ++r) {
            int tip_h = 0;
            if (st->settings_open_tip == fam.rows[r].spec->id) {
                tip_h = TextHeight(hdc, st->hFontRegular, Utf8ToWide(fam.rows[r].tip), w - 72) + 62;
            }
            Item& row = add(Kind::Row, kRowH + tip_h);
            row.fam = static_cast<int>(f);
            row.row = static_cast<int>(r);
            row.flat = flat++;
            row.tip_h = tip_h;
        }
        items[card_index].rc = {x0, items[card_index + 1].rc.top, x1, y};
        items[card_index].fam = static_cast<int>(f);
        y += 16;
    }
    add(Kind::Footer, 30);
    if (total) *total = y - top;
    return items;
}

struct RowControls {
    RECT toggle{};
    RECT tip{};
};

RowControls ControlsOf(const Item& row) {
    const int right = row.rc.right - 16;
    RowControls c;
    c.toggle = {right - 132, row.rc.top + 16, right, row.rc.top + 16 + kCtl};
    c.tip = {c.toggle.left - 8 - kCtl, row.rc.top + 16, c.toggle.left - 8, row.rc.top + 16 + kCtl};
    return c;
}

RECT ExperimentalSwitch(const Item& gpu_card) {
    return {gpu_card.rc.right - 16 - 270, gpu_card.rc.top + 20, gpu_card.rc.right - 16, gpu_card.rc.top + 20 + kCtl};
}

HWND MakeButton(HWND parent, int id, const std::wstring& name) {
    return CreateWindowExW(0, L"BUTTON", name.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_NOTIFY, 0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

std::wstring ExperimentalName(bool on) {
    return std::wstring(text::kShowExperimental) + (on ? L": activado" : L": desactivado");
}

std::wstring ToggleName(const SettingRow& r) {
    return Utf8ToWide(r.spec->name) + (r.on ? L": activado" : L": de fábrica");
}

void DestroyControls(GuiWindowState* st) {
    for (HWND h : st->settings_controls) {
        if (h) DestroyWindow(h);
    }
    st->settings_controls.clear();
}

void PlaceClipped(HWND h, const RECT& r, const RECT& content) {
    if (!h) return;
    SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    // Clip to the part inside the content area so a scrolled control never paints over the sidebar or the status bar,
    // yet stays focusable (a hidden control would be skipped by Tab).
    RECT vis{};
    if (!IntersectRect(&vis, &r, &content)) vis = {0, 0, 0, 0};
    else OffsetRect(&vis, -r.left, -r.top);
    SetWindowRgn(h, CreateRectRgn(vis.left, vis.top, vis.right, vis.bottom), TRUE);
}

void DrawChip(HDC hdc, int x, int y, const std::wstring& label, COLORREF fg, COLORREF bg, COLORREF border, HFONT font, int* width) {
    const int w = TextWidth(hdc, font, label) + 20;
    RECT rc = {x, y, x + w, y + 20};
    DrawBadge(hdc, rc, label, fg, bg, border, font);
    *width = w;
}

} // namespace

void SettingsViewScroll(GuiWindowState* st, int delta_px) {
    st->settings_scroll += delta_px;
    SettingsViewRelayout(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void SettingsViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->settings_controls.empty()) return;
    const RECT content = ContentRect(st->hwnd);
    HDC hdc = GetDC(st->hwnd);
    int total = 0;
    auto items = BuildLayout(hdc, st, content, &total);
    const int max_scroll = std::max(0, total - static_cast<int>(content.bottom - content.top));
    if (st->settings_scroll > max_scroll || st->settings_scroll < 0) {
        st->settings_scroll = std::clamp(st->settings_scroll, 0, max_scroll);
        items = BuildLayout(hdc, st, content, &total);
    }
    ReleaseDC(st->hwnd, hdc);
    for (const Item& it : items) {
        if (it.kind == Kind::GpuCard) {
            PlaceClipped(st->settings_controls[0], ExperimentalSwitch(it), content);
        } else if (it.kind == Kind::Row) {
            const RowControls c = ControlsOf(it);
            const size_t k = static_cast<size_t>(it.flat);
            if (1 + 2 * k + 1 < st->settings_controls.size()) {
                PlaceClipped(st->settings_controls[1 + 2 * k], c.toggle, content);
                PlaceClipped(st->settings_controls[2 + 2 * k], c.tip, content);
            }
        }
    }
}

void SettingsViewShow(GuiWindowState* st, bool show) {
    if (show) {
        SettingsViewRelayout(st);
    } else {
        for (HWND h : st->settings_controls) {
            if (h) ShowWindow(h, SW_HIDE);
        }
    }
}

void SettingsViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->callbacks.on_PollSettings) st->settings_inputs = st->callbacks.on_PollSettings();
    st->settings_inputs.show_experimental = st->settings_show_experimental;
    st->settings_model = BuildSettingsScreen(st->settings_inputs);
    const auto rows = FlatRows(st);

    std::vector<std::string> signature = {st->settings_show_experimental ? "exp" : "std"};
    for (const SettingRow* r : rows) signature.push_back(r->spec->id);
    if (signature != st->settings_signature || st->settings_controls.empty()) {
        DestroyControls(st);
        st->settings_controls.push_back(MakeButton(hwnd, kSettingsExperimentalId, ExperimentalName(st->settings_show_experimental)));
        for (size_t k = 0; k < rows.size(); ++k) {
            const int base = kSettingsRowBaseId + 2 * static_cast<int>(k);
            st->settings_controls.push_back(rows[k]->spec->user_switch ? MakeButton(hwnd, base, ToggleName(*rows[k])) : nullptr);
            st->settings_controls.push_back(MakeButton(hwnd, base + 1, L"Qué cambia: " + Utf8ToWide(rows[k]->spec->name)));
        }
        st->settings_signature = signature;
    } else {
        // Same rows: touch a control only when its words changed, so a 1.5 s poll causes no repaint or flicker.
        auto set_text = [&](HWND h, const std::wstring& name) {
            wchar_t cur[256] = {};
            GetWindowTextW(h, cur, 256);
            if (name == cur) return false;
            SetWindowTextW(h, name.c_str());
            return true;
        };
        bool changed = set_text(st->settings_controls[0], ExperimentalName(st->settings_show_experimental));
        for (size_t k = 0; k < rows.size(); ++k) {
            if (HWND h = st->settings_controls[1 + 2 * k]) changed = set_text(h, ToggleName(*rows[k])) || changed;
        }
        if (!changed) return;
    }
    SettingsViewRelayout(st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void SettingsViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    int total = 0;
    const auto items = BuildLayout(hdc, st, content, &total);
    const auto& d = st->settings_inputs.device;

    for (const Item& it : items) {
        const RECT& rc = it.rc;
        switch (it.kind) {
        case Kind::Title:
            DrawTextAt(hdc, rc.left, rc.top, text::kSettingsTitle, CLR_TEXT_TITLE, st->hFontHeader);
            break;
        case Kind::Intro: {
            RECT r = {rc.left, rc.top, rc.right, rc.bottom};
            DrawTextRect(hdc, r, text::kSettingsIntro, CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            break;
        }
        case Kind::Recommended: {
            RECT r = {rc.left, rc.top, rc.right, rc.bottom};
            DrawTextRect(hdc, r, text::kSettingsRecommended, CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            break;
        }
        case Kind::GpuCard: {
            DrawRoundedCard(hdc, rc, RGB(26, 34, 51), CLR_ACCENT_CYAN, 8);
            DrawTextAt(hdc, rc.left + 20, rc.top + 12, text::kYourGpu, CLR_TEXT_BODY, st->hFontSmall);
            const std::wstring gpu = Utf8ToWide(st->data.gpu_summary);
            DrawTextAt(hdc, rc.left + 20, rc.top + 28, gpu.empty() ? L"No se pudo leer" : gpu, CLR_TEXT_TITLE, st->hFontHeader);
            std::wstring kind = GpuKind(d);
            if (d.vendor == GpuVendor::AMD && !d.driver_tweaks_allowed) kind += L". " + std::wstring(text::kDriverBlocked);
            RECT r = {rc.left + 20, rc.top + 50, rc.right - 300, rc.bottom - 4};
            DrawTextRect(hdc, r, kind, CLR_TEXT_BODY, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
            break;
        }
        case Kind::Banner: {
            DrawRoundedCard(hdc, rc, RGB(51, 40, 15), CLR_ACCENT_AMBER, 8);
            DrawTextAt(hdc, rc.left + 16, rc.top + 10, text::kExperimentalTitle, RGB(253, 231, 184), st->hFontBold);
            RECT r = {rc.left + 16, rc.top + 32, rc.right - 16, rc.bottom - 6};
            DrawTextRect(hdc, r, text::kExperimentalBody, RGB(253, 231, 184), st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            break;
        }
        case Kind::FamCard:
            DrawRoundedCard(hdc, rc, CLR_CARD_BG, CLR_CARD_BORDER, 8);
            break;
        case Kind::FamHeader: {
            const auto& fam = st->settings_model.families[static_cast<size_t>(it.fam)];
            RECT r = {rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom};
            HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
            FillRect(hdc, &r, br);
            DeleteObject(br);
            DrawTextAt(hdc, rc.left + 20, rc.top + 10, Utf8ToWide(fam.title), CLR_TEXT_TITLE, st->hFontBold);
            const std::wstring scope = Utf8ToWide(fam.scope);
            const int sw = TextWidth(hdc, st->hFontSmall, scope);
            DrawTextAt(hdc, rc.right - 20 - sw, rc.top + 12, scope, CLR_TEXT_MUTED, st->hFontSmall);
            break;
        }
        case Kind::Row: {
            const SettingRow& row = st->settings_model.families[static_cast<size_t>(it.fam)].rows[static_cast<size_t>(it.row)];
            HPEN pen = CreatePen(PS_SOLID, 1, CLR_SIDEBAR_BORDER);
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, rc.left + 1, rc.top, nullptr);
            LineTo(hdc, rc.right - 1, rc.top);
            SelectObject(hdc, old);
            DeleteObject(pen);

            const RowControls c = ControlsOf(it);
            const std::wstring name = Utf8ToWide(row.spec->name);
            DrawTextAt(hdc, rc.left + 20, rc.top + 12, name, CLR_TEXT_TITLE, st->hFontBold);
            int chip_x = rc.left + 20 + TextWidth(hdc, st->hFontBold, name) + 12;
            int cw = 0;
            if (row.spec->experimental) {
                DrawChip(hdc, chip_x, rc.top + 12, text::kExperimentalChip, CLR_ACCENT_AMBER, RGB(51, 40, 15), CLR_ACCENT_AMBER, st->hFontSmall, &cw);
                chip_x += cw + 8;
            }
            const std::wstring label = Utf8ToWide(row.evidence_label);
            const bool good = row.spec->evidence == Evidence::Gain;
            DrawChip(hdc, chip_x, rc.top + 12, label, good ? CLR_ACCENT_GREEN : CLR_TEXT_MUTED, good ? RGB(15, 46, 36) : RGB(42, 47, 58),
                     good ? CLR_ACCENT_GREEN : CLR_CARD_BORDER, st->hFontSmall, &cw);
            RECT why = {rc.left + 20, rc.top + 38, c.tip.left - 12, rc.top + 58};
            DrawTextRect(hdc, why, Utf8ToWide(row.spec->why), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
            if (!row.spec->user_switch) {
                RECT auto_r = {c.toggle.left, c.toggle.top, c.toggle.right, c.toggle.bottom};
                DrawTextRect(hdc, auto_r, text::kAutomatic, CLR_TEXT_MUTED, st->hFontRegular, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            if (it.tip_h > 0) {
                RECT panel = {rc.left + 20, rc.top + kRowH - 4, rc.right - 20, rc.bottom - 10};
                DrawRoundedCard(hdc, panel, CLR_BG_SIDEBAR, CLR_CONTROL_BORDER, 6);
                DrawTextAt(hdc, panel.left + 14, panel.top + 10, text::kWhatChanges, CLR_TEXT_TITLE, st->hFontSmallBold);
                RECT r = {panel.left + 14, panel.top + 32, panel.right - 14, panel.bottom - 6};
                DrawTextRect(hdc, r, Utf8ToWide(row.tip), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
            break;
        }
        case Kind::Footer:
            DrawTextAt(hdc, rc.left, rc.top + 4, st->settings_model.families.empty() ? text::kNothingToShow : text::kOnlyApplicable,
                       CLR_TEXT_MUTED, st->hFontSmall);
            break;
        }
    }
    RestoreDC(hdc, saved);
}

void SettingsViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    HDC hdc = d->hDC;
    const RECT rc = d->rcItem;
    const int id = static_cast<int>(d->CtlID);
    const bool focus = (d->itemState & ODS_FOCUS) != 0;
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    const bool is_exp = (id == kSettingsExperimentalId);
    const bool is_tip = !is_exp && ((id - kSettingsRowBaseId) % 2 == 1);

    const COLORREF surface = is_exp ? RGB(26, 34, 51) : CLR_CARD_BG;
    HBRUSH bg = CreateSolidBrush(surface);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT body = {rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1};
    DrawRoundedCard(hdc, body, pressed ? CLR_BTN_HOVER : CLR_BTN_BG, CLR_CONTROL_BORDER, 6);

    bool on = false;
    std::wstring label;
    if (is_exp) {
        on = st->settings_show_experimental;
        label = text::kShowExperimental;
    } else {
        const auto rows = FlatRows(st);
        const size_t k = static_cast<size_t>((id - kSettingsRowBaseId) / 2);
        if (k < rows.size()) {
            on = rows[k]->on;
            label = on ? text::kOn : text::kFactory;
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    if (is_tip) {
        const int cx = (rc.left + rc.right) / 2;
        const int cy = (rc.top + rc.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, 2, CLR_TEXT_TITLE);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Ellipse(hdc, cx - 9, cy - 9, cx + 9, cy + 9);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
        RECT glyph = {cx - 9, cy - 9, cx + 9, cy + 9};
        DrawTextRect(hdc, glyph, L"i", CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        // Switch: track and knob, then the state in words (colour is never the only cue).
        const int tx = rc.left + 10;
        const int ty = (rc.top + rc.bottom) / 2 - 10;
        RECT track = {tx, ty, tx + 34, ty + 20};
        DrawRoundedCard(hdc, track, on ? CLR_ACCENT_CYAN : CLR_CONTROL_BORDER, on ? CLR_ACCENT_CYAN : CLR_CONTROL_BORDER, 10);
        const int kx = on ? tx + 34 - 18 : tx + 2;
        HBRUSH knob = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ old_brush = SelectObject(hdc, knob);
        HGDIOBJ old_pen = SelectObject(hdc, GetStockObject(NULL_PEN));
        Ellipse(hdc, kx, ty + 2, kx + 16, ty + 18);
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(knob);
        RECT tr = {tx + 34 + 8, rc.top, rc.right - 6, rc.bottom};
        DrawTextRect(hdc, tr, label, CLR_TEXT_TITLE, st->hFontSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    if (focus) {
        // Keyboard focus ring: 2 px, 8:1 or better on every surface (Test 62).
        HPEN ring = CreatePen(PS_SOLID, 2, CLR_FOCUS_RING);
        HGDIOBJ old_pen = SelectObject(hdc, ring);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, 8, 8);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(ring);
    }
}

// Brings a control that got keyboard focus into the visible area, so the focus ring is never drawn off screen.
void ScrollIntoView(HWND hwnd, GuiWindowState* st, HWND control) {
    RECT r{};
    GetWindowRect(control, &r);
    MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&r), 2);
    const RECT content = ContentRect(hwnd);
    int delta = 0;
    if (r.top < content.top) delta = static_cast<int>(r.top - content.top) - 8;
    else if (r.bottom > content.bottom) delta = static_cast<int>(r.bottom - content.bottom) + 8;
    if (delta != 0) SettingsViewScroll(st, delta);
}

bool SettingsViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id != kSettingsExperimentalId && id < kSettingsRowBaseId) return false;
    // Only our own controls may act: WM_COMMAND is allowed through the UIPI filter for the tray, so a posted fake
    // (lParam 0 or a foreign window) must not reach HKLM.
    if (std::find(st->settings_controls.begin(), st->settings_controls.end(), from) == st->settings_controls.end()) return true;
    if (notify_code == BN_SETFOCUS) {
        ScrollIntoView(hwnd, st, from);
        return true;
    }
    if (notify_code != BN_CLICKED) return true; // ours, but only a click acts
    const auto rows = FlatRows(st);
    if (id == kSettingsExperimentalId) {
        st->settings_show_experimental = !st->settings_show_experimental;
        st->settings_scroll = 0;
        SettingsViewRefresh(hwnd, st);
        if (!st->settings_controls.empty()) SetFocus(st->settings_controls[0]); // the old button was destroyed with the rebuild
        return true;
    }
    const size_t k = static_cast<size_t>((id - kSettingsRowBaseId) / 2);
    if (k >= rows.size()) return true;
    const SettingRow& row = *rows[k];
    if ((id - kSettingsRowBaseId) % 2 == 1) {
        st->settings_open_tip = (st->settings_open_tip == row.spec->id) ? std::string() : std::string(row.spec->id);
        SettingsViewRelayout(st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    if (st->callbacks.on_SetSetting) {
        st->data.last_action_status = st->callbacks.on_SetSetting(row.spec->id, !row.on);
    }
    SettingsViewRefresh(hwnd, st);
    return true;
}

} // namespace vegaflow::ui
