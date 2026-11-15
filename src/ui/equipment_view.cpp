#include "ui/equipment_view.hpp"

#include <algorithm>

#include "session/equipment_screen.hpp"
#include "ui/native_controls.hpp"
#include "ui/texts.hpp"

namespace vegaflow::ui {

namespace {

constexpr int kPerRow = 6;
constexpr int kCellH = 52;
constexpr int kGap = 8;
constexpr int kHeadH = 38;

struct RoleLook {
    COLORREF fill, border, text;
};
const RoleLook kRole[] = {
    {RGB(30, 42, 68), RGB(59, 130, 246), RGB(219, 230, 253)}, // game
    {RGB(58, 46, 18), RGB(245, 158, 11), RGB(253, 231, 184)}, // background
    {RGB(35, 38, 45), RGB(75, 82, 96), RGB(196, 201, 210)},   // left to Windows
};

void Header(HDC hdc, GuiWindowState* st, const RECT& card, const std::wstring& title, const std::wstring& right) {
    RECT r = {card.left + 1, card.top + 1, card.right - 1, card.top + kHeadH};
    HBRUSH br = CreateSolidBrush(CLR_CARD_HEADER);
    FillRect(hdc, &r, br);
    DeleteObject(br);
    DrawTextAt(hdc, card.left + 20, card.top + 10, title, CLR_TEXT_TITLE, st->hFontBold);
    if (!right.empty()) DrawTextAt(hdc, card.right - 20 - TextWidth(hdc, st->hFontSmall, right), card.top + 12, right, CLR_TEXT_MUTED, st->hFontSmall);
}

} // namespace

void EquipmentViewScroll(GuiWindowState* st, int delta_px) {
    st->equipment_scroll = std::clamp(st->equipment_scroll + delta_px, 0, st->equipment_max_scroll);
    InvalidateRect(st->hwnd, nullptr, FALSE);
}

void EquipmentViewPaint(HDC hdc, GuiWindowState* st, const RECT& content) {
    using session::ThreadRole;
    const auto m = session::BuildEquipmentScreen(st->data.system_info, st->data.cpu_sets, st->data.game_cpu_sets, st->data.background_cpu_sets, st->data.restrict_game_cpu_sets);
    const int saved = SaveDC(hdc);
    IntersectClipRect(hdc, content.left, content.top, content.right, content.bottom);
    const int width = content.right - content.left;
    int y = content.top - st->equipment_scroll;
    const int top = y;

    DrawTextAt(hdc, content.left, y, text::kEquipTitle, CLR_TEXT_TITLE, st->hFontHeader);
    y += 32;
    DrawTextAt(hdc, content.left, y, Utf8ToWide(m.cpu_line), CLR_TEXT_BODY, st->hFontRegular);
    y += 34;

    // Threads
    const int rows = static_cast<int>((m.threads.size() + kPerRow - 1) / kPerRow);
    const int note_h = TextHeight(hdc, st->hFontSmall, Utf8ToWide(m.cpu_note), width - 40);
    const int grid_h = rows * (kCellH + kGap);
    RECT card = {content.left, y, content.right, y + kHeadH + 12 + grid_h + 34 + note_h + 16};
    DrawRoundedCard(hdc, card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, card, text::kEquipThreadsTitle, Utf8ToWide(m.threads_text));
    const int cell_w = (width - 40 - (kPerRow - 1) * kGap) / kPerRow;
    for (size_t i = 0; i < m.threads.size(); ++i) {
        const int cx = content.left + 20 + static_cast<int>(i % kPerRow) * (cell_w + kGap);
        const int cy = y + kHeadH + 12 + static_cast<int>(i / kPerRow) * (kCellH + kGap);
        const RoleLook& look = kRole[static_cast<int>(m.threads[i].role)];
        const RECT cell = {cx, cy, cx + cell_w, cy + kCellH};
        DrawRoundedCard(hdc, cell, look.fill, look.border, 6);
        DrawTextAt(hdc, cx + 10, cy + 8, Utf8ToWide(m.threads[i].label), look.text, st->hFontBold);
        DrawTextAt(hdc, cx + 10, cy + 28, Utf8ToWide(m.threads[i].kind), look.text, st->hFontSmall);
    }
    int ly = y + kHeadH + 12 + grid_h + 4;
    const wchar_t* legend[] = {text::kEquipLegendGame, text::kEquipLegendBackground, text::kEquipLegendSystem};
    int lx = content.left + 20;
    for (int r = 0; r < 3 && m.has_data; ++r) {
        const RECT sw = {lx, ly + 3, lx + 14, ly + 17};
        DrawRoundedCard(hdc, sw, kRole[r].fill, kRole[r].border, 3);
        DrawTextAt(hdc, lx + 20, ly, legend[r], CLR_TEXT_BODY, st->hFontSmall);
        lx += 20 + TextWidth(hdc, st->hFontSmall, legend[r]) + 24;
    }
    RECT note = {content.left + 20, ly + 28, content.right - 20, ly + 28 + note_h};
    DrawTextRect(hdc, note, Utf8ToWide(m.cpu_note), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    y = card.bottom + 16;

    // Graphics
    int gh = kHeadH;
    std::vector<int> note_hs;
    for (const auto& g : m.gpus) {
        note_hs.push_back(TextHeight(hdc, st->hFontSmall, Utf8ToWide(g.note), width - 40));
        gh += 40 + note_hs.back() + 12;
    }
    card = {content.left, y, content.right, y + gh};
    DrawRoundedCard(hdc, card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, card, text::kEquipGraphicsTitle, L"");
    int gy = y + kHeadH + 10;
    for (size_t i = 0; i < m.gpus.size(); ++i) {
        DrawTextAt(hdc, content.left + 20, gy, Utf8ToWide(m.gpus[i].name), CLR_TEXT_TITLE, st->hFontBold);
        const std::wstring link = Utf8ToWide(m.gpus[i].link);
        DrawTextAt(hdc, content.right - 20 - TextWidth(hdc, st->hFontSmall, link), gy + 2, link, CLR_TEXT_MUTED, st->hFontSmall);
        RECT n = {content.left + 20, gy + 26, content.right - 20, gy + 26 + note_hs[i]};
        DrawTextRect(hdc, n, Utf8ToWide(m.gpus[i].note), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        gy += 40 + note_hs[i] + 12;
    }
    y = card.bottom + 16;

    // Memory protection level
    const int tier_note_h = TextHeight(hdc, st->hFontSmall, Utf8ToWide(m.tier_note), width - 40);
    card = {content.left, y, content.right, y + kHeadH + 12 + 56 + 12 + tier_note_h + 16};
    DrawRoundedCard(hdc, card, CLR_CARD_BG, CLR_CARD_BORDER, 8);
    Header(hdc, st, card, text::kEquipTierTitle, L"");
    const int tier_w = (width - 40 - 3 * kGap) / 4;
    for (int i = 0; i < 4; ++i) {
        const int tx = content.left + 20 + i * (tier_w + kGap);
        const RECT box = {tx, y + kHeadH + 12, tx + tier_w, y + kHeadH + 12 + 56};
        const bool current = m.tier == i + 1;
        DrawRoundedCard(hdc, box, current ? RGB(30, 42, 68) : RGB(22, 25, 30), current ? RGB(59, 130, 246) : RGB(38, 41, 48), 6);
        const std::wstring head = L"Nivel " + std::to_wstring(i + 1) + (current ? text::kEquipYours : L"");
        DrawTextAt(hdc, tx + 10, box.top + 8, head, current ? CLR_TEXT_TITLE : CLR_TEXT_MUTED, st->hFontBold);
        DrawTextAt(hdc, tx + 10, box.top + 30, Utf8ToWide(m.tier_labels[static_cast<size_t>(i)]), current ? CLR_TEXT_BODY : CLR_TEXT_MUTED, st->hFontSmall);
    }
    RECT tn = {content.left + 20, y + kHeadH + 12 + 56 + 12, content.right - 20, y + kHeadH + 12 + 56 + 12 + tier_note_h};
    DrawTextRect(hdc, tn, Utf8ToWide(m.tier_note), CLR_TEXT_MUTED, st->hFontSmall, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    y = card.bottom + 16;

    st->equipment_max_scroll = std::max(0, (y - top) - static_cast<int>(content.bottom - content.top));
    const int clamped = std::clamp(st->equipment_scroll, 0, st->equipment_max_scroll);
    if (clamped != st->equipment_scroll) { // the window grew while scrolled: draw again without the gap
        st->equipment_scroll = clamped;
        InvalidateRect(st->hwnd, nullptr, FALSE);
    }
    RestoreDC(hdc, saved);
}

} // namespace vegaflow::ui
