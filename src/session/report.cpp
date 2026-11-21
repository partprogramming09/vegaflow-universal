#include "session/report.hpp"

#include <algorithm>
#include <cctype>

namespace vegaflow::session {

namespace {

constexpr size_t kMaxField = 200;
constexpr size_t kMaxRows = 30;

// "C:\Users\Ana\x" -> "C:\Users\<usuario>\x": the report is pasted in public places, so the Windows user name stays home.
std::string HideUserName(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        const bool sep_before = i > 0 && (s[i - 1] == '\\' || s[i - 1] == '/');
        const bool at_users = sep_before && i + 6 <= s.size() && (s[i + 5] == '\\' || s[i + 5] == '/') &&
                              std::equal(s.begin() + static_cast<long>(i), s.begin() + static_cast<long>(i) + 5, "users",
                                         [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
        if (!at_users) {
            out += s[i++];
            continue;
        }
        out += s.substr(i, 6); // "Users" and its separator
        i += 6;
        while (i < s.size() && s[i] != '\\' && s[i] != '/' && s[i] != '"' && s[i] != '\r' && s[i] != '\n') ++i; // the name (may hold spaces)
        out += "<usuario>";
    }
    return out;
}

std::string Clean(std::string s) {
    s = HideUserName(s);
    for (char& c : s) {
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    }
    if (s.size() > kMaxField) {
        size_t cut = kMaxField;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut; // not in the middle of a UTF-8 character
        s = s.substr(0, cut) + "...";
    }
    return s;
}

void Line(std::string& out, const std::string& text) { out += Clean(text) + "\r\n"; }

} // namespace

std::string BuildReport(const ReportInputs& in) {
    std::string out;
    Line(out, "VegaFlow Universal " + in.version + " - informe");
    Line(out, in.home.permission_text);
    Line(out, in.home.game_title + (in.home.game_name.empty() ? "" : ": " + in.home.game_name));
    out += "\r\nEquipo\r\n";
    for (const auto& row : in.home.equipment) Line(out, "  " + row.first + ": " + row.second);
    out += "\r\nMemoria\r\n";
    for (const std::string* s : {&in.home.memory_detail, &in.home.guard_text, &in.home.purge_text, &in.home.background_text}) {
        if (!s->empty()) Line(out, "  " + *s);
    }
    const size_t total = in.history.rows.size();
    out += "\r\nCambios";
    if (total == 0) {
        out += "\r\n";
        Line(out, "  " + in.history.empty_text);
    } else {
        out += " (" + std::to_string(total) + (total > kMaxRows ? ", los " + std::to_string(kMaxRows) + " más recientes" : "") + ")\r\n";
        for (size_t i = 0; i < std::min(total, kMaxRows); ++i) {
            const auto& r = in.history.rows[i];
            Line(out, "  " + r.when + " | " + r.what + " | antes: " + r.before + " | ahora: " + r.after + " | " + r.state);
        }
    }
    if (!in.status.empty()) {
        out += "\r\n";
        Line(out, "Último aviso: " + in.status);
    }
    return out;
}

} // namespace vegaflow::session
