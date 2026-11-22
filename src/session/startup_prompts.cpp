#include "session/startup_prompts.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ostream>
#include <vector>

namespace vegaflow::session {

bool CanAskPerson(StdinKind kind) {
    return kind == StdinKind::VisibleConsole;
}

bool IsAffirmative(std::string s, bool empty_means_yes) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return empty_means_yes;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s == "s" || s == "si" || s == "sí" || s == "y" || s == "yes";
}

bool ShouldRevertPending(const std::optional<std::string>& answer) {
    return answer && IsAffirmative(*answer, true);
}

std::optional<bool> RestorePointAnswer(const std::optional<std::string>& answer) {
    if (!answer) return std::nullopt;
    return IsAffirmative(*answer, false);
}

namespace {

// UTF-16 code unit outside the surrogate range to UTF-8.
std::string Utf8(char16_t c) {
    if (c < 0x80) return std::string(1, static_cast<char>(c));
    if (c < 0x800) return {static_cast<char>(0xC0 | (c >> 6)), static_cast<char>(0x80 | (c & 0x3F))};
    return {static_cast<char>(0xE0 | (c >> 12)), static_cast<char>(0x80 | ((c >> 6) & 0x3F)),
            static_cast<char>(0x80 | (c & 0x3F))};
}

} // namespace

std::optional<std::string> ReadLineWithDeadline(KeyInput& input, const Clock& clock, std::ostream& out,
                                                const std::string& prompt, unsigned timeout_ms) {
    using std::chrono::milliseconds;
    out << prompt << std::flush;
    std::string line;
    std::vector<size_t> widths; // UTF-8 bytes of each typed character, so Backspace removes whole characters
    const auto deadline = clock.Now() + milliseconds(timeout_ms);
    for (auto now = clock.Now(); now < deadline; now = clock.Now()) {
        const KeyRead r = input.Next(std::chrono::duration_cast<milliseconds>(deadline - now));
        if (r.kind == KeyRead::Kind::Timeout || r.kind == KeyRead::Kind::Eof || r.kind == KeyRead::Kind::Failed) break;
        if (r.kind != KeyRead::Kind::Key) continue;
        const char16_t c = r.ch;
        if (c == u'\r') {
            out << "\n";
            return line;
        }
        if (c == u'\b') {
            if (!widths.empty()) {
                line.resize(line.size() - widths.back());
                widths.pop_back();
                out << "\b \b" << std::flush;
            }
        } else if (c >= 0x20 && (c < 0xD800 || c > 0xDFFF) && widths.size() < kMaxPromptLine) {
            const std::string bytes = Utf8(c);
            line += bytes;
            widths.push_back(bytes.size());
            out << bytes << std::flush;
        }
    }
    out << "\n";
    input.Discard();
    return std::nullopt;
}

} // namespace vegaflow::session
