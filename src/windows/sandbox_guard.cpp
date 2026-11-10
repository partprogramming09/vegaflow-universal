#include "windows/sandbox_guard.hpp"
#include "bridge/rust_bridge.h"
#include "windows/steam_paths.hpp"
#include "windows/unique_handle.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <mutex>
#include <optional>

namespace vegaflow {

namespace {

std::mutex g_sandbox_mutex;
std::vector<std::wstring> g_custom_allowed_paths;
std::vector<std::wstring> g_read_only_paths;
std::optional<std::vector<std::wstring>> g_steam_libraries; // empty: read from Steam on first use
void (*g_before_open_hook)() = nullptr;
std::vector<SandboxViolationRecord> g_violations;

void BeforeOpen() {
    if (g_before_open_hook) g_before_open_hook();
}

const uint16_t* U16(const std::wstring& s) {
    static_assert(sizeof(wchar_t) == sizeof(uint16_t), "the Rust core reads Windows wchar_t as UTF-16");
    return reinterpret_cast<const uint16_t*>(s.data());
}

std::wstring ToLowerW(std::wstring_view sv) {
    std::wstring result;
    result.reserve(sv.size());
    for (wchar_t c : sv) {
        result.push_back(static_cast<wchar_t>(std::towlower(c)));
    }
    return result;
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

constexpr wchar_t kLongPrefix[] = L"\\\\?\\";

// "\\?\X:\..." names the same file as "X:\...". Any other path starting with two separators (UNC, "\\.\", other
// "\\?\" forms) is returned unchanged so the decision refuses it as not local.
bool StripLongPrefix(std::wstring& path) {
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        if (path.size() >= 7 && path.compare(0, 4, kLongPrefix) == 0 && path[5] == L':' && path[6] == L'\\') {
            path.erase(0, 4);
            return true;
        }
        return false;
    }
    return true;
}

// The real path of an open file, in the form CanonicalizePath gives.
std::wstring HandlePath(HANDLE h) {
    std::wstring buf(1024, L'\0');
    DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n >= buf.size()) {
        buf.resize(n + 1);
        n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    if (n == 0 || n >= buf.size()) return L"";
    buf.resize(n);
    StripLongPrefix(buf); // a "\\?\UNC\" result stays as it is and is refused as not local
    return buf;
}

// The real path of an existing file or folder, with junctions, symlinks and 8.3 names resolved by Windows.
std::wstring FinalPathOf(const std::wstring& existing) {
    UniqueHandle h(CreateFileW(existing.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    return h ? HandlePath(h.get()) : L"";
}

} // namespace

std::wstring SandboxGuard::CanonicalizePath(const std::wstring& path) {
    if (path.empty()) return L"";

    std::wstring cleaned = path;
    if (cleaned.size() >= 2 && cleaned.front() == L'"' && cleaned.back() == L'"') {
        cleaned = cleaned.substr(1, cleaned.size() - 2);
    }
    std::replace(cleaned.begin(), cleaned.end(), L'/', L'\\');
    if (!StripLongPrefix(cleaned)) return cleaned;

    // Resolves "." and ".." and makes the path absolute.
    wchar_t full[MAX_PATH * 4] = {0};
    const DWORD ret = GetFullPathNameW(cleaned.c_str(), _countof(full), full, nullptr);
    if (ret == 0 || ret >= _countof(full)) return L"";
    std::filesystem::path p = std::filesystem::path(full).lexically_normal();

    // The deepest part that exists is replaced by its real path; the rest (not created yet) is appended as written.
    std::filesystem::path existing = p;
    std::filesystem::path rest;
    std::error_code ec;
    while (!existing.empty() && !std::filesystem::exists(existing, ec)) {
        if (existing == existing.root_path()) break;
        rest = existing.filename() / rest;
        existing = existing.parent_path();
    }
    std::wstring res = p.wstring();
    if (!existing.empty() && std::filesystem::exists(existing, ec)) {
        const std::wstring real = FinalPathOf(existing.wstring());
        if (real.empty()) return L"";
        res = rest.empty() ? real : (std::filesystem::path(real) / rest).wstring();
    }
    std::replace(res.begin(), res.end(), L'/', L'\\');

    if (res.size() >= 2 && res[1] == L':' && std::iswlower(res[0])) {
        res[0] = static_cast<wchar_t>(std::towupper(res[0]));
    }
    if (res.size() > 3 && res.back() == L'\\') {
        res.pop_back();
    }
    return res;
}

namespace {

// The verdict codes are a contract with rust/vegaflow-core/src/policy/path_policy.rs: a reordered enum would map a
// verdict to the wrong rule and message without any other error.
static_assert(VF_PATH_ALLOWED == 0 && VF_PATH_EMPTY == 1 && VF_PATH_DRIVE_ROOT == 2 && VF_PATH_CRITICAL_FILE == 3 &&
              VF_PATH_WINDOWS_DIR == 4 && VF_PATH_WINDOWS_OTHER_DRIVE == 5 && VF_PATH_COMMON_FILES == 6 &&
              VF_PATH_SYSTEM_VOLUME_INFORMATION == 7 && VF_PATH_INTERNAL_ERROR == 8 && VF_PATH_ANTICHEAT == 9 &&
              VF_PATH_NOT_WHITELISTED == 10 && VF_PATH_NOT_LOCAL_PATH == 11,
              "VfPathVerdict must match path_policy.rs");

const char* BlacklistReason(uint32_t verdict) {
    switch (verdict) {
        case VF_PATH_EMPTY:
            return "Ruta vacía";
        case VF_PATH_DRIVE_ROOT:
            return "Operación denegada: Prohibido escribir en la raíz del disco o sobre archivos raíz directos";
        case VF_PATH_CRITICAL_FILE:
            return "Operación denegada: Intento de modificar archivos críticos de arranque de Windows";
        case VF_PATH_WINDOWS_DIR:
            return "Operación denegada: Acceso a directorios del sistema de Windows bloqueado por seguridad";
        case VF_PATH_WINDOWS_OTHER_DRIVE:
            return "Operación denegada: Acceso a carpeta de Windows bloqueado";
        case VF_PATH_COMMON_FILES:
            return "Operación denegada: Modificación de Common Files de Program Files bloqueada";
        case VF_PATH_SYSTEM_VOLUME_INFORMATION:
            return "Operación denegada: System Volume Information protegido";
        default:
            return "Operación denegada";
    }
}

struct SystemDirs {
    std::wstring win, sys;
};

// The Windows and System folders, lowercased; they do not change while the process runs.
const SystemDirs& LoweredSystemDirs() {
    static const SystemDirs dirs{ToLowerW(GetWindowsDirectoryPath()), ToLowerW(GetSystemDirectoryPath())};
    return dirs;
}

} // namespace

// The rules live in the Rust core (path_policy.rs). Case folding stays here with towlower, as before, so the
// verdict cannot differ from the old C++ code for non-ASCII paths.
bool SandboxGuard::IsBlacklisted(const std::wstring& canonical_path, std::string* out_reason) {
    const std::wstring lower = ToLowerW(canonical_path);
    const SystemDirs& dirs = LoweredSystemDirs();
    const uint32_t verdict = vf_rust_classify_blacklisted_path(U16(lower), lower.size(), U16(dirs.win), dirs.win.size(),
                                                               U16(dirs.sys), dirs.sys.size());

    if (verdict == VF_PATH_ALLOWED) {
        return false;
    }
    if (out_reason) *out_reason = BlacklistReason(verdict);
    return true;
}

namespace {

struct Root {
    std::wstring lower;
    bool writable;
};

// True if the folder or one above it is a junction or a symbolic link (not, e.g., a OneDrive placeholder). Such a
// folder can be pointed anywhere by the user who owns it, so it must not become an allowed folder.
bool UnderNameSurrogate(const std::wstring& path) {
    wchar_t full[MAX_PATH * 4] = {0};
    const DWORD ret = GetFullPathNameW(path.c_str(), _countof(full), full, nullptr);
    if (ret == 0 || ret >= _countof(full)) return true;
    for (std::filesystem::path p = full; p.has_relative_path(); p = p.parent_path()) {
        WIN32_FIND_DATAW data{};
        UniqueFindHandle find(FindFirstFileW(p.c_str(), &data));
        if (find && (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(data.dwReserved0)) {
            return true;
        }
    }
    return false;
}

void AddRoot(std::vector<Root>& roots, const std::wstring& path, bool writable) {
    if (path.empty() || UnderNameSurrogate(path)) return;
    std::wstring canonical = SandboxGuard::CanonicalizePath(path);
    if (!canonical.empty()) roots.push_back({ToLowerW(canonical), writable});
}

const std::vector<std::wstring>& SteamLibrariesLocked() {
    if (!g_steam_libraries) g_steam_libraries = ReadSteamLibraryFolders(FindSteamInstallPath());
    return *g_steam_libraries;
}

// The folders VegaFlow may touch: its own folder, LOCALAPPDATA, temp, My Games and Saved Games, the steamapps\common
// folder of each Steam library, and the folders registered at run time.
std::vector<Root> CollectAllowedRoots() {
    std::vector<Root> roots;
    AddRoot(roots, GetVegaFlowExecutableDirectory(), true);
    AddRoot(roots, GetEnvVarW(L"LOCALAPPDATA"), true);
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (!ec) AddRoot(roots, temp.wstring(), true);
    const std::wstring profile = GetEnvVarW(L"USERPROFILE");
    if (!profile.empty()) {
        AddRoot(roots, profile + L"\\Documents\\My Games", true);
        AddRoot(roots, profile + L"\\Saved Games", true);
    }
    std::vector<std::wstring> libraries, custom, read_only;
    {
        std::lock_guard<std::mutex> lock(g_sandbox_mutex);
        libraries = SteamLibrariesLocked();
        custom = g_custom_allowed_paths;
        read_only = g_read_only_paths;
    }
    for (const auto& lib : libraries) AddRoot(roots, lib + L"\\steamapps\\common", true);
    for (const auto& p : custom) AddRoot(roots, p, true);
    for (const auto& p : read_only) AddRoot(roots, p, false);
    return roots;
}

uint32_t Decide(const std::wstring& canonical, bool write, bool anticheat, const std::vector<Root>& roots) {
    const std::wstring lower = ToLowerW(canonical);
    const SystemDirs& dirs = LoweredSystemDirs();
    std::vector<VfAllowedRoot> raw;
    raw.reserve(roots.size());
    for (const auto& r : roots) raw.push_back({U16(r.lower), r.lower.size(), r.writable});
    return vf_rust_decide_file_access(U16(lower), lower.size(), U16(dirs.win), dirs.win.size(), U16(dirs.sys),
                                      dirs.sys.size(), write, anticheat, raw.data(), raw.size());
}

} // namespace

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

    // Scan the folder and up to 4 levels upward (without going past the root or the project boundary)
    int levels_checked = 0;
    while (!current.empty() && levels_checked < 5) {
        if (!std::filesystem::exists(current, ec)) {
            if (!project_root.empty() && current == project_root) break;
            current = current.parent_path();
            levels_checked++;
            continue;
        }

        // If the path itself contains the signature
        if (PathContainsAntiCheatSignature(current.wstring())) {
            return true;
        }

        // Inspect the files and subdirectories present in the folder
        for (const auto& entry : std::filesystem::directory_iterator(
                 current, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;

            std::wstring filename = entry.path().filename().wstring();
            if (PathContainsAntiCheatSignature(filename)) {
                return true;
            }
        }

        // Stop when we reach the project root or the drive root
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

    // A write probe: ValidatePath also scans this folder and the ones above it for anti-cheat files.
    return ValidatePath((dir / L"dxvk.conf").wstring(), SandboxAction::Write, false).allowed;
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

    std::wstring canonical = CanonicalizePath(path);
    result.canonical_path = canonical;
    if (canonical.empty()) {
        result.reason = SandboxDenialReason::PathTraversalDetected;
        result.message = "Fallo en la resolución canónica de la ruta";
        RecordViolation(path, action, result.reason, result.message);
        return result;
    }

    // The anti-cheat scan reads the disk, so it only runs for a write that every other rule allows.
    const bool write = (action != SandboxAction::Read);
    const std::vector<Root> roots = CollectAllowedRoots();
    uint32_t verdict = Decide(canonical, write, false, roots);
    bool detected = false;
    if (verdict == VF_PATH_ALLOWED && write) {
        detected = !is_anticheat_protected &&
                   (PathContainsAntiCheatSignature(canonical) ||
                    DirectoryContainsAntiCheat(std::filesystem::path(canonical).parent_path()));
        if (is_anticheat_protected || detected) verdict = Decide(canonical, write, true, roots);
    }

    switch (verdict) {
        case VF_PATH_ALLOWED:
            result.allowed = true;
            result.reason = SandboxDenialReason::None;
            result.message = "Operación autorizada dentro de los límites del sandbox";
            return result;
        case VF_PATH_EMPTY:
            result.reason = SandboxDenialReason::EmptyPath;
            result.message = "Ruta vacía o no especificada";
            break;
        case VF_PATH_ANTICHEAT:
            result.reason = SandboxDenialReason::AntiCheatLocked;
            result.message = detected
                ? "Firma de Anti-Cheat detectada en el directorio: Escritura abortada para salvaguardar la cuenta del usuario"
                : "Bloqueo Anti-Cheat activo: Prohibida toda modificación en juegos con protección a nivel de kernel";
            break;
        case VF_PATH_NOT_WHITELISTED:
            result.reason = SandboxDenialReason::NotWhitelisted;
            result.message = "La ruta se encuentra fuera de los límites autorizados por VegaFlow Sandbox";
            break;
        case VF_PATH_NOT_LOCAL_PATH:
            result.reason = SandboxDenialReason::PathTraversalDetected;
            result.message = "Ruta no local: prefijo de dispositivo o de red, segmentos '.' o '..', o flujo ':'";
            break;
        default:
            result.reason = SandboxDenialReason::BlacklistedSystemDirectory;
            result.message = BlacklistReason(verdict);
            break;
    }
    RecordViolation(canonical, action, result.reason, result.message);
    return result;
}

namespace {

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

bool IsDirectoryHandle(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION info{};
    return !GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool ReadAll(HANDLE h, std::string& out) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > (1LL << 30)) return false;
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    size_t done = 0;
    while (done < data.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 20));
        if (!ReadFile(h, data.data() + done, want, &got, nullptr) || got == 0) return false;
        done += got;
    }
    out = std::move(data);
    return true;
}

bool ReplaceContent(HANDLE h, const std::string& data) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN) || !SetEndOfFile(h)) return false;
    size_t done = 0;
    while (done < data.size()) {
        DWORD put = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 20));
        if (!WriteFile(h, data.data() + done, want, &put, nullptr) || put == 0) return false;
        done += put;
    }
    return true;
}

