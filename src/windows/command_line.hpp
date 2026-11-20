#pragma once
// The command line handed to the elevated instance (--elevate relaunches through UAC with the same flags).

#include <string>

#include "windows/text_convert.hpp"

namespace vegaflow {

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
