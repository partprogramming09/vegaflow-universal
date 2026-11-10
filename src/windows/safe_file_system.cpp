#include "windows/safe_file_system.hpp"

#include "safety/sandbox_guard.hpp" // Migration: SandboxGuard's I/O moves here in phase 1 (docs/plan/maestro/).

namespace vegaflow {

bool SafeFileSystem::Read(const std::wstring& path, std::string& out_content) {
    return SandboxGuard::SafeReadFile(path, out_content);
}

bool SafeFileSystem::Write(const std::wstring& path, const std::string& content, bool anticheat_protected) {
    return SandboxGuard::SafeWriteFile(path, content, anticheat_protected);
}

bool SafeFileSystem::Copy(const std::wstring& src, const std::wstring& dst, bool overwrite, bool anticheat_protected) {
    return SandboxGuard::SafeCopyFile(src, dst, overwrite, anticheat_protected);
}

bool SafeFileSystem::Remove(const std::wstring& path, bool anticheat_protected) {
    return SandboxGuard::SafeRemoveFile(path, anticheat_protected);
}

bool SafeFileSystem::CanModifyGameDir(const std::filesystem::path& dir) {
    return SandboxGuard::CanModifyGamePath(dir);
}

bool SafeFileSystem::HasAntiCheatSignature(const std::wstring& path) {
    return SandboxGuard::PathContainsAntiCheatSignature(path);
}

bool SafeFileSystem::CanWrite(const std::wstring& path) {
    return SandboxGuard::ValidatePath(path, SandboxAction::Write, false).allowed;
}

} // namespace vegaflow
