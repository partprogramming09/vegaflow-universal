#include "session/startup_prompts.hpp"

#include <algorithm>
#include <cctype>

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

} // namespace vegaflow::session
