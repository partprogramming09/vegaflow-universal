// Unit tests: WCAG contrast of the GUI palette (phase G). Computes the ratios from the real constants in
// ui/gui_components.hpp, so a palette change that breaks readability fails here instead of being eyeballed.

#include "ui/gui_components.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vftest {

namespace {

double Channel(int v) {
    const double c = v / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double Luminance(COLORREF c) {
    return 0.2126 * Channel(GetRValue(c)) + 0.7152 * Channel(GetGValue(c)) + 0.0722 * Channel(GetBValue(c));
}

double Ratio(COLORREF a, COLORREF b) {
    const double la = Luminance(a);
    const double lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// Returns the names of the pairs below `minimum`.
std::string Below(double minimum, const std::vector<std::pair<std::string, double>>& ratios) {
    std::string bad;
    for (const auto& [name, r] : ratios) {
        if (r < minimum) bad += name + "=" + std::to_string(r).substr(0, 4) + " ";
    }
    return bad;
}

} // namespace

void Test62() {
    using namespace vegaflow;

    // 1. Sanity of the formula: black on white is 21:1 (WCAG), a colour on itself is 1:1.
    RecordResult(std::fabs(Ratio(RGB(0, 0, 0), RGB(255, 255, 255)) - 21.0) < 0.01 && std::fabs(Ratio(RGB(9, 9, 9), RGB(9, 9, 9)) - 1.0) < 1e-9,
                 "Test 62.1: the contrast formula gives 21:1 for black on white and 1:1 for equal colours");

    // 2. Text colours reach 4.5:1 on every surface text is drawn on (WCAG 1.4.3 AA).
    const std::vector<std::pair<std::string, COLORREF>> surfaces = {
        {"main", CLR_BG_MAIN}, {"sidebar", CLR_BG_SIDEBAR}, {"card", CLR_CARD_BG}, {"card-header", CLR_CARD_HEADER}, {"button", CLR_BTN_BG}};
    const std::vector<std::pair<std::string, COLORREF>> texts = {
        {"title", CLR_TEXT_TITLE}, {"body", CLR_TEXT_BODY}, {"muted", CLR_TEXT_MUTED}};
    std::vector<std::pair<std::string, double>> text_ratios;
    for (const auto& [tn, tc] : texts) {
        for (const auto& [sn, sc] : surfaces) text_ratios.push_back({tn + " on " + sn, Ratio(tc, sc)});
    }
    const std::string text_bad = Below(4.5, text_ratios);
    RecordResult(text_bad.empty(), "Test 62.2: title, body and muted text reach 4.5:1 on every surface", text_bad);

    // 3. Accent colours used as text reach 4.5:1 on the surfaces they are drawn on.
    const std::vector<std::pair<std::string, COLORREF>> accents = {
        {"cyan", CLR_ACCENT_CYAN}, {"green", CLR_ACCENT_GREEN}, {"amber", CLR_ACCENT_AMBER}, {"purple-text", CLR_ACCENT_PURPLE_TEXT}};
    std::vector<std::pair<std::string, double>> accent_ratios;
    for (const auto& [an, ac] : accents) {
        for (const auto& [sn, sc] : {surfaces[0], surfaces[1], surfaces[2]}) accent_ratios.push_back({an + " on " + sn, Ratio(ac, sc)});
    }
    accent_ratios.push_back({"red on main", Ratio(CLR_ACCENT_RED, CLR_BG_MAIN)});
    accent_ratios.push_back({"red on log", Ratio(CLR_ACCENT_RED, CLR_BAR_BG)});
    const std::string accent_bad = Below(4.5, accent_ratios);
    RecordResult(accent_bad.empty(), "Test 62.3: accent text colours reach 4.5:1 on main, sidebar and card", accent_bad);

    // 4. Borders of controls (buttons, fields, switches) reach 3:1 on main, card and card header (WCAG 1.4.11).
    std::vector<std::pair<std::string, double>> border_ratios;
    for (const auto& [sn, sc] : {surfaces[0], surfaces[2], surfaces[3], surfaces[4]}) {
        border_ratios.push_back({"control border on " + sn, Ratio(CLR_CONTROL_BORDER, sc)});
    }
    const std::string border_bad = Below(3.0, border_ratios);
    RecordResult(border_bad.empty(), "Test 62.4: control borders reach 3:1 on the surfaces controls sit on", border_bad);

    // 5. The keyboard focus ring is visible (3:1 or more) on every surface (WCAG 2.4.7 and 1.4.11).
    std::vector<std::pair<std::string, double>> focus_ratios;
    for (const auto& [sn, sc] : surfaces) focus_ratios.push_back({"focus ring on " + sn, Ratio(CLR_FOCUS_RING, sc)});
    const std::string focus_bad = Below(3.0, focus_ratios);
    RecordResult(focus_bad.empty(), "Test 62.5: the focus ring reaches 3:1 on every surface", focus_bad);
}

} // namespace vftest
