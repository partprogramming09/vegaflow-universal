#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vegaflow {

enum class SandboxAction {
    Read,
    Write,
    Copy,
    Delete
};

enum class SandboxDenialReason {
    None = 0,
    EmptyPath,
    PathTraversalDetected,
    BlacklistedSystemDirectory,
    DriveRootRestricted,
    NotWhitelisted,
    AntiCheatLocked,
    IoError
};

struct SandboxValidationResult {
    bool allowed = false;
    SandboxDenialReason reason = SandboxDenialReason::None;
    std::wstring canonical_path;
    std::string message;

    explicit operator bool() const { return allowed; }
};

struct SandboxViolationRecord {
    std::wstring path;
    SandboxAction action;
    SandboxDenialReason reason;
    std::string message;
    uint64_t timestamp_ms = 0;
};

class SandboxGuard {
public:
    // Safe I/O operations
    static bool SafeWriteFile(const std::wstring& path, const std::string& content, bool is_anticheat_protected = false);
    static bool SafeCopyFile(const std::wstring& src, const std::wstring& dst, bool overwrite, bool is_anticheat_protected = false);
    static bool SafeRemoveFile(const std::wstring& path, bool is_anticheat_protected = false);
    static bool SafeReadFile(const std::wstring& path, std::string& out_content);
    // Writes "<path>.vegaflow.tmp" with the checks of SafeWriteFile, flushes it and renames it over `path` in one step.
    static bool SafeReplaceFile(const std::wstring& path, const std::string& content);

    // Boundary validation and path security
    static SandboxValidationResult ValidatePath(const std::wstring& path, SandboxAction action, bool is_anticheat_protected = false);

    // Absolute path with "." and ".." resolved, "\\?\X:\" removed, and the existing part replaced by its real path
    // (junctions, symlinks and 8.3 names resolved). Other paths starting with "\\" come back unchanged.
    static std::wstring CanonicalizePath(const std::wstring& path);

    // Blacklist alone (Test 33); ValidatePath runs the whole decision in the Rust core (policy/file_access.rs).
    static bool IsBlacklisted(const std::wstring& canonical_path, std::string* out_reason = nullptr);

    // Anti-Cheat detection and inspection
    static bool DirectoryContainsAntiCheat(const std::filesystem::path& dir);
    static bool PathContainsAntiCheatSignature(const std::wstring& path);
    static bool CanModifyGamePath(const std::filesystem::path& targetDir);

    // Dynamic whitelist (for registered game libraries or test directories)
    static void AddAllowedPath(const std::wstring& path);
    static void RemoveAllowedPath(const std::wstring& path);
    static void ClearCustomAllowedPaths(); // also clears the read-only folders
    static void AddReadOnlyPath(const std::wstring& path);

    // Steam libraries whose steamapps\common folders are allowed. Read once from libraryfolders.vdf unless set here.
    static void SetSteamLibraryFolders(std::vector<std::wstring> folders);
    static void ReloadSteamLibraryFolders();

    // Test seam: runs after a path is validated and before the file is opened (Test 42 swaps a folder there).
    static void SetBeforeOpenHookForTesting(void (*hook)());
    static std::vector<std::wstring> GetAllowedPaths();

    // Security violation log
    static std::vector<SandboxViolationRecord> GetViolations();
    static void ClearViolations();
    static size_t GetViolationCount();

    // Debugging utilities
    static const char* ActionToString(SandboxAction action);
    static const char* DenialReasonToString(SandboxDenialReason reason);

private:
    static bool WriteVerified(const std::wstring& canonical, const std::string& content, SandboxAction action,
                              bool is_anticheat_protected, bool overwrite, bool flush = false);
    static bool ReadVerified(const std::wstring& canonical, std::string& out_content);
    static bool OpenedFileAllowed(HANDLE h, const std::wstring& validated, SandboxAction action,
                                  bool is_anticheat_protected);
    static void RecordViolation(const std::wstring& path, SandboxAction action, SandboxDenialReason reason, const std::string& message);
};

} // namespace vegaflow
