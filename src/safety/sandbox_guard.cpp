#include "safety/sandbox_guard.hpp"
#include "bridge/rust_bridge.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace vegaflow {

namespace {

std::mutex g_sandbox_mutex;
std::vector<std::wstring> g_custom_allowed_paths;
std::vector<SandboxViolationRecord> g_violations;

std::wstring ToLowerW(std::wstring_view sv) {
    std::wstring result;
    result.reserve(sv.size());
    for (wchar_t c : sv) {
        result.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return result;
}

bool StartsWithInsensitiveW(std::wstring_view str, std::wstring_view prefix) {
    if (prefix.size() > str.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(str[i]) != std::towlower(prefix[i])) {
            return false;
        }
    }
    return true;
}

bool ContainsInsensitiveW(std::wstring_view haystack, std::wstring_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](wchar_t ch1, wchar_t ch2) {
            return std::towlower(ch1) == std::towlower(ch2);
        }
    );
    return it != haystack.end();
}

std::wstring GetEnvVarW(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2] = {0};
    DWORD len = GetEnvironmentVariableW(name, buf, _countof(buf));
    if (len > 0 && len < _countof(buf)) {
        return std::wstring(buf, len);
    }
    return L"";
}

std::wstring GetWindowsDirectoryPath() {
    wchar_t buf[MAX_PATH] = {0};
    UINT len = GetWindowsDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(buf, len);
    }
    return L"C:\\Windows";
}

std::wstring GetSystemDirectoryPath() {
    wchar_t buf[MAX_PATH] = {0};
    UINT len = GetSystemDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::wstring(buf, len);
    }
    return L"C:\\Windows\\System32";
}

std::wstring GetVegaFlowExecutableDirectory() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::error_code ec;
        return std::filesystem::path(buf).parent_path().wstring();
    }
    return L"";
}

uint64_t GetCurrentTimeMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace

std::wstring SandboxGuard::CanonicalizePath(const std::wstring& path) {
    if (path.empty()) return L"";

    // 1. Limpieza de comillas y separadores
    std::wstring cleaned = path;
    if (cleaned.size() >= 2 && cleaned.front() == L'"' && cleaned.back() == L'"') {
        cleaned = cleaned.substr(1, cleaned.size() - 2);
    }
    std::replace(cleaned.begin(), cleaned.end(), L'/', L'\\');

    // 2. Resolver componentes relativos (. y ..) mediante GetFullPathNameW
    wchar_t fullPath[MAX_PATH * 4] = {0};
    DWORD ret = GetFullPathNameW(cleaned.c_str(), _countof(fullPath), fullPath, nullptr);
    std::wstring resolvedStr = (ret > 0 && ret < _countof(fullPath)) ? fullPath : cleaned;

    // 3. Normalización canónica débil (resuelve symlinks y normaliza segmentos que no existan aún)
    std::error_code ec;
    std::filesystem::path p(resolvedStr);
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        canonical = p.lexically_normal();
    }

    std::wstring res = canonical.wstring();
    std::replace(res.begin(), res.end(), L'/', L'\\');

    // Convertir la letra de la unidad a mayúscula si aplica (e.g. c:\ -> C:\)
    if (res.size() >= 2 && res[1] == L':' && std::iswlower(res[0])) {
        res[0] = static_cast<wchar_t>(std::towupper(res[0]));
    }

    // Quitar barra invertida final si no es la raíz de la unidad (e.g. C:\)
    if (res.size() > 3 && res.back() == L'\\') {
        res.pop_back();
    }

    return res;
}

bool SandboxGuard::IsDriveRootOrDirectChild(const std::filesystem::path& canonical_path) {
    if (canonical_path.empty()) return false;

    // Comprobar si la ruta es exactamente la raíz (e.g. "C:\")
    if (canonical_path == canonical_path.root_path()) {
        return true;
    }

    // Comprobar si la ruta es un archivo o carpeta hijo directo de la raíz (e.g. "C:\bootmgr" o "C:\test.txt")
    if (canonical_path.parent_path() == canonical_path.root_path()) {
        return true;
    }

    std::wstring str = canonical_path.wstring();
    if (str.size() <= 3 && str.find(L':') != std::wstring::npos) {
        return true;
    }

    return false;
}