bool DeleteByHandle(HANDLE h) {
    FILE_DISPOSITION_INFO info{};
    info.DeleteFile = TRUE;
    return SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info)) != 0;
}

bool CreateParentDirectories(const std::filesystem::path& p) {
    std::error_code ec;
    if (p.parent_path().empty() || std::filesystem::exists(p.parent_path(), ec)) return true;
    std::filesystem::create_directories(p.parent_path(), ec);
    return !ec;
}

} // namespace

// The real path of an opened file is checked again only when it differs from the path validated before opening;
// that difference is what a folder swapped for a junction in between produces.
bool SandboxGuard::OpenedFileAllowed(HANDLE h, const std::wstring& validated, SandboxAction action,
                                     bool is_anticheat_protected) {
    const std::wstring real = HandlePath(h);
    if (real.empty()) return false;
    return _wcsicmp(real.c_str(), validated.c_str()) == 0 || ValidatePath(real, action, is_anticheat_protected).allowed;
}

// The checks run on the path; the file is then opened and its real path checked again, so a folder swapped for a
// junction in between is caught. Content is only changed after that second check (no truncation on open).
bool SandboxGuard::WriteVerified(const std::wstring& canonical, const std::string& content, SandboxAction action,
                                 bool is_anticheat_protected, bool overwrite) {
    std::filesystem::path p(canonical);
    if (!CreateParentDirectories(p)) {
        RecordViolation(canonical, action, SandboxDenialReason::IoError, "No se pudo crear el directorio de destino");
        return false;
    }
    UniqueHandle h(CreateFileW(p.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ, nullptr,
                               overwrite ? OPEN_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    const bool created = (GetLastError() != ERROR_ALREADY_EXISTS);
    if (!h) {
        RecordViolation(canonical, action, SandboxDenialReason::IoError, "No se pudo abrir el archivo para escritura");
        return false;
    }
    if (!OpenedFileAllowed(h.get(), canonical, action, is_anticheat_protected)) {
        if (created) DeleteByHandle(h.get());
        return false;
    }
    if (!ReplaceContent(h.get(), content)) {
        RecordViolation(canonical, action, SandboxDenialReason::IoError, "Error durante la escritura del archivo en disco");
        return false;
    }
    return true;
}

bool SandboxGuard::ReadVerified(const std::wstring& canonical, std::string& out_content) {
    UniqueHandle h(CreateFileW(canonical.c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h || IsDirectoryHandle(h.get())) return false;
    if (!OpenedFileAllowed(h.get(), canonical, SandboxAction::Read, false)) return false;
    return ReadAll(h.get(), out_content);
}

bool SandboxGuard::SafeWriteFile(
    const std::wstring& path,
    const std::string& content,
    bool is_anticheat_protected) {

    auto val = ValidatePath(path, SandboxAction::Write, is_anticheat_protected);
    if (!val.allowed) {
        return false;
    }
    BeforeOpen();
    return WriteVerified(val.canonical_path, content, SandboxAction::Write, is_anticheat_protected, true);
}

bool SandboxGuard::SafeCopyFile(
    const std::wstring& src,
    const std::wstring& dst,
    bool overwrite,
    bool is_anticheat_protected) {

    auto valSrc = ValidatePath(src, SandboxAction::Read, false);
    if (!valSrc.allowed) {
        return false;
    }
    auto valDst = ValidatePath(dst, SandboxAction::Copy, is_anticheat_protected);
    if (!valDst.allowed) {
        return false;
    }
    BeforeOpen();

    std::string content;
    if (!ReadVerified(valSrc.canonical_path, content)) {
        RecordViolation(valSrc.canonical_path, SandboxAction::Copy, SandboxDenialReason::IoError,
                        "No se pudo leer el archivo de origen");
        return false;
    }
    return WriteVerified(valDst.canonical_path, content, SandboxAction::Copy, is_anticheat_protected, overwrite);
}

bool SandboxGuard::SafeRemoveFile(
    const std::wstring& path,
    bool is_anticheat_protected) {

    auto val = ValidatePath(path, SandboxAction::Delete, is_anticheat_protected);
    if (!val.allowed) {
        return false;
    }
    BeforeOpen();

    // FILE_FLAG_OPEN_REPARSE_POINT: a link is removed itself, never the file it points to.
    UniqueHandle h(CreateFileW(val.canonical_path.c_str(), DELETE, kShareAll, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!h) {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return true; // Already gone
        RecordViolation(val.canonical_path, SandboxAction::Delete, SandboxDenialReason::IoError, "No se pudo abrir el archivo para eliminarlo");
        return false;
    }
    if (IsDirectoryHandle(h.get())) return false;
    if (!OpenedFileAllowed(h.get(), val.canonical_path, SandboxAction::Delete, is_anticheat_protected)) return false;
    if (!DeleteByHandle(h.get())) {
        RecordViolation(val.canonical_path, SandboxAction::Delete, SandboxDenialReason::IoError, "No se pudo eliminar el archivo");
        return false;
    }
    return true;
}

bool SandboxGuard::SafeReadFile(const std::wstring& path, std::string& out_content) {
    auto val = ValidatePath(path, SandboxAction::Read, false);
    if (!val.allowed) {
        return false;
    }
    BeforeOpen();
    return ReadVerified(val.canonical_path, out_content);
}

namespace {

void AddUniqueCanonical(std::vector<std::wstring>& list, const std::wstring& path) {
    if (path.empty()) return;
    std::wstring canonical = SandboxGuard::CanonicalizePath(path);
    if (canonical.empty()) return;

    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    for (const auto& existing : list) {
        if (_wcsicmp(existing.c_str(), canonical.c_str()) == 0) return;
    }
    list.push_back(std::move(canonical));
}

} // namespace

void SandboxGuard::AddAllowedPath(const std::wstring& path) {
    AddUniqueCanonical(g_custom_allowed_paths, path);
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

void SandboxGuard::AddReadOnlyPath(const std::wstring& path) {
    AddUniqueCanonical(g_read_only_paths, path);
}

void SandboxGuard::SetSteamLibraryFolders(std::vector<std::wstring> folders) {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_steam_libraries = std::move(folders);
}

void SandboxGuard::SetBeforeOpenHookForTesting(void (*hook)()) {
    g_before_open_hook = hook;
}

void SandboxGuard::ReloadSteamLibraryFolders() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_steam_libraries.reset();
}

void SandboxGuard::ClearCustomAllowedPaths() {
    std::lock_guard<std::mutex> lock(g_sandbox_mutex);
    g_custom_allowed_paths.clear();
    g_read_only_paths.clear();
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
