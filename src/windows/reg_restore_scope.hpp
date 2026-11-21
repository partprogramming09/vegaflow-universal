#pragma once
// Which registry keys a RegBackup record may be restored into. A record is read back from a key any user can write
// (HKCU), so its target is never trusted as stored: only the keys VegaFlow itself edits, in the hive that holds it.

#include <array>
#include <cwctype>
#include <functional>
#include <string>
#include <string_view>

namespace vegaflow {

// The id a RegBackup record is named after: hive, subkey and value name, lower case.
inline std::wstring RegBackupRecordId(int hive, std::wstring_view subkey, std::wstring_view name) {
    std::wstring id = std::to_wstring(hive) + L"|";
    for (wchar_t c : subkey) id.push_back(static_cast<wchar_t>(std::towlower(c)));
    id += L"|";
    for (wchar_t c : name) id.push_back(static_cast<wchar_t>(std::towlower(c)));
    return id;
}

// Name used by builds up to 2026-11-20 (std::hash): still looked up so their records are found.
inline std::wstring LegacyRegBackupRecordName(int hive, std::wstring_view subkey, std::wstring_view name) {
    return std::to_wstring(std::hash<std::wstring>{}(RegBackupRecordId(hive, subkey, name)));
}

// FNV-1a (64 bits) over the UTF-16 units of the id: the same name with every compiler and build.
inline std::wstring RegBackupRecordName(int hive, std::wstring_view subkey, std::wstring_view name) {
    unsigned long long h = 0xcbf29ce484222325ULL;
    for (wchar_t c : RegBackupRecordId(hive, subkey, name)) {
        h ^= static_cast<unsigned long long>(static_cast<unsigned short>(c));
        h *= 0x100000001b3ULL;
    }
    static constexpr wchar_t kHex[] = L"0123456789abcdef";
    std::wstring out = L"v2-";
    for (int shift = 60; shift >= 0; shift -= 4) out.push_back(kHex[(h >> shift) & 0xF]);
    return out;
}

// hive ids as in registry.cpp: 0 = HKLM, 1 = HKCU. Each allowed key is tied to the hive VegaFlow edits it in.
inline bool IsAllowedRestoreTarget(int hive, int hive_of_backup, std::wstring_view subkey) {
    if (hive != hive_of_backup) return false;
    if (hive != 0 && hive != 1) return false;
    std::wstring s;
    for (wchar_t c : subkey) {
        // In the registry '/' is a literal character: translating it here would validate a different key than the one opened.
        if (c == L'/') return false;
        s.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    // ".." could climb out of an allowed prefix.
    if (s.find(L"..") != std::wstring::npos) return false;
    struct Allowed {
        int hive;
        std::wstring_view root;
    };
    static constexpr std::array<Allowed, 10> kAllowed = {{
        {0, L"software\\microsoft\\directx"},
        {1, L"software\\microsoft\\directx\\usergpupreferences"},
        {0, L"software\\microsoft\\windows nt\\currentversion\\multimedia\\systemprofile"},
        {1, L"software\\microsoft\\windows nt\\currentversion\\appcompatflags\\layers"},
        {1, L"software\\microsoft\\windows\\currentversion\\gamedvr"},
        {0, L"software\\microsoft\\windows\\dwm"},
        {0, L"system\\currentcontrolset\\control\\class\\{4d36e968-e325-11ce-bfc1-08002be10318}"},
        {0, L"system\\currentcontrolset\\control\\prioritycontrol"},
        {0, L"system\\currentcontrolset\\control\\session manager\\memory management"},
        {1, L"system\\gameconfigstore"},
    }};
    for (const auto& entry : kAllowed) {
        if (entry.hive != hive) continue;
        const auto root = entry.root;
        if (s == root || (s.size() > root.size() && s.compare(0, root.size(), root) == 0 && s[root.size()] == L'\\')) {
            return true;
        }
    }
    return false;
}

} // namespace vegaflow
