#include "ui/memory_view.hpp"

#include <algorithm>
#include <cstdio>

#include "session/memory_screen.hpp"
#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kCellW = 86;
constexpr int kCellH = 46;
constexpr int kGapX = 10;
constexpr int kGapY = 8;
constexpr int kTopCardH = 138;
constexpr int kBtnH = 38;
constexpr int kPad = 16;

session::MemoryScreenModel Model(const GuiWindowState* st) {
    session::MemoryScreenInputs in;
    in.cpu_sets = st->data.cpu_sets;
    in.game_cpu_sets = st->data.game_cpu_sets;
    in.background_cpu_sets = st->data.background_cpu_sets;
    in.total_ram_mb = st->data.total_ram_mb;
    in.free_ram_mb = st->data.free_ram_mb;
    in.standby_ram_mb = st->data.standby_ram_mb;
    in.modified_ram_mb = st->data.modified_ram_mb;
    in.dxgi_pressure_ratio = st->data.dxgi_pressure_ratio;
    in.dxgi_pressure_threshold = st->data.dxgi_pressure_threshold;
    in.uma_guard_triggered_live = st->data.uma_guard_triggered_live;
    in.noisy_bg_processes_count = st->data.noisy_bg_processes_count;
    in.isolated_bg_processes_count = st->data.isolated_bg_processes_count;
    return session::BuildMemoryScreen(in);
}

struct Layout {
    int title_y = 0;
    RECT top_card{}, purge_btn{}, cpu_card{}, grid{}, isolate_btn{}, restore_btn{};
    int columns = 1;
    int summary_y = 0, selected_y = 0, stats_y = 0;
    int total = 0; // unscrolled height of everything
};

Layout BuildLayout(const GuiWindowState* st, const RECT& content, const session::MemoryScreenModel& m) {
    Layout l;
    const int top = content.top - st->memory_scroll;
    l.title_y = top;
    l.top_card = {content.left, top + 46, content.right, top + 46 + kTopCardH};
    const int bot_row = l.top_card.top + 88;
    l.purge_btn = {l.top_card.right - 260, bot_row, l.top_card.right - kPad, bot_row + 36};
    l.cpu_card = {content.left, l.top_card.bottom + 12, content.right, 0};
    const int grid_w = std::max(40, static_cast<int>(content.right - content.left) - 2 * kPad);
    const session::GridGeometry g = session::ComputeGrid(static_cast<int>(m.cells.size()), grid_w, kCellW, kGapX, kCellH, kGapY);
    l.columns = g.columns;
    l.grid = {content.left + kPad, l.cpu_card.top + 56, content.left + kPad + grid_w, l.cpu_card.top + 56 + std::max(g.height, 24)};
    l.summary_y = l.grid.bottom + 10;
    l.selected_y = l.summary_y + 20;
    l.stats_y = l.selected_y + 28;
    const int btn_y = l.stats_y + 26;
    l.isolate_btn = {content.left + kPad, btn_y, content.left + kPad + 314, btn_y + kBtnH};
    l.restore_btn = {l.isolate_btn.right + 12, btn_y, l.isolate_btn.right + 12 + 298, btn_y + kBtnH};
    l.cpu_card.bottom = btn_y + kBtnH + kPad;
    l.total = l.cpu_card.bottom - top;
    return l;
}

HWND Grid(const GuiWindowState* st) { return st->memory_controls.size() == 4 ? st->memory_controls[3] : nullptr; }

COLORREF RoleColor(session::CoreRole r) {
    switch (r) {
        case session::CoreRole::Game: return CLR_ACCENT_CYAN;
        case session::CoreRole::Background: return CLR_ACCENT_PURPLE;
        case session::CoreRole::Windows: break;
    }
    return CLR_CONTROL_BORDER;
}

COLORREF RoleTextColor(session::CoreRole r) {
    switch (r) {
        case session::CoreRole::Game: return RGB(147, 180, 248);
        case session::CoreRole::Background: return CLR_ACCENT_PURPLE_TEXT;
        case session::CoreRole::Windows: break;
    }
    return CLR_TEXT_BODY;
}