bool SandboxGuard::IsBlacklisted(const std::wstring& canonical_path, std::string* out_reason) {
    if (canonical_path.empty()) {
        if (out_reason) *out_reason = "Ruta vacía";
        return true;
    }

    std::filesystem::path p(canonical_path);

    // 1. Protección de la raíz de unidad
    if (IsDriveRootOrDirectChild(p)) {
        if (out_reason) *out_reason = "Operación denegada: Prohibido escribir en la raíz del disco o sobre archivos raíz directos";
        return true;
    }

    std::wstring lower = ToLowerW(canonical_path);

    // 2. Archivos críticos del sistema operativo
    std::wstring filename = ToLowerW(p.filename().wstring());
    char utf8_name[256] = {0};
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, filename.data(), static_cast<int>(filename.size()), utf8_name, sizeof(utf8_name) - 1, nullptr, nullptr);
    if (utf8_len > 0 && vf_rust_is_critical_system_filename(utf8_name, static_cast<size_t>(utf8_len))) {
        if (out_reason) *out_reason = "Operación denegada: Intento de modificar archivos críticos de arranque de Windows";
        return true;
    }

    // 3. Directorios de Windows y del Sistema
    std::wstring winDir = ToLowerW(GetWindowsDirectoryPath());
    std::wstring sysDir = ToLowerW(GetSystemDirectoryPath());

    if (StartsWithInsensitiveW(lower, winDir) ||
        StartsWithInsensitiveW(lower, sysDir) ||
        ContainsInsensitiveW(lower, L"\\windows\\system32") ||
        ContainsInsensitiveW(lower, L"\\windows\\syswow64") ||
        ContainsInsensitiveW(lower, L"\\windows\\winsxs")) {
        if (out_reason) *out_reason = "Operación denegada: Acceso a directorios del sistema de Windows bloqueado por seguridad";
        return true;
    }

    // Comprobar cualquier otra letra de disco con \windows (e.g. D:\windows)
    if (lower.size() >= 10 && lower[1] == L':' && lower.substr(2, 9) == L"\\windows\\") {
        if (out_reason) *out_reason = "Operación denegada: Acceso a carpeta de Windows bloqueado";
        return true;
    }

    // 4. Program Files / Common Files
    if (ContainsInsensitiveW(lower, L"\\program files\\common files") ||
        ContainsInsensitiveW(lower, L"\\program files (x86)\\common files")) {
        if (out_reason) *out_reason = "Operación denegada: Modificación de Common Files de Program Files bloqueada";
        return true;
    }

    // 5. System Volume Information
    if (ContainsInsensitiveW(lower, L"\\system volume information")) {
        if (out_reason) *out_reason = "Operación denegada: System Volume Information protegido";
        return true;
    }

    return false;
}

bool SandboxGuard::IsWhitelisted(const std::wstring& canonical_path, std::string* out_reason) {
    if (canonical_path.empty()) return false;

    std::wstring lower = ToLowerW(canonical_path);

    // 1. Carpeta del ejecutable de VegaFlow y directorio de trabajo actual
    std::wstring exeDir = ToLowerW(GetVegaFlowExecutableDirectory());
    if (!exeDir.empty() && StartsWithInsensitiveW(lower, exeDir)) {
        return true;
    }

    // Si el ejecutable está dentro de un subdirectorio build/ o bin/, permitir la raíz del proyecto
    std::error_code ec;
    std::filesystem::path exeDirPath(GetVegaFlowExecutableDirectory());
    if (!exeDirPath.empty()) {
        std::wstring parentDir = ToLowerW(exeDirPath.parent_path().wstring());
        if (!parentDir.empty() && StartsWithInsensitiveW(lower, parentDir)) {
            return true;
        }
    }

    std::wstring cwd = ToLowerW(std::filesystem::current_path(ec).wstring());
    if (!ec && !cwd.empty() && StartsWithInsensitiveW(lower, cwd)) {
        return true;
    }

    // 2. Bibliotecas de Steam (steamapps/common/...)
    if (ContainsInsensitiveW(lower, L"\\steamapps\\common\\") ||
        ContainsInsensitiveW(lower, L"/steamapps/common/")) {
        return true;
    }

    // 3. Carpetas de usuario en %LOCALAPPDATA%
    std::wstring localApp = ToLowerW(GetEnvVarW(L"LOCALAPPDATA"));
    if (!localApp.empty() && StartsWithInsensitiveW(lower, localApp)) {
        return true;
    }

    // 4. Directorio temporal de Windows (std::filesystem::temp_directory_path)
    std::wstring tempDir = ToLowerW(std::filesystem::temp_directory_path(ec).wstring());
    if (!ec && !tempDir.empty()) {
        if (tempDir.back() == L'\\') tempDir.pop_back();
        if (StartsWithInsensitiveW(lower, tempDir)) {
            return true;
        }
    }

    // 5. Documentos de usuario y partidas guardadas (%USERPROFILE%\Documents\My Games, Saved Games)
    std::wstring userProfile = ToLowerW(GetEnvVarW(L"USERPROFILE"));
    if (!userProfile.empty()) {
        std::wstring myGames = userProfile + L"\\documents\\my games";
        std::wstring savedGames = userProfile + L"\\saved games";
        if (StartsWithInsensitiveW(lower, myGames) || StartsWithInsensitiveW(lower, savedGames)) {
            return true;
        }
    }

    // 6. Whitelist dinámica registrada explícitamente
    {
        std::lock_guard<std::mutex> lock(g_sandbox_mutex);
        for (const auto& allowed : g_custom_allowed_paths) {
            std::wstring allowedLower = ToLowerW(allowed);
            if (!allowedLower.empty() && StartsWithInsensitiveW(lower, allowedLower)) {
                return true;
            }
        }
    }

    if (out_reason) {
        *out_reason = "La ruta se encuentra fuera de los límites autorizados por VegaFlow Sandbox";
    }
    return false;
}

