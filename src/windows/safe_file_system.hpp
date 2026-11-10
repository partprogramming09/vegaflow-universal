#pragma once
// The real FileSystem port: every call goes through SandboxGuard (canonical path, blacklist, whitelist,
// anti-cheat lockdown, audit log), exactly as the optimizers called it before the port existed.

#include "ports/file_system.hpp"

namespace vegaflow {

class SafeFileSystem final : public FileSystem {
public:
    bool Read(const std::wstring& path, std::string& out_content) override;
    bool Write(const std::wstring& path, const std::string& content, bool anticheat_protected = false) override;
    bool Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, bool anticheat_protected = false) override;
    bool Remove(const std::wstring& path, bool anticheat_protected = false) override;
    bool CanModifyGameDir(const std::filesystem::path& dir) override;
    bool HasAntiCheatSignature(const std::wstring& path) override;
    bool CanWrite(const std::wstring& path) override;
    void AllowReading(const std::wstring& dir) override;
};

} // namespace vegaflow
