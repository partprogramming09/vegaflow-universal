#include "windows/safe_file_system.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "windows/sandbox_guard.hpp"

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

bool SafeFileSystem::Replace(const std::wstring& path, const std::string& content) {
    return SandboxGuard::SafeReplaceFile(path, content);
}

bool SafeFileSystem::Exists(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    const DWORD error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
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

void SafeFileSystem::AllowReading(const std::wstring& dir) {
    SandboxGuard::AddReadOnlyPath(dir);
}

} // namespace vegaflow
