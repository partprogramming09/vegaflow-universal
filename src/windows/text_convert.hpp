#pragma once

#include <string>

namespace vegaflow::app {

std::string WideToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWide(const std::string& str);

// Windows file names compare without regard to case, for every letter and not only ASCII (ordinal comparison, upper-cased).
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);
// False when the bytes are not well-formed UTF-8 (Utf8ToWide would replace them with U+FFFD).
bool IsValidUtf8(const std::string& str);

} // namespace vegaflow::app
