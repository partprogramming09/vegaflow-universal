#include "ui/home_view.hpp"

#include <algorithm>

#include "session/home_screen.hpp"
#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kHeadH = 38;
constexpr int kBtnH = 34;
constexpr int kPad = 20;
constexpr int kGap = 16;

session::HomeScreenModel Model(const GuiWindowState* st) {
    const auto& d = st->data;
    session::HomeInputs in;
    in.elevated = d.is_elevated;
    in.daemon_ws_kb = d.daemon_private_ws_kb;
    in.game_active = d.game_active;
    in.game_name = d.active_game_name;
    in.game_engine = d.active_game_engine;
    in.game_api = d.active_game_api;
    in.game_foreground = d.game_is_foreground;
    in.background_saving = d.ecoqos_processes_count;
    in.io_priority_high = d.io_priority_active;
    in.direct_flip = d.directflip_active;
    in.ram_total_mb = d.total_ram_mb;
    in.ram_available_mb = d.free_ram_mb + d.standby_ram_mb; // free plus standby: Windows hands standby back when asked
    in.guard_interventions = d.uma_guard_interventions;
    in.automatic = d.automatic;
    in.guard_active = d.uma_guard_enabled;
    in.purge_before_free_mb = d.last_purge_before_free_mb;
    in.purge_after_free_mb = d.last_purge_after_free_mb;
    return session::BuildHomeScreen(in, session::BuildEquipmentScreen(d.system_info, d.cpu_sets, d.game_cpu_sets, d.background_cpu_sets, d.restrict_game_cpu_sets),
                                    st->programs_model);
}

struct Layout {
    int title_y = 0;
    RECT game{}, equipment{}, memory{}, background{};
    RECT equipment_btn{}, purge_btn{}, programs_btn{};
    int total = 0; // unscrolled height
    int eq_row_h = 0;
};

// Wrapped text needs a DC to be measured; a window DC with the same fonts gives the heights that painting will use.
Layout BuildLayout(GuiWindowState* st, const RECT& content, const session::HomeScreenModel& m) {
    Layout l;
    const int width = content.right - content.left;
    const int inner = width - 2 * kPad;
    HDC dc = GetDC(st->hwnd);
    const int top = content.top - st->home_scroll;
    l.title_y = top;
    int y = top + 72;

    // Game card: header, name, sub line, facts or the waiting sentence
    int h = kHeadH + 12 + 26 + 8;
    if (m.game_active) h += 22 + static_cast<int>(m.game_facts.size()) * 22 + 6;
    else h += TextHeight(dc, st->hFontRegular, Utf8ToWide(m.waiting_text), inner) + 10;
    l.game = {content.left, y, content.right, y + h};
    y += h + kGap;

    // Equipment card: one line per fact, then the button
    l.eq_row_h = 28;
    h = kHeadH + 8 + static_cast<int>(m.equipment.size()) * l.eq_row_h + 10 + kBtnH + 14;
    l.equipment = {content.left, y, content.right, y + h};
    l.equipment_btn = {content.left + kPad, l.equipment.bottom - 14 - kBtnH, content.left + kPad + 220, l.equipment.bottom - 14};
    y += h + kGap;

    // Memory card: percent + bar, detail, guard, purge, button
    h = kHeadH + 14 + 40 + 10 + 22 + 22 + 22 + 10 + kBtnH + 14;
    l.memory = {content.left, y, content.right, y + h};
    l.purge_btn = {content.left + kPad, l.memory.bottom - 14 - kBtnH, content.left + kPad + 220, l.memory.bottom - 14};
    y += h + kGap;

    // Background card: count, note, button
    const int note_h = m.background_note.empty() ? 0 : TextHeight(dc, st->hFontSmall, Utf8ToWide(m.background_note), inner) + 8;
    h = kHeadH + 12 + 24 + note_h + 12 + kBtnH + 14;
    l.background = {content.left, y, content.right, y + h};
    l.programs_btn = {content.left + kPad, l.background.bottom - 14 - kBtnH, content.left + kPad + 220, l.background.bottom - 14};
    y += h + kGap;
    ReleaseDC(st->hwnd, dc);
    l.total = y - top;
    return l;
}

