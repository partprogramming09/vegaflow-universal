#pragma once
// Port: key events of an interactive console, so a question with a deadline can be tested with scripted keys and a
// fake clock instead of a real console and real seconds. windows/console_prompt.cpp is the real one.

#include <chrono>

namespace vegaflow {

struct KeyRead {
    enum class Kind {
        Timeout, // nothing arrived within max_wait
        Eof,     // the input ended
        Failed,  // the read itself failed
        Ignored, // an event that is not a key press (mouse, focus, resize, key up)
        Key,     // a key press carrying ch (UTF-16 code unit; '\r' is Enter, '\b' is Backspace)
    };
    Kind kind = Kind::Timeout;
    char16_t ch = 0;
};

class KeyInput {
public:
    virtual ~KeyInput() = default;
    // Waits up to max_wait for the next event. A key that arrives exactly at the end of max_wait is delivered.
    virtual KeyRead Next(std::chrono::milliseconds max_wait) = 0;
    // Throws away input that was typed but not read yet.
    virtual void Discard() = 0;
};

} // namespace vegaflow
