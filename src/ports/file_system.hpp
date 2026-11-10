#pragma once
// Port: the file operations the optimizers need, so a use case can be tested with a fake file system
// (tests/unit/) and the real one goes through SandboxGuard (windows/safe_file_system). Only standard types here.

#include <filesystem>
#include <string>

namespace vegaflow {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual bool Read(const std::wstring& path, std::string& out_content) = 0;
    // `anticheat_protected`: the target belongs to a game with anti-cheat; the real file system refuses it.
    virtual bool Write(const std::wstring& path, const std::string& content, bool anticheat_protected = false) = 0;
    virtual bool Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, bool anticheat_protected = false) = 0;
    virtual bool Remove(const std::wstring& path, bool anticheat_protected = false) = 0;

    // Whether files of a game folder may be changed at all (allowed folder and no anti-cheat in it).
    virtual bool CanModifyGameDir(const std::filesystem::path& dir) = 0;
    // Whether the path names a known anti-cheat (EasyAntiCheat, BattlEye, ...).
    virtual bool HasAntiCheatSignature(const std::wstring& path) = 0;
    // Whether a write to this path would be allowed, without writing.
    virtual bool CanWrite(const std::wstring& path) = 0;
    // Lets Read and the source of Copy use files under `dir` (writing there stays refused).
    virtual void AllowReading(const std::wstring& dir) = 0;
};

} // namespace vegaflow
