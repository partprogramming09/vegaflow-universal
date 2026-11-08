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
    // Operaciones seguras de I/O
    static bool SafeWriteFile(const std::wstring& path, const std::string& content, bool is_anticheat_protected = false);
    static bool SafeCopyFile(const std::wstring& src, const std::wstring& dst, bool overwrite, bool is_anticheat_protected = false);
    static bool SafeRemoveFile(const std::wstring& path, bool is_anticheat_protected = false);
    static bool SafeReadFile(const std::wstring& path, std::string& out_content);

    // Validación de límites y seguridad de rutas
    static SandboxValidationResult ValidatePath(const std::wstring& path, SandboxAction action, bool is_anticheat_protected = false);

    // Normalización canónica (GetFullPathNameW + weakly_canonical, resuelve '..', '.', symlinks)
    static std::wstring CanonicalizePath(const std::wstring& path);

    // Reglas de evaluación
    static bool IsBlacklisted(const std::wstring& canonical_path, std::string* out_reason = nullptr);
    static bool IsWhitelisted(const std::wstring& canonical_path, std::string* out_reason = nullptr);
    static bool IsDriveRootOrDirectChild(const std::filesystem::path& canonical_path);

    // Detección e inspección de Anti-Cheat
    static bool DirectoryContainsAntiCheat(const std::filesystem::path& dir);
    static bool PathContainsAntiCheatSignature(const std::wstring& path);
    static bool CanModifyGamePath(const std::filesystem::path& targetDir);

    // Whitelist dinámica (para bibliotecas de juegos registradas o directorios de prueba)
    static void AddAllowedPath(const std::wstring& path);
    static void RemoveAllowedPath(const std::wstring& path);
    static void ClearCustomAllowedPaths();
    static std::vector<std::wstring> GetAllowedPaths();

    // Registro de violaciones de seguridad
    static std::vector<SandboxViolationRecord> GetViolations();
    static void ClearViolations();
    static size_t GetViolationCount();

    // Utilidades de depuración
    static const char* ActionToString(SandboxAction action);
    static const char* DenialReasonToString(SandboxDenialReason reason);

private:
    static void RecordViolation(const std::wstring& path, SandboxAction action, SandboxDenialReason reason, const std::string& message);
};

} // namespace vegaflow
