#pragma once

#include <string>

namespace vegaflow::app {

std::string WideToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWide(const std::string& str);

} // namespace vegaflow::app