void Header(HDC hdc, GuiWindowState* st, const RECT& card, const wchar_t* title) {
    RECT r = {card.left + 1, card.top + 1, card.right - 1, card.top + kHeadH};
    HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
    FillRect(hdc, &r, br);
    DeleteObject(br);
    DrawTextAt(hdc, card.left + kPad, card.top + 10, title, CLR_TEXT_TITLE, st->hFontBold);
}

HWND Find(const GuiWindowState* st, int id) {
    for (HWND h : st->home_controls) {
        if (h && GetDlgCtrlID(h) == id) return h;
    }
    return nullptr;
}

const wchar_t* Label(int id) {
    switch (id) {
        case kHomeEquipmentId: return text::kHomeEquipmentButton;
        case kHomePurgeId: return text::kHomePurgeButton;
        default: return text::kHomeProgramsButton;
    }
}

} // namespace

void HomeViewDestroy(GuiWindowState* st) {
    for (HWND h : st->home_controls) {
        if (h) DestroyWindow(h);
    }
    st->home_controls.clear();
}

void HomeViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->home_controls.empty()) return;
    const RECT content = ScreenContentRect(st->hwnd);
    const auto m = Model(st);
    Layout l = BuildLayout(st, content, m);
    st->home_max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->home_scroll > st->home_max_scroll || st->home_scroll < 0) {
        st->home_scroll = std::clamp(st->home_scroll, 0, st->home_max_scroll);
        l = BuildLayout(st, content, m);
    }
    PlaceClipped(Find(st, kHomeEquipmentId), l.equipment_btn, content);
    PlaceClipped(Find(st, kHomePurgeId), l.purge_btn, content);
    PlaceClipped(Find(st, kHomeProgramsId), l.programs_btn, content);
}

