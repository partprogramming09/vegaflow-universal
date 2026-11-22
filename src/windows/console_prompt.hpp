#pragma once
// Console questions that can never wait forever: what stdin is, and its key events for a read with a deadline.

#include "model/console.hpp"
#include "ports/key_input.hpp"

namespace vegaflow {

// Classifies stdin; a console whose window is hidden (scheduled task, Start-Process -WindowStyle Hidden) is HiddenConsole.
StdinKind ClassifyStdin();

// The real console input. The deadline and the key rules live in session::ReadLineWithDeadline; this only waits for
// and reads one event, and Discard flushes the console input buffer. Use only for StdinKind::VisibleConsole.
class ConsoleKeyInput final : public KeyInput {
public:
    KeyRead Next(std::chrono::milliseconds max_wait) override;
    void Discard() override;
};

} // namespace vegaflow
