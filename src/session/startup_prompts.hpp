#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>

#include "model/console.hpp"
#include "ports/clock.hpp"
#include "ports/key_input.hpp"

namespace vegaflow::session {

// How long a startup question waits for a person before taking its safe default.
inline constexpr unsigned kStartupPromptTimeoutMs = 30000;

// Longest line a startup question keeps; further characters are dropped (the answers are one word).
inline constexpr size_t kMaxPromptLine = 256;

// Prints the prompt to out, then reads one line key by key until Enter or until timeout_ms have passed on the clock,
// echoing what is typed (Backspace erases; other control keys and surrogate halves are ignored). Returns the line as
// UTF-8, or nullopt when nobody answered in time, the input ended or the read failed: the caller takes its safe
// default, and keys still waiting in the input are discarded so a half answer cannot reach the next question.
// A key that arrives exactly at the deadline counts; one a moment later does not.
std::optional<std::string> ReadLineWithDeadline(KeyInput& input, const Clock& clock, std::ostream& out,
                                                const std::string& prompt, unsigned timeout_ms);

// True only for a visible console: a hidden one (scheduled task, Start-Process -WindowStyle Hidden), a pipe, a file or
// no stdin never has anyone typing, so asking there would only wait. --once (once_run) is one non-interactive pass
// and never asks, even on a visible console.
bool CanAskPerson(StdinKind kind, bool once_run = false);

// Yes/no reading of a typed line ("s", "si", "sí", "y", "yes", any case); an empty line is empty_means_yes.
bool IsAffirmative(std::string answer, bool empty_means_yes);

// "Revert pending changes? [S/n]": nullopt (nobody answered) never reverts; an answered empty line means yes.
bool ShouldRevertPending(const std::optional<std::string>& answer);

// "Create a restore point? [s/N]": nullopt when nobody answered, so the journal is not marked as asked and the next
// attended start offers it again; otherwise the person's yes or no (empty means no).
std::optional<bool> RestorePointAnswer(const std::optional<std::string>& answer);

} // namespace vegaflow::session