bool SandboxGuard::PathContainsAntiCheatSignature(const std::wstring& path) {
    if (path.empty()) return false;

    char utf8_path[2048] = {0};
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), utf8_path, sizeof(utf8_path) - 1, nullptr, nullptr);
    if (utf8_len > 0) {
        return vf_rust_has_anticheat_signature(utf8_path, static_cast<size_t>(utf8_len));
    }
    return false;
}

bool SandboxGuard::DirectoryContainsAntiCheat(const std::filesystem::path& dir) {
    if (dir.empty()) return false;

    std::error_code ec;
    std::filesystem::path current = dir;

    std::error_code cwd_ec;
    std::filesystem::path project_root = std::filesystem::current_path(cwd_ec);

    // Escanear la carpeta y hasta 4 niveles hacia arriba (sin sobrepasar la raíz o el límite del proyecto)
    int levels_checked = 0;
    while (!current.empty() && levels_checked < 5) {
        if (!std::filesystem::exists(current, ec)) {
            if (!project_root.empty() && current == project_root) break;
            current = current.parent_path();
            levels_checked++;
            continue;
        }

        // Si la propia ruta contiene la firma
        if (PathContainsAntiCheatSignature(current.wstring())) {
            return true;
        }

        // Inspeccionar los archivos y subdirectorios presentes en la carpeta
        for (const auto& entry : std::filesystem::directory_iterator(
                 current, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;

            std::wstring filename = entry.path().filename().wstring();
            if (PathContainsAntiCheatSignature(filename)) {
                return true;
            }
        }

        // Detenerse si alcanzamos la raíz del proyecto o la raíz de la unidad
        if (current == current.root_path()) {
            break;
        }
        if (!project_root.empty() && current == project_root) {
            break;
        }

        current = current.parent_path();
        levels_checked++;
    }

    return false;
}

bool SandboxGuard::CanModifyGamePath(const std::filesystem::path& targetDir) {
    if (targetDir.empty()) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path dir = targetDir;
    if (std::filesystem::exists(dir, ec) && std::filesystem::is_regular_file(dir, ec)) {
        dir = dir.parent_path();
    }

    std::wstring pathStr = dir.wstring();

    // 1. Detección inmediata de Anti-Cheat por firma de ruta o archivos en el directorio
    if (PathContainsAntiCheatSignature(pathStr) || DirectoryContainsAntiCheat(dir)) {
        RecordViolation(pathStr, SandboxAction::Write, SandboxDenialReason::AntiCheatLocked,
            "Bloqueo Anti-Cheat: Directorio bajo cuarentena de seguridad (anti-cheat detectado)");
        return false;
    }

    // 2. Validación de límites del sandbox
    std::filesystem::path probe = dir / L"dxvk.conf";
    auto val = ValidatePath(probe.wstring(), SandboxAction::Write, false);
    return val.allowed;
}

SandboxValidationResult SandboxGuard::ValidatePath(
    const std::wstring& path,
    SandboxAction action,
    bool is_anticheat_protected) {

    SandboxValidationResult result;
    result.allowed = false;

    if (path.empty()) {
        result.reason = SandboxDenialReason::EmptyPath;
        result.message = "Ruta vacía o no especificada";
        RecordViolation(L"", action, result.reason, result.message);
        return result;
    }

    // 1. Normalización canónica
    std::wstring canonical = CanonicalizePath(path);
    result.canonical_path = canonical;

    if (canonical.empty()) {
        result.reason = SandboxDenialReason::PathTraversalDetected;
        result.message = "Fallo en la resolución canónica de la ruta";
        RecordViolation(path, action, result.reason, result.message);
        return result;
    }

    std::filesystem::path canonicalPathObj(canonical);

    // 2. Comprobar Lista Negra (Blacklist de máxima prioridad)
    std::string blacklistReason;
    if (IsBlacklisted(canonical, &blacklistReason)) {
        result.reason = SandboxDenialReason::BlacklistedSystemDirectory;
        result.message = blacklistReason;
        RecordViolation(canonical, action, result.reason, result.message);
        return result;
    }

    // 3. Protección de Anti-Cheat (Lockdown inmediato si se detecta tecnología kernel o bandera activa)
    if (action != SandboxAction::Read) {
        if (is_anticheat_protected) {
            result.reason = SandboxDenialReason::AntiCheatLocked;
            result.message = "Bloqueo Anti-Cheat activo: Prohibida toda modificación en juegos con protección a nivel de kernel";
            RecordViolation(canonical, action, result.reason, result.message);
            return result;
        }

        if (PathContainsAntiCheatSignature(canonical) || DirectoryContainsAntiCheat(canonicalPathObj.parent_path())) {
            result.reason = SandboxDenialReason::AntiCheatLocked;
            result.message = "Firma de Anti-Cheat detectada en el directorio: Escritura abortada para salvaguardar la cuenta del usuario";
            RecordViolation(canonical, action, result.reason, result.message);
            return result;
        }
    }

    // 4. Comprobar Lista Blanca (Whitelist)
    std::string whitelistReason;
    if (!IsWhitelisted(canonical, &whitelistReason)) {
        result.reason = SandboxDenialReason::NotWhitelisted;
        result.message = whitelistReason;
        RecordViolation(canonical, action, result.reason, result.message);
        return result;
    }

    // Acceso autorizado
    result.allowed = true;
    result.reason = SandboxDenialReason::None;
    result.message = "Operación autorizada dentro de los límites del sandbox";
    return result;
}

bool SandboxGuard::SafeWriteFile(
    const std::wstring& path,
    const std::string& content,
    bool is_anticheat_protected) {

    auto val = ValidatePath(path, SandboxAction::Write, is_anticheat_protected);
    if (!val.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path p(val.canonical_path);

    // Crear directorio padre si no existe
    if (!p.parent_path().empty() && !std::filesystem::exists(p.parent_path(), ec)) {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) {
            RecordViolation(val.canonical_path, SandboxAction::Write, SandboxDenialReason::IoError,
                            "No se pudo crear el directorio de destino: " + ec.message());
            return false;
        }
    }

    std::ofstream out(p, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        RecordViolation(val.canonical_path, SandboxAction::Write, SandboxDenialReason::IoError,
                        "No se pudo abrir el archivo para escritura");
        return false;
    }

    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.flush();
    if (!out.good()) {
        RecordViolation(val.canonical_path, SandboxAction::Write, SandboxDenialReason::IoError,
                        "Error durante la escritura del archivo en disco");
        return false;
    }

    return true;
}

bool SandboxGuard::SafeCopyFile(
    const std::wstring& src,
    const std::wstring& dst,
    bool overwrite,
    bool is_anticheat_protected) {

    // Validar origen para lectura
    auto valSrc = ValidatePath(src, SandboxAction::Read, false);
    if (!valSrc.allowed) {
        return false;
    }

    // Validar destino para escritura / copia
    auto valDst = ValidatePath(dst, SandboxAction::Copy, is_anticheat_protected);
    if (!valDst.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path pSrc(valSrc.canonical_path);
    std::filesystem::path pDst(valDst.canonical_path);

    if (!std::filesystem::exists(pSrc, ec)) {
        RecordViolation(valSrc.canonical_path, SandboxAction::Copy, SandboxDenialReason::IoError,
                        "El archivo de origen no existe");
        return false;
    }

    // Crear directorio padre de destino si no existe
    if (!pDst.parent_path().empty() && !std::filesystem::exists(pDst.parent_path(), ec)) {
        std::filesystem::create_directories(pDst.parent_path(), ec);
        if (ec) {
            RecordViolation(valDst.canonical_path, SandboxAction::Copy, SandboxDenialReason::IoError,
                            "No se pudo crear el directorio padre del archivo destino: " + ec.message());
            return false;
        }
    }

    auto copyOptions = overwrite ? std::filesystem::copy_options::overwrite_existing
                                 : std::filesystem::copy_options::none;

    std::filesystem::copy_file(pSrc, pDst, copyOptions, ec);
    if (ec) {
        RecordViolation(valDst.canonical_path, SandboxAction::Copy, SandboxDenialReason::IoError,
                        "Fallo al copiar archivo: " + ec.message());
        return false;
    }

    return true;
}

bool SandboxGuard::SafeRemoveFile(
    const std::wstring& path,
    bool is_anticheat_protected) {

    auto val = ValidatePath(path, SandboxAction::Delete, is_anticheat_protected);
    if (!val.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path p(val.canonical_path);

    if (!std::filesystem::exists(p, ec)) {
        return true; // Ya no existe
    }

    bool removed = std::filesystem::remove(p, ec);
    if (ec || !removed) {
        RecordViolation(val.canonical_path, SandboxAction::Delete, SandboxDenialReason::IoError,
                        "No se pudo eliminar el archivo: " + ec.message());
        return false;
    }

    return true;
}

bool SandboxGuard::SafeReadFile(const std::wstring& path, std::string& out_content) {
    auto val = ValidatePath(path, SandboxAction::Read, false);
    if (!val.allowed) {
        return false;
    }

    std::error_code ec;
    std::filesystem::path p(val.canonical_path);
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        return false;
    }

    std::ifstream in(p, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    out_content = ss.str();
    return true;
}

void SandboxGuard::AddAllowedPath(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring canonical = CanonicalizePath(path);
    if (canonical.empty()) return;

    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    for (const auto& existing : g_custom_allowed_paths) {
        if (_wcsicmp(existing.c_str(), canonical.c_str()) == 0) {
            return;
        }
    }
    g_custom_allowed_paths.push_back(canonical);
}

void SandboxGuard::RemoveAllowedPath(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring canonical = CanonicalizePath(path);

    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_custom_allowed_paths.erase(
        std::remove_if(
            g_custom_allowed_paths.begin(),
            g_custom_allowed_paths.end(),
            [&](const std::wstring& item) {
                return _wcsicmp(item.c_str(), canonical.c_str()) == 0;
            }
        ),
        g_custom_allowed_paths.end()
    );
}

void SandboxGuard::ClearCustomAllowedPaths() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_custom_allowed_paths.clear();
}

std::vector<std::wstring> SandboxGuard::GetAllowedPaths() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    return g_custom_allowed_paths;
}

void SandboxGuard::RecordViolation(
    const std::wstring& path,
    SandboxAction action,
    SandboxDenialReason reason,
    const std::string& message) {

    SandboxViolationRecord rec;
    rec.path = path;
    rec.action = action;
    rec.reason = reason;
    rec.message = message;
    rec.timestamp_ms = GetCurrentTimeMs();

    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_violations.push_back(std::move(rec));
}

std::vector<SandboxViolationRecord> SandboxGuard::GetViolations() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    return g_violations;
}

void SandboxGuard::ClearViolations() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_violations.clear();
}

size_t SandboxGuard::GetViolationCount() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    return g_violations.size();
}

const char* SandboxGuard::ActionToString(SandboxAction action) {
    switch (action) {
        case SandboxAction::Read: return "Read";
        case SandboxAction::Write: return "Write";
        case SandboxAction::Copy: return "Copy";
        case SandboxAction::Delete: return "Delete";
        default: return "Unknown";
    }
}

const char* SandboxGuard::DenialReasonToString(SandboxDenialReason reason) {
    switch (reason) {
        case SandboxDenialReason::None: return "None";
        case SandboxDenialReason::EmptyPath: return "EmptyPath";
        case SandboxDenialReason::PathTraversalDetected: return "PathTraversalDetected";
        case SandboxDenialReason::BlacklistedSystemDirectory: return "BlacklistedSystemDirectory";
        case SandboxDenialReason::DriveRootRestricted: return "DriveRootRestricted";
        case SandboxDenialReason::NotWhitelisted: return "NotWhitelisted";
        case SandboxDenialReason::AntiCheatLocked: return "AntiCheatLocked";
        case SandboxDenialReason::IoError: return "IoError";
        default: return "Unknown";
    }
}

} // namespace vegaflow