std::string Signature(const GuiWindowState* st, const session::MemoryScreenModel& m) {
    std::string s = std::to_string(st->memory_selected) + ":";
    for (const auto& c : m.cells) s += static_cast<char>('0' + static_cast<int>(c.role));
    return s;
}

// Screen position of the selected cell, so a keyboard move can scroll it into view (the map is one control, maybe taller than the window).
void RevealSelected(GuiWindowState* st) {
    const RECT content = ScreenContentRect(st->hwnd);
    const auto m = Model(st);
    if (m.cells.empty()) return;
    const Layout l = BuildLayout(st, content, m);
    const int row = st->memory_selected / std::max(l.columns, 1);
    const int cell_top = l.grid.top + row * (kCellH + kGapY);
    int delta = 0;
    if (cell_top < content.top) delta = cell_top - content.top - 8;
    else if (cell_top + kCellH > content.bottom) delta = cell_top + kCellH - content.bottom + 8;
    if (delta != 0) MemoryViewScroll(st, delta);
}

void UpdateGridName(GuiWindowState* st, const session::MemoryScreenModel& m) {
    if (HWND g = Grid(st)) {
        const std::string d = session::DescribeCell(m, st->memory_selected);
        SetWindowTextW(g, (std::wstring(text::kMemGridName) + (d.empty() ? Utf8ToWide(m.cores_summary) : Utf8ToWide(d))).c_str());
    }
}

void Select(GuiWindowState* st, int index) {
    const auto m = Model(st);
    const int clamped = m.cells.empty() ? 0 : std::clamp(index, 0, static_cast<int>(m.cells.size()) - 1);
    if (clamped == st->memory_selected) return;
    st->memory_selected = clamped;
    UpdateGridName(st, m);
    if (HWND g = Grid(st)) InvalidateRect(g, nullptr, FALSE);
    RevealSelected(st);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

WNDPROC g_grid_proc = nullptr; // the BUTTON class procedure the map control is subclassed from (one map per process)

LRESULT CALLBACK GridProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* st = reinterpret_cast<GuiWindowState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_GETDLGCODE) return CallWindowProcW(g_grid_proc, h, msg, w, l) | DLGC_WANTARROWS;
    if (st && msg == WM_KEYDOWN) {
        const auto m = Model(st);
        session::CoreKey key;
        bool known = true;
        switch (w) {
            case VK_LEFT: key = session::CoreKey::Left; break;
            case VK_RIGHT: key = session::CoreKey::Right; break;
            case VK_UP: key = session::CoreKey::Up; break;
            case VK_DOWN: key = session::CoreKey::Down; break;
            case VK_HOME: key = session::CoreKey::Home; break;
            case VK_END: key = session::CoreKey::End; break;
            default: key = session::CoreKey::Left; known = false; break;
        }
        if (known) {
            Select(st, session::MoveCoreSelection(st->memory_selected, key, static_cast<int>(m.cells.size()), st->memory_columns));
            return 0;
        }
    }
    if (st && msg == WM_LBUTTONDOWN) {
        SetFocus(h);
        const int col = GET_X_LPARAM(l) / (kCellW + kGapX);
        const int row = GET_Y_LPARAM(l) / (kCellH + kGapY);
        const int x_in = GET_X_LPARAM(l) % (kCellW + kGapX);
        const int y_in = GET_Y_LPARAM(l) % (kCellH + kGapY);
        if (col < st->memory_columns && x_in < kCellW && y_in < kCellH) Select(st, row * st->memory_columns + col);
        return 0;
    }
    return CallWindowProcW(g_grid_proc, h, msg, w, l);
}

} // namespace

bool MemoryViewOwnsId(int id) { return id == ID_ACT_PURGE_RAM || id == ID_ACT_ISOLATE_BG || id == ID_ACT_RESTORE_CPU || id == kMemGridId; }

void MemoryViewDestroy(GuiWindowState* st) {
    for (HWND h : st->memory_controls) {
        if (h) DestroyWindow(h);
    }
    st->memory_controls.clear();
}

