#pragma once

#include <optional>
#include <string>

#include "model/console.hpp"

namespace vegaflow::session {

// How long a startup question waits for a person before taking its safe default.
inline constexpr unsigned kStartupPromptTimeoutMs = 30000;

// True only for a visible console: a hidden one (scheduled task, Start-Process -WindowStyle Hidden), a pipe, a file or
// no stdin never has anyone typing, so asking there would only wait.
bool CanAskPerson(StdinKind kind);

// Yes/no reading of a typed line ("s", "si", "sí", "y", "yes", any case); an empty line is empty_means_yes.
bool IsAffirmative(std::string answer, bool empty_means_yes);

// "Revert pending changes? [S/n]": nullopt (nobody answered) never reverts; an answered empty line means yes.
bool ShouldRevertPending(const std::optional<std::string>& answer);

// "Create a restore point? [s/N]": nullopt when nobody answered, so the journal is not marked as asked and the next
// attended start offers it again; otherwise the person's yes or no (empty means no).
std::optional<bool> RestorePointAnswer(const std::optional<std::string>& answer);

} // namespace vegaflow::session
