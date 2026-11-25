#pragma once
// The file-name part of a Windows path: what follows the last \ / or drive colon. It is what
// std::filesystem::path::filename() gives on Windows, written out so the same code gives the same answer on the host
// (where a backslash is an ordinary character) and the screens can be tested there.

#include <string>

namespace vegaflow {

inline std::wstring FileNameOf(const std::wstring& path) {
    const size_t cut = path.find_last_of(L"\\/:");
    return cut == std::wstring::npos ? path : path.substr(cut + 1);
}

} // namespace vegaflow
