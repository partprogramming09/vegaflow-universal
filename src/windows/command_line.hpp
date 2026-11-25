#pragma once
// The command line handed to the elevated instance (--elevate relaunches through UAC with the same flags).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "ports/text_convert.hpp"

namespace vegaflow {

// The arguments of `command_line` (as GetCommandLineW gives it) in UTF-8. main()'s argv comes in the ANSI code page,
// so a path with "ñ" or an accent would not survive Utf8ToWide.
inline std::vector<std::string> Utf8Args(const wchar_t* command_line) {
    std::vector<std::string> out;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(command_line, &count);
    if (!wide) return out;
    for (int i = 0; i < count; ++i) out.push_back(app::WideToUtf8(wide[i]));
    LocalFree(wide);
    return out;
}

// One argument quoted so CommandLineToArgvW reads it back whole: backslashes are doubled only before a quote
// (embedded or the closing one), and an embedded quote becomes \".
inline std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');
    return out + L"\"";
}

// Every argument but --elevate/-e, joined by spaces and quoted when needed.
inline std::wstring BuildForwardedArgs(int argc, char* argv[]) {
    std::wstring forward_args;
    for (int j = 1; j < argc; ++j) {
        std::string a = argv[j];
        if (a != "--elevate" && a != "-e") {
            if (!forward_args.empty()) forward_args += L" ";
            forward_args += QuoteArg(app::Utf8ToWide(a));
        }
    }
    return forward_args;
}

} // namespace vegaflow
