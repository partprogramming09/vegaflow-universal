#pragma once
// Reads one string value from the registry.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace vegaflow {

// A REG_SZ or REG_EXPAND_SZ value (not expanded), up to 1023 characters. False if the key or value is missing.
bool QueryRegString(HKEY root, const wchar_t* sub_key, const wchar_t* value_name, std::wstring& out);

} // namespace vegaflow