void HomeViewScroll(GuiWindowState* st, int delta_px) {
    st->home_scroll += delta_px;
    HomeViewRelayout(st); // clamps against the real maximum
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void HomeViewShow(GuiWindowState* st, bool show) {
    if (show) {
        HomeViewRelayout(st);
        return;
    }
    for (HWND h : st->home_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void HomeViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->home_controls.empty()) {
        for (int id : {kHomeEquipmentId, kHomePurgeId, kHomeProgramsId}) st->home_controls.push_back(MakeOwnerButton(hwnd, id, Label(id)));
    }
    // The numbers change on every poll: only the paint differs, never the controls.
    if (st->current_tab == 0) HomeViewRelayout(st);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void HomeViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const auto m = Model(st);
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const Layout l = BuildLayout(st, content, m);
    const int inner = (content.right - content.left) - 2 * kPad;
    DrawTextAt(hdc, content.left, l.title_y, text::kHomeTitle, CLR_TEXT_TITLE, st->hFontHeader);
    DrawTextAt(hdc, content.left, l.title_y + 30, Utf8ToWide(m.permission_text), m.permission_ok ? CLR_ACCENT_GREEN : CLR_ACCENT_AMBER, st->hFontSmall);
    if (!m.memory_text.empty()) {
        const std::wstring mt = Utf8ToWide(m.memory_text);
        DrawTextAt(hdc, content.right - TextWidth(hdc, st->hFontSmall, mt), l.title_y + 30, mt, CLR_TEXT_MUTED, st->hFontSmall);
    }

    // Game
    DrawRoundedCard(hdc, l.game, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, l.game, Utf8ToWide(m.game_title).c_str());
    int y = l.game.top + kHeadH + 12;
    DrawTextRect(hdc, {l.game.left + kPad, y, l.game.right - kPad, y + 26}, Utf8ToWide(m.game_name), CLR_TEXT_TITLE, st->hFontBold, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    y += 34;
    if (m.game_active) {
        DrawTextRect(hdc, {l.game.left + kPad, y - 8, l.game.right - kPad, y + 14}, Utf8ToWide(m.game_sub), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        y += 22;
        for (const auto& f : m.game_facts) {
            DrawTextRect(hdc, {l.game.left + kPad, y, l.game.right - kPad, y + 20}, Utf8ToWide(f), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
            y += 22;
        }
    } else {
        DrawTextRect(hdc, {l.game.left + kPad, y - 8, l.game.right - kPad, l.game.bottom - 6}, Utf8ToWide(m.waiting_text), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }

    // Equipment
    DrawRoundedCard(hdc, l.equipment, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, l.equipment, text::kHomeEquipmentTitle);
    y = l.equipment.top + kHeadH + 8;
    for (const auto& row : m.equipment) {
        DrawTextAt(hdc, l.equipment.left + kPad, y + 4, Utf8ToWide(row.first), CLR_TEXT_MUTED, st->hFontSmall);
        DrawTextRect(hdc, {l.equipment.left + kPad + 170, y + 2, l.equipment.right - kPad, y + l.eq_row_h}, Utf8ToWide(row.second), CLR_TEXT_TITLE, st->hFontRegular,
                     DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        y += l.eq_row_h;
    }

    // Shared graphics memory
    DrawRoundedCard(hdc, l.memory, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, l.memory, Utf8ToWide(m.memory_title).c_str());
    y = l.memory.top + kHeadH + 14;
    const std::wstring pct = m.memory_percent >= 0 ? std::to_wstring(m.memory_percent) + L" % en uso" : L"Sin dato";
    DrawTextAt(hdc, l.memory.left + kPad, y, pct, CLR_TEXT_TITLE, st->hFontHeader);
    if (m.memory_percent >= 0) { // the bar says the same as the number, never alone
        const RECT bar = {l.memory.left + kPad + 220, y + 8, l.memory.right - kPad, y + 24};
        HBRUSH track = CreateSolidBrush(RGB(42, 47, 58));
        FillRect(hdc, &bar, track);
        DeleteObject(track);
        RECT fill = bar;
        fill.right = bar.left + (bar.right - bar.left) * std::min(m.memory_percent, 100) / 100;
        HBRUSH fb = CreateSolidBrush(m.memory_percent >= 85 ? RGB(245, 158, 11) : RGB(59, 130, 246));
        FillRect(hdc, &fill, fb);
        DeleteObject(fb);
    }
    y += 50;
    for (const std::string* s : {&m.memory_detail, &m.guard_text, &m.purge_text}) {
        DrawTextRect(hdc, {l.memory.left + kPad, y, l.memory.right - kPad, y + 20}, Utf8ToWide(*s), CLR_TEXT_BODY, st->hFontRegular, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
        y += 22;
    }

    // Background programs
    DrawRoundedCard(hdc, l.background, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, l.background, text::kHomeBackgroundTitle);
    y = l.background.top + kHeadH + 12;
    DrawTextAt(hdc, l.background.left + kPad, y, Utf8ToWide(m.background_text), CLR_TEXT_TITLE, st->hFontBold);
    y += 24;
    if (!m.background_note.empty()) {
        DrawTextRect(hdc, {l.background.left + kPad, y, l.background.left + kPad + inner, l.programs_btn.top - 8}, Utf8ToWide(m.background_note), CLR_TEXT_MUTED, st->hFontSmall,
                     DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
    RestoreDC(hdc, saved);
}

void HomeViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, (d->itemState & ODS_SELECTED) != 0);
    SetBkMode(d->hDC, TRANSPARENT);
    DrawTextRect(d->hDC, d->rcItem, Label(static_cast<int>(d->CtlID)), CLR_TEXT_TITLE, st->hFontBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

bool HomeViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from) {
    if (id < kHomeEquipmentId || id > kHomeProgramsId) return false;
    // Only our own controls, on our own tab, may act (a posted fake has no sender, a foreign window, or an id that is not the sender's).
    if (st->current_tab != 0 || !from || GetDlgCtrlID(from) != id || std::find(st->home_controls.begin(), st->home_controls.end(), from) == st->home_controls.end()) return true;
    if (notify_code == BN_SETFOCUS) {
        const int delta = ScrollDeltaToReveal(hwnd, from);
        if (delta != 0) HomeViewScroll(st, delta);
        return true;
    }
    if (notify_code != BN_CLICKED) return true;
    if (id == kHomeEquipmentId) SelectTab(hwnd, st, 7);
    else if (id == kHomeProgramsId) SelectTab(hwnd, st, 6);
    else if (st->callbacks.on_PurgeNow) st->data.last_action_status = st->callbacks.on_PurgeNow();
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

} // namespace vegaflow::ui
