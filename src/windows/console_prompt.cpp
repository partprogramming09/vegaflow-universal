#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows/console_prompt.hpp"

namespace vegaflow {

StdinKind ClassifyStdin() {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (!in || in == INVALID_HANDLE_VALUE) return StdinKind::None;
    switch (GetFileType(in)) {
    case FILE_TYPE_PIPE: return StdinKind::Pipe;
    case FILE_TYPE_DISK: return StdinKind::File;
    case FILE_TYPE_CHAR: {
        DWORD mode = 0;
        if (!GetConsoleMode(in, &mode)) return StdinKind::None; // NUL or another character device, not a console
        HWND window = GetConsoleWindow();
        return window && IsWindowVisible(window) ? StdinKind::VisibleConsole : StdinKind::HiddenConsole;
    }
    default: return StdinKind::None;
    }
}

KeyRead ConsoleKeyInput::Next(std::chrono::milliseconds max_wait) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (WaitForSingleObject(in, static_cast<DWORD>(max_wait.count())) != WAIT_OBJECT_0) return {KeyRead::Kind::Timeout, 0};
    INPUT_RECORD rec{};
    DWORD read = 0;
    if (!ReadConsoleInputW(in, &rec, 1, &read)) return {KeyRead::Kind::Failed, 0};
    if (read == 0) return {KeyRead::Kind::Eof, 0};
    if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) return {KeyRead::Kind::Ignored, 0}; // mouse, focus, resize, key up
    return {KeyRead::Kind::Key, static_cast<char16_t>(rec.Event.KeyEvent.uChar.UnicodeChar)};
}

void ConsoleKeyInput::Discard() {
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
}

} // namespace vegaflow