void MemoryViewRelayout(GuiWindowState* st) {
    if (!st->hwnd || st->memory_controls.size() != 4) return;
    const RECT content = ScreenContentRect(st->hwnd);
    const auto m = Model(st);
    Layout l = BuildLayout(st, content, m);
    st->memory_columns = l.columns;
    st->memory_max_scroll = std::max(0, l.total - static_cast<int>(content.bottom - content.top));
    if (st->memory_scroll > st->memory_max_scroll || st->memory_scroll < 0) {
        st->memory_scroll = std::clamp(st->memory_scroll, 0, st->memory_max_scroll);
        l = BuildLayout(st, content, m);
    }
    PlaceClipped(st->memory_controls[0], l.purge_btn, content);
    PlaceClipped(st->memory_controls[1], l.isolate_btn, content);
    PlaceClipped(st->memory_controls[2], l.restore_btn, content);
    if (m.cells.empty()) ShowWindow(st->memory_controls[3], SW_HIDE); // nothing to select: Tab skips it
    else PlaceClipped(st->memory_controls[3], l.grid, content);
}

void MemoryViewScroll(GuiWindowState* st, int delta_px) {
    st->memory_scroll += delta_px;
    MemoryViewRelayout(st); // clamps against the real maximum
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void MemoryViewShow(GuiWindowState* st, bool show) {
    if (show) {
        MemoryViewRelayout(st);
        return;
    }
    for (HWND h : st->memory_controls) {
        if (h) ShowWindow(h, SW_HIDE);
    }
}

void MemoryViewRefresh(HWND hwnd, GuiWindowState* st) {
    st->hwnd = hwnd;
    if (st->memory_controls.empty()) {
        st->memory_controls.push_back(MakeOwnerButton(hwnd, ID_ACT_PURGE_RAM, text::kMemPurge));
        st->memory_controls.push_back(MakeOwnerButton(hwnd, ID_ACT_ISOLATE_BG, text::kMemIsolate));
        st->memory_controls.push_back(MakeOwnerButton(hwnd, ID_ACT_RESTORE_CPU, text::kMemRestore));
        HWND grid = MakeOwnerButton(hwnd, kMemGridId, text::kMemGridName);
        if (grid) {
            SetWindowLongPtrW(grid, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            const auto prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(grid, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GridProc)));
            if (!g_grid_proc) g_grid_proc = prev;
        }
        st->memory_controls.push_back(grid);
    }
    const auto m = Model(st);
    if (!m.cells.empty() && st->memory_selected >= static_cast<int>(m.cells.size())) st->memory_selected = static_cast<int>(m.cells.size()) - 1; // fewer threads than before
    if (m.cells.empty()) st->memory_selected = 0;
    UpdateGridName(st, m);
    if (st->current_tab == 2) MemoryViewRelayout(st);
    const std::string sig = Signature(st, m);
    if (sig != st->memory_signature) { // children are not repainted by the parent's invalidation
        st->memory_signature = sig;
        if (HWND g = Grid(st)) InvalidateRect(g, nullptr, FALSE);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void MemoryViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    const auto m = Model(st);
    const Layout l = BuildLayout(st, content, m);
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);

    DrawTextAt(hdc, content.left, l.title_y, text::kMemTitle, CLR_ACCENT_CYAN, st->hFontHeader);
    DrawTextAt(hdc, content.left, l.title_y + 20, text::kMemSubtitle, CLR_TEXT_MUTED, st->hFontSmall);

    // Top card: unified memory
    const RECT& tc = l.top_card;
    DrawRoundedCard(hdc, tc, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    DrawTextAt(hdc, tc.left + kPad, tc.top + 12, text::kMemManagerTitle, CLR_ACCENT_CYAN, st->hFontBold);
    const RECT dxgi = {tc.right - 195, tc.top + 10, tc.right - kPad, tc.top + 32};
    if (m.pressure_alert) DrawBadge(hdc, dxgi, Utf8ToWide(m.pressure_badge), CLR_ACCENT_AMBER, RGB(36, 26, 20), CLR_ACCENT_AMBER, st->hFontSmallBold);
    else DrawBadge(hdc, dxgi, Utf8ToWide(m.pressure_badge), CLR_ACCENT_CYAN, RGB(20, 36, 44), CLR_ACCENT_CYAN, st->hFontSmallBold);

    std::wstring line = Utf8ToWide(m.ram_line);
    wchar_t buf[160];
    if (m.has_memory_data) {
        swprintf_s(buf, L"   |   VRAM Local: %llu / %llu MB", static_cast<unsigned long long>(st->data.vram_local_used_mb), static_cast<unsigned long long>(st->data.vram_local_budget_mb));
        line += buf;
    }
    if (st->data.has_amd_driver) {
        const bool pool_active = st->data.amd_vram_override_active || st->data.amd_vram_reported_mb >= 2048;
        const uint32_t pool_mb = st->data.amd_vram_reported_mb > 0 ? st->data.amd_vram_reported_mb : (pool_active ? 4096 : 512);
        swprintf_s(buf, L"POOL TEXTURAS: %u MB", pool_mb);
        const RECT pool = {tc.right - 390, tc.top + 10, tc.right - 203, tc.top + 32};
        if (pool_active) DrawBadge(hdc, pool, buf, CLR_ACCENT_GREEN, RGB(20, 36, 28), CLR_ACCENT_GREEN, st->hFontSmallBold);
        else DrawBadge(hdc, pool, buf, CLR_TEXT_MUTED, RGB(33, 38, 48), CLR_CARD_BORDER, st->hFontSmallBold);
    }
    DrawTextRect(hdc, {tc.left + kPad, tc.top + 38, tc.right - kPad, tc.top + 58}, line, CLR_TEXT_TITLE, st->hFontRegular, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);

    if (!m.bar.empty()) {
        std::vector<std::pair<double, COLORREF>> segments;
        for (const auto& s : m.bar) {
            const COLORREF c = s.kind == session::BarPart::Used ? CLR_ACCENT_CYAN : s.kind == session::BarPart::Standby ? CLR_ACCENT_PURPLE : s.kind == session::BarPart::Modified ? CLR_ACCENT_AMBER : CLR_ACCENT_GREEN;
            segments.push_back({s.fraction, c});
        }
        DrawMultiSegmentBar(hdc, {tc.left + kPad, tc.top + 62, tc.right - kPad, tc.top + 76}, segments, CLR_BAR_BG, CLR_CARD_BORDER);
    }

    const int thresh = static_cast<int>(st->data.dxgi_pressure_threshold * 100.0 + 0.5);
    const int row_y = tc.top + 88;
    const RECT note = {tc.left + kPad, row_y + 8, l.purge_btn.left - 12, row_y + 30};
    if (st->data.last_purge_after_free_mb > 0) {
        swprintf_s(buf, L"Última purga: %llu MB libres  |  Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral %d%%)", static_cast<unsigned long long>(st->data.last_purge_after_free_mb),
                   st->data.uma_guard_interventions, st->data.dxgi_pressure_ratio * 100.0, thresh);
        DrawTextRect(hdc, note, buf, CLR_ACCENT_GREEN, st->hFontSmallBold, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        swprintf_s(buf, L"Mitigaciones UMA Guard: %u  |  Presión DXGI: %.1f%% (Umbral %d%%)", st->data.uma_guard_interventions, st->data.dxgi_pressure_ratio * 100.0, thresh);
        DrawTextRect(hdc, note, buf, m.pressure_alert ? CLR_ACCENT_AMBER : CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    }

    // CPU card
    DrawRoundedCard(hdc, l.cpu_card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    DrawTextAt(hdc, l.cpu_card.left + kPad, l.cpu_card.top + 12, text::kMemCpuTitle, CLR_ACCENT_CYAN, st->hFontBold);
    DrawTextRect(hdc, {l.cpu_card.left + kPad, l.cpu_card.top + 34, l.cpu_card.right - kPad, l.cpu_card.top + 54}, text::kMemLegend, CLR_TEXT_MUTED, st->hFontSmall,
                 DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    DrawTextRect(hdc, {l.cpu_card.left + kPad, l.summary_y, l.cpu_card.right - kPad, l.summary_y + 18}, Utf8ToWide(m.cores_summary), CLR_TEXT_BODY, st->hFontRegular,
                 DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    if (!m.cells.empty()) {
        const std::wstring sel = Utf8ToWide(session::DescribeCell(m, st->memory_selected)) + L"   ·   " + text::kMemGridHint;
        DrawTextRect(hdc, {l.cpu_card.left + kPad, l.selected_y, l.cpu_card.right - kPad, l.selected_y + 18}, sel, CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    }
    DrawTextRect(hdc, {l.cpu_card.left + kPad, l.stats_y, l.cpu_card.right - kPad, l.stats_y + 20}, Utf8ToWide(m.background_line), CLR_TEXT_BODY, st->hFontRegular,
                 DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(hdc, saved);
}

void MemoryViewDrawItem(const DRAWITEMSTRUCT* d, GuiWindowState* st) {
    const int id = static_cast<int>(d->CtlID);
    if (id != kMemGridId) {
        DrawButtonFrame(d->hDC, d->rcItem, CLR_CARD_BG, (d->itemState & ODS_SELECTED) != 0);
        SetBkMode(d->hDC, TRANSPARENT);
        const wchar_t* label = id == ID_ACT_PURGE_RAM ? text::kMemPurge : id == ID_ACT_ISOLATE_BG ? text::kMemIsolate : text::kMemRestore;
        DrawTextRect(d->hDC, d->rcItem, label, CLR_TEXT_TITLE, st->hFontSmallBold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (d->itemState & ODS_FOCUS) DrawFocusRing(d->hDC, d->rcItem);
        return;
    }
    // The thread map: every cell is drawn here, relative to the control; only the selected one gets the focus ring.
    HBRUSH bg = CreateSolidBrush(CLR_CARD_BG);
    FillRect(d->hDC, &d->rcItem, bg);
    DeleteObject(bg);
    SetBkMode(d->hDC, TRANSPARENT);
    const auto m = Model(st);
    const int cols = std::max(st->memory_columns, 1);
    for (size_t i = 0; i < m.cells.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const int x = d->rcItem.left + col * (kCellW + kGapX);
        const int y = d->rcItem.top + row * (kCellH + kGapY);
        const RECT cell = {x, y, x + kCellW, y + kCellH};
        const auto& c = m.cells[i];
        DrawRoundedCard(d->hDC, cell, CLR_BAR_BG, RoleColor(c.role), 6);
        DrawTextAt(d->hDC, x + 8, y + 6, Utf8ToWide(c.label), CLR_TEXT_TITLE, st->hFontSmallBold);
        DrawTextAt(d->hDC, x + 8, y + 24, Utf8ToWide(c.role_text), RoleTextColor(c.role), st->hFontSmallBold);
        if ((d->itemState & ODS_FOCUS) && static_cast<int>(i) == st->memory_selected) DrawFocusRing(d->hDC, cell);
    }
}

bool MemoryViewCommand(HWND hwnd, GuiWindowState* st, int id, int notify_code, HWND from, int* act_id) {
    if (!MemoryViewOwnsId(id)) return false;
    if (act_id) *act_id = 0;
    // Only our own controls, on our own tab, may act.
    if (st->current_tab != 2 || !from || GetDlgCtrlID(from) != id || std::find(st->memory_controls.begin(), st->memory_controls.end(), from) == st->memory_controls.end()) return true;
    if (notify_code == BN_SETFOCUS) {
        if (id == kMemGridId) RevealSelected(st); // the map may be taller than the window: show the selected thread, not the whole control
        else if (const int delta = ScrollDeltaToReveal(hwnd, from)) MemoryViewScroll(st, delta);
        return true;
    }
    if (notify_code == BN_CLICKED && id != kMemGridId && act_id) *act_id = id;
    return true;
}

} // namespace vegaflow::ui
