#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows/console_prompt.hpp"

#include <iostream>

#include "windows/text_convert.hpp"

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

std::optional<std::string> ReadConsoleLine(const std::string& prompt, unsigned timeout_ms) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::cout << prompt << std::flush;
    // Key events instead of std::getline: getline has no deadline, and one typed key would make it wait for Enter forever.
    std::wstring line;
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (ULONGLONG now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        if (WaitForSingleObject(in, static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0) break;
        INPUT_RECORD rec{};
        DWORD read = 0;
        if (!ReadConsoleInputW(in, &rec, 1, &read) || read == 0) break;
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue; // mouse, focus, resize, key up
        const wchar_t c = rec.Event.KeyEvent.uChar.UnicodeChar;
        if (c == L'\r') {
            std::cout << "\n";
            return app::WideToUtf8(line);
        }
        if (c == L'\b') {
            if (!line.empty()) {
                line.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if (c >= 0x20) {
            line.push_back(c);
            std::cout << app::WideToUtf8(std::wstring(1, c)) << std::flush;
        }
    }
    std::cout << "\n";
    return std::nullopt;
}

} // namespace vegaflow
