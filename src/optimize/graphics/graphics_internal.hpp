#pragma once
// Internal helpers shared by the GraphicsOptimizer family implementations (pure C++, no <windows.h>).

#include "optimize/graphics/graphics_optimizer.hpp"

#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>

namespace vegaflow::graphics_internal {

inline constexpr wchar_t kDisplayClassBase[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}";
inline constexpr const wchar_t* kAdapterSubkeys[] = { L"0000", L"0001", L"0002", L"0003" };
inline constexpr wchar_t kDirectXBasePath[] = L"SOFTWARE\\Microsoft\\DirectX";

inline constexpr wchar_t kMmcssProfilePath[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile";
inline constexpr wchar_t kMmcssGamesPath[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games";

// Legacy backup cleanup list includes "GPU Priority" so --restore-registry and RestoreTaskTweaks
// still clean up backups created by earlier VegaFlow builds.
inline constexpr const wchar_t* kMmcssGamesCleanupValues[] = {
    L"GPU Priority", L"Priority", L"Scheduling Category", L"SFIO Priority", L"Background Only", L"Clock Rate"
};

inline constexpr const wchar_t* kUmdBoostValues[] = {
    L"SurfaceFormatReplacements", L"FlipQueueSize", L"TFQ", L"Tessellation_OPTION",
    L"Tessellation", L"Main3D_DEF", L"TurboSync", L"DisableFlipQueueOptimization"
};

inline std::wstring ToLower(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t ch : str) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

inline std::string WideToUtf8(std::wstring_view w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
            uint32_t low = static_cast<uint32_t>(w[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                ++i;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Strict AMD adapter verification under Control\Class\{4d36e968-...}\0000..0003:
// 1. If MatchingDeviceId exists, require "ven_1002" (reject Intel/NVIDIA immediately).
// 2. Otherwise fall back to DriverDesc / ProviderName strings.
// Never infer AMD from UMD or ShaderCache presence.
inline bool IsAmdAdapterKey(RegistryPort& reg, const std::wstring& adapter_path) {
    if (!reg.KeyExists(RegHive::LocalMachine, adapter_path)) {
        return false;
    }

    RegValue match_id;
    if (reg.ReadValue(RegHive::LocalMachine, adapter_path, L"MatchingDeviceId", match_id) &&
        match_id.type == RegValueType::String) {
        return ToLower(match_id.str).find(L"ven_1002") != std::wstring::npos;
    }

    RegValue desc;
    if (reg.ReadValue(RegHive::LocalMachine, adapter_path, L"DriverDesc", desc) &&
        desc.type == RegValueType::String) {
        std::wstring d = ToLower(desc.str);
        if (d.find(L"amd") != std::wstring::npos || d.find(L"radeon") != std::wstring::npos) {
            return true;
        }
    }

    RegValue prov;
    if (reg.ReadValue(RegHive::LocalMachine, adapter_path, L"ProviderName", prov) &&
        prov.type == RegValueType::String) {
        std::wstring p = ToLower(prov.str);
        if (p.find(L"amd") != std::wstring::npos || p.find(L"advanced micro devices") != std::wstring::npos) {
            return true;
        }
    }

    return false;
}

// Strict AMD subkey verification under HKLM\SOFTWARE\Microsoft\DirectX\*:
// 1. If VendorId exists, require 0x1002 (4098).
// 2. Otherwise fall back to Description string.
inline bool IsAmdDirectXSubkey(RegistryPort& reg, const std::wstring& dx_subkey) {
    RegValue vendor_id;
    if (reg.ReadValue(RegHive::LocalMachine, dx_subkey, L"VendorId", vendor_id)) {
        if (vendor_id.type == RegValueType::Dword) {
            return vendor_id.dword == 0x1002u;
        }
        if (vendor_id.type == RegValueType::Binary && vendor_id.binary.size() >= 4) {
            const uint32_t vid = static_cast<uint32_t>(vendor_id.binary[0]) |
                                 (static_cast<uint32_t>(vendor_id.binary[1]) << 8) |
                                 (static_cast<uint32_t>(vendor_id.binary[2]) << 16) |
                                 (static_cast<uint32_t>(vendor_id.binary[3]) << 24);
            return vid == 0x1002u;
        }
        return false;
    }

    RegValue desc;
    if (reg.ReadValue(RegHive::LocalMachine, dx_subkey, L"Description", desc) &&
        desc.type == RegValueType::String) {
        std::wstring d = ToLower(desc.str);
        return (d.find(L"amd") != std::wstring::npos || d.find(L"radeon") != std::wstring::npos);
    }

    return false;
}

} // namespace vegaflow::graphics_internal
