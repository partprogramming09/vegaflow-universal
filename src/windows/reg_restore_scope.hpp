#pragma once
// Which registry keys a RegBackup record may be restored into. A record is read back from a key any user can write
// (HKCU), so its target is never trusted as stored: only the keys VegaFlow itself edits, in the hive that holds it.

#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace vegaflow {

// hive ids as in registry.cpp: 0 = HKLM, 1 = HKCU.
inline bool IsAllowedRestoreTarget(int hive, int hive_of_backup, std::wstring_view subkey) {
    if (hive != hive_of_backup) return false;
    if (hive != 0 && hive != 1) return false;
    std::wstring s;
    for (wchar_t c : subkey) s.push_back(c == L'/' ? L'\\' : static_cast<wchar_t>(std::towlower(c)));
    // ".." could climb out of an allowed prefix.
    if (s.find(L"..") != std::wstring::npos) return false;
    static constexpr std::array<std::wstring_view, 9> kAllowed = {
        L"software\\microsoft\\directx",
        L"software\\microsoft\\windows nt\\currentversion\\multimedia\\systemprofile",
        L"software\\microsoft\\windows nt\\currentversion\\appcompatflags\\layers",
        L"software\\microsoft\\windows\\currentversion\\gamedvr",
        L"software\\microsoft\\windows\\dwm",
        L"system\\currentcontrolset\\control\\class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
        L"system\\currentcontrolset\\control\\prioritycontrol",
        L"system\\currentcontrolset\\control\\session manager\\memory management",
        L"system\\gameconfigstore",
    };
    for (auto root : kAllowed) {
        if (s == root || (s.size() > root.size() && s.compare(0, root.size(), root) == 0 && s[root.size()] == L'\\')) {
            return true;
        }
    }
    return false;
}

} // namespace vegaflow
