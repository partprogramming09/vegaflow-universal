#include "windows/registry.hpp"
#include "windows/unique_handle.hpp"

namespace vegaflow {

bool QueryRegString(HKEY root, const wchar_t* sub_key, const wchar_t* value_name, std::wstring& out) {
    UniqueRegKey key;
    if (RegOpenKeyExW(root, sub_key, 0, KEY_READ, key.put()) != ERROR_SUCCESS) return false;
    wchar_t buf[1024]{};
    DWORD type = 0;
    DWORD size = sizeof(buf) - sizeof(wchar_t); // the last character stays NUL
    if (RegQueryValueExW(key.get(), value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS) return false;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
    out = buf;
    return true;
}

} // namespace vegaflow
