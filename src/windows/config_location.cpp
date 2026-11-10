#include "windows/config_location.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <filesystem>
#include <vector>

#include "bridge/rust_bridge.h"

namespace vegaflow {

namespace {

constexpr wchar_t kTrustedInstallerSid[] = L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464";

uint8_t SidKind(PSID sid) {
    if (!sid || !IsValidSid(sid)) return VF_SID_OTHER;
    if (IsWellKnownSid(sid, WinLocalSystemSid)) return VF_SID_SYSTEM;
    if (IsWellKnownSid(sid, WinBuiltinAdministratorsSid)) return VF_SID_ADMINISTRATORS;
    PSID ti = nullptr;
    if (!ConvertStringSidToSidW(kTrustedInstallerSid, &ti)) return VF_SID_OTHER;
    const bool is_ti = EqualSid(sid, ti) != FALSE;
    LocalFree(ti);
    return is_ti ? VF_SID_TRUSTED_INSTALLER : VF_SID_OTHER;
}

// Allow entries whose SID sits at SidStart; object and callback-object entries put it elsewhere, so they are read as
// another account.
bool SidAtStart(BYTE type) {
    return type == ACCESS_ALLOWED_ACE_TYPE || type == ACCESS_DENIED_ACE_TYPE || type == ACCESS_ALLOWED_CALLBACK_ACE_TYPE ||
           type == ACCESS_DENIED_CALLBACK_ACE_TYPE;
}

bool IsDenyType(BYTE type) {
    return type == ACCESS_DENIED_ACE_TYPE || type == ACCESS_DENIED_OBJECT_ACE_TYPE ||
           type == ACCESS_DENIED_CALLBACK_ACE_TYPE || type == ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE;
}

} // namespace

std::wstring ConfigFilePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) return L"";
        if (len < buf.size()) {
            const std::filesystem::path exe(std::wstring(buf.data(), len));
            return (exe.parent_path() / L"config" / L"config.json").wstring();
        }
        if (buf.size() >= 32768) return L"";
        buf.resize(buf.size() * 2);
    }
}

bool OnlyAdminsCanWrite(const std::wstring& path) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                              &owner, nullptr, &dacl, nullptr, &sd) != ERROR_SUCCESS) {
        return false;
    }
    std::vector<VfAceEntry> aces;
    bool readable = true;
    if (dacl) {
        for (DWORD i = 0; i < dacl->AceCount; ++i) {
            void* raw = nullptr;
            if (!GetAce(dacl, i, &raw)) {
                readable = false;
                break;
            }
            const auto* header = static_cast<const ACE_HEADER*>(raw);
            // Every ACE type starts with the header followed by the access mask.
            const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(raw);
            VfAceEntry e{};
            e.mask = allowed->Mask;
            e.sid_kind = SidAtStart(header->AceType) ? SidKind(const_cast<DWORD*>(&allowed->SidStart)) : VF_SID_OTHER;
            e.allows = !IsDenyType(header->AceType);
            e.inherit_only = (header->AceFlags & INHERIT_ONLY_ACE) != 0;
            aces.push_back(e);
        }
    }
    const bool trusted = readable && vf_rust_only_admins_can_write(dacl != nullptr, SidKind(owner), aces.data(), aces.size());
    LocalFree(sd);
    return trusted;
}

namespace {

// A path that does not exist yet is created later inside a checked folder, so it does not make the location untrusted.
// Only metadata is read here, so it does not go through SandboxGuard.
bool MissingOrAdminOnly(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    return OnlyAdminsCanWrite(path);
}

} // namespace

bool ConfigLocationTrusted(const std::wstring& config_path) {
    if (config_path.empty()) return false;
    const std::filesystem::path config_dir = std::filesystem::path(config_path).parent_path();
    return OnlyAdminsCanWrite(config_dir.parent_path().wstring()) && MissingOrAdminOnly(config_dir.wstring()) &&
           MissingOrAdminOnly(config_path);
}

} // namespace vegaflow
