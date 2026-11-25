#include "ui/sidebar_view.hpp"

#include <algorithm>

#include "session/sidebar_nav.hpp"
#include "session/window_fit.hpp"
#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kTabTop = 78;
constexpr int kTabStep = 44;
constexpr int kTabH = 38;
constexpr int kTabMinStep = 30;
// Room the block under the tabs needs (divider, badge, tray button, two text lines), as painted in gui_window.cpp.
constexpr int kBottomBlock = 145 + 8;

int EntryOfId(int id) { return id >= ID_TAB_0 && id < ID_TAB_0 + session::kSidebarEntries ? id - ID_TAB_0 : -1; }

int EntryOfWindow(const GuiWindowState* st, HWND h) {
    for (int i = 0; i < session::kSidebarEntries && static_cast<size_t>(i) < st->sidebar_controls.size(); ++i) {
        if (st->sidebar_controls[static_cast<size_t>(i)] == h) return i;
    }
    return -1;
}

} // namespace

bool SidebarViewOwnsId(int id) { return EntryOfId(id) >= 0 || id == ID_ACT_MINIMIZE_TRAY; }

void SidebarViewCreate(HWND hwnd, GuiWindowState* st) {
    if (!st->sidebar_controls.empty()) return;
    st->hwnd = hwnd;
    for (int i = 0; i < session::kSidebarEntries; ++i) st->sidebar_controls.push_back(MakeOwnerButton(hwnd, ID_TAB_0 + i, text::kSidebarTabs[i]));
    st->sidebar_controls.push_back(MakeOwnerButton(hwnd, ID_ACT_MINIMIZE_TRAY, text::kSidebarTray));
    SidebarViewRelayout(st);
}

void SidebarViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->sidebar_controls.size() != static_cast<size_t>(session::kSidebarEntries) + 1) return;
    RECT c{};
    GetClientRect(st->hwnd, &c);
    const int bottom = c.bottom - STATUSBAR_HEIGHT;
    // In a short window (175 % scale on 1080p) the tabs get closer instead of running into the block under them.
    const int step = session::SidebarTabStep(bottom - kBottomBlock - kTabTop, session::kSidebarEntries, kTabStep, kTabMinStep);
    const int height = std::min(kTabH, step - (kTabStep - kTabH));
    for (int i = 0; i < session::kSidebarEntries; ++i) {
        const int y = kTabTop + i * step;
        SetWindowPos(st->sidebar_controls[static_cast<size_t>(i)], nullptr, 12, y, SIDEBAR_WIDTH - 24, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    SetWindowPos(st->sidebar_controls.back(), nullptr, 14, bottom - 102, SIDEBAR_WIDTH - 28, 26, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void SidebarViewSync(GuiWindowState* st) {
    for (HWND h : st->sidebar_controls) {
        if (h) InvalidateRect(h, nullptr, FALSE);
    }
}

void SidebarViewDestroy(GuiWindowState* st) {
    for (HWND h : st->sidebar_controls) {
        if (h) DestroyWindow(h);
    }
    st->sidebar_controls.clear();
}

void SidebarViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    const int id = static_cast<int>(d->CtlID);
    const bool pressed = (d->itemState & ODS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(CLR_BG_SIDEBAR);
    FillRect(d->hDC, &d->rcItem, bg);
    DeleteObject(bg);
    SetBkMode(d->hDC, TRANSPARENT);
    if (id == ID_ACT_MINIMIZE_TRAY) {
        DrawRoundedCard(d->hDC, d->rcItem, pressed ? CLR_BTN_HOVER : CLR_BTN_BG, CLR_CONTROL_BORDER, 6);
        DrawTextRect(d->hDC, d->rcItem, text::kSidebarTray, CLR_TEXT_TITLE, st->hFontSmall, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        const int entry = EntryOfId(id);
        const bool active = entry == session::SidebarEntryOfTab(st->current_tab);
        DrawRoundedCard(d->hDC, d->rcItem, active ? CLR_TAB_ACTIVE : (pressed ? CLR_TAB_HOVER : CLR_BG_SIDEBAR), active ? CLR_TAB_INDICATOR : CLR_BG_SIDEBAR, 6);
        if (active) {
            const RECT ind = {d->rcItem.left + 3, d->rcItem.top + 6, d->rcItem.left + 7, d->rcItem.bottom - 6};
            HBRUSH hb = CreateSolidBrush(CLR_TAB_INDICATOR);
            FillRect(d->hDC, &ind, hb);
            DeleteObject(hb);
        }
        const RECT tr = {d->rcItem.left + 16, d->rcItem.top, d->rcItem.right - 8, d->rcItem.bottom};
        DrawTextRect(d->hDC, tr, entry >= 0 ? text::kSidebarTabs[entry] : L"", active ? CLR_TEXT_TITLE : CLR_TEXT_BODY, active ? st->hFontBold : st->hFontRegular,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
}

bool SidebarViewCommand(GuiWindowState* st, int id, int notify_code, HWND from, int* act_id) {
    if (!SidebarViewOwnsId(id)) return false;
    if (act_id) *act_id = 0;
    // Only our own controls may act: a posted fake has no sender, a foreign window, or an id that is not the sender's.
    if (!from || GetDlgCtrlID(from) != id || std::find(st->sidebar_controls.begin(), st->sidebar_controls.end(), from) == st->sidebar_controls.end()) return true;
    if (notify_code == BN_CLICKED && act_id) *act_id = id;
    return true;
}

bool SidebarViewKey(GuiWindowState* st, HWND focus, WPARAM key) {
    // The "Minimizar a Bandeja" button is the last control and not a tab entry: Enter must press it too.
    if (key == VK_RETURN && !st->sidebar_controls.empty() && focus == st->sidebar_controls.back()) {
        SendMessageW(focus, BM_CLICK, 0, 0);
        return true;
    }
    const int entry = EntryOfWindow(st, focus);
    if (entry < 0) return false;
    if (key == VK_RETURN) { // IsDialogMessage would answer Enter with IDOK, not with the focused button
        SendMessageW(focus, BM_CLICK, 0, 0);
        return true;
    }
    session::SidebarKey k;
    if (key == VK_UP) k = session::SidebarKey::Up;
    else if (key == VK_DOWN) k = session::SidebarKey::Down;
    else if (key == VK_HOME) k = session::SidebarKey::Home;
    else if (key == VK_END) k = session::SidebarKey::End;
    else return false;
    SetFocus(st->sidebar_controls[static_cast<size_t>(session::MoveSidebarFocus(entry, k))]);
    return true;
}

} // namespace vegaflow::ui
