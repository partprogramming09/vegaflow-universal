#pragma once
// Console questions that can never wait forever: what stdin is, and a line read with a deadline.

#include <optional>
#include <string>

#include "model/console.hpp"

namespace vegaflow {

// Classifies stdin; a console whose window is hidden (scheduled task, Start-Process -WindowStyle Hidden) is HiddenConsole.
StdinKind ClassifyStdin();

// Prints the prompt and reads one line from the console input as UTF-8, key by key, until Enter or timeout_ms.
// nullopt on timeout or read error: the caller takes its no-answer default. Call only for StdinKind::VisibleConsole.
std::optional<std::string> ReadConsoleLine(const std::string& prompt, unsigned timeout_ms);

} // namespace vegaflow
