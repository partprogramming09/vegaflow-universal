#include "session/reports.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "ports/config_location.hpp"
#include "ports/text_convert.hpp"

namespace vegaflow::app {

namespace {

// A name that goes into the log: one line, at most 200 bytes (never cut inside a UTF-8 character), "(unknown)" if empty.
std::string LogField(const std::string& raw, const char* when_empty) {
    constexpr size_t kMaxFieldBytes = 200;
    std::string s = raw.substr(0, kMaxFieldBytes);
    if (raw.size() > kMaxFieldBytes) {
        while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (!s.empty() && static_cast<unsigned char>(s.back()) >= 0xC0) s.pop_back();
    }
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return s.empty() ? when_empty : s;
}

// Closes a HANDLE on every path.
struct HandleCloser {
    HANDLE h;
    ~HandleCloser() {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};

// F3.4: the lock is created by whoever writes first, often the elevated daemon; the default DACL would then shut out the
// GUI. SYSTEM and Administrators get everything; authenticated users get only SYNCHRONIZE | MUTEX_MODIFY_STATE (Local is per logon session; a job run without the Interactive SID must still join).
HANDLE CreateSessionLogLock() {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, FALSE};
    const bool dacl = ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x100001;;;AU)",
                                                                           SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr);
    // shortcut: if the descriptor cannot be built the default DACL is used (the old behaviour), not a failed log.
    // CreateMutexW asks for MUTEX_ALL_ACCESS when the mutex already exists (and ignores the DACL), which a standard user
    // cannot have on the daemon's lock; asking only for what is needed lets it open.
    HANDLE h = CreateMutexExW(dacl ? &sa : nullptr, L"Local\\VegaFlowLog", 0, SYNCHRONIZE | MUTEX_MODIFY_STATE);
    if (dacl) LocalFree(sa.lpSecurityDescriptor);
    return h;
}

// Renames an oversized log to <name>.1 (replacing the old one). Caller holds the log mutex.
void RotateIfTooBig(const std::filesystem::path& p) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &info)) return;
    const uint64_t size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (size > kSessionLogMaxBytes) {
        MoveFileExW(p.c_str(), (p.wstring() + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

} // namespace

std::filesystem::path ResolveSessionLogPath(const std::string& file_path, const std::wstring& exe_dir) {
    if (file_path != kSessionLogDefaultPath || exe_dir.empty()) return std::filesystem::path(file_path);
    return std::filesystem::path(exe_dir) / L"logs" / L"session_reports.log";
}

// One lock covers the size check, the rotation and the write, so the GUI and the daemon never interleave.
static bool AppendToSessionLog(const std::string& text, const std::filesystem::path& p) {
    try {
        HandleCloser lock{CreateSessionLogLock()};
        if (!lock.h) return false;
        const DWORD waited = WaitForSingleObject(lock.h, 5000);
        if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) return false;
        struct Release {
            HANDLE h;
            ~Release() { ReleaseMutex(h); }
        } release{lock.h};

        RotateIfTooBig(p);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }
        // Open for append only and close at once, with full sharing, so a rotation by another process can rename it.
        HandleCloser file{CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.h == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        return WriteFile(file.h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    } catch (...) {
        return false;
    }
}

bool SaveSessionReportToFile(
    const std::string& game_name,
    const std::string& api_name,
    uint32_t pid,
    const std::string& file_path) {
    try {
        const std::wstring exe_dir = ExecutableDirectory();
        if (exe_dir.empty() && file_path == kSessionLogDefaultPath) {
            static std::atomic_flag warned = ATOMIC_FLAG_INIT;
            if (!warned.test_and_set()) {
                std::cerr << "[WARN] Could not find the .exe folder; the session log goes to the working directory.\n";
            }
        }
        const std::filesystem::path p = ResolveSessionLogPath(file_path, exe_dir);

        const std::time_t now = std::time(nullptr);
        std::tm tm_local{};
        localtime_s(&tm_local, &now);
        char time_buf[64] = {0};
        std::strftime(time_buf, sizeof(time_buf), "[%Y-%m-%d %H:%M:%S] ", &tm_local);
        const std::string game = LogField(game_name, "(unknown)");
        const std::string api = LogField(api_name, "N/A");

        std::ostringstream block;
        block << time_buf << "Session Report: " << game << "\n"
              << "========================================================\n"
              << " VEGAFLOW SESSION REPORT: " << game << "\n"
              << "========================================================\n"
              << " Target PID            : " << pid << "\n"
              << " Render API            : " << api << "\n"
              << "========================================================\n\n";
        const std::string text = block.str();

        return AppendToSessionLog(text, p);
    } catch (...) {
        return false;
    }
}

bool LogNoticeToSessionLog(const std::string& notice, const std::string& file_path) {
    try {
        const std::filesystem::path p = ResolveSessionLogPath(file_path, ExecutableDirectory());
        const std::time_t now = std::time(nullptr);
        std::tm tm_local{};
        localtime_s(&tm_local, &now);
        char time_buf[64] = {0};
        std::strftime(time_buf, sizeof(time_buf), "[%Y-%m-%d %H:%M:%S] ", &tm_local);
        return AppendToSessionLog(time_buf + std::string("[WARN] ") + LogField(notice, "(empty)") + "\n", p);
    } catch (...) {
        return false;
    }
}

void* OpenSessionLogLock() { return CreateSessionLogLock(); }


std::string GpuMemoryLine(const VideoMemoryTelemetry& vram) {
    constexpr uint64_t kMb = 1024ULL * 1024ULL;
    std::ostringstream ss;
    ss << "[GPU] Shared: " << vram.gpu_shared_usage_bytes / kMb << " MB | Dedicated: " << vram.gpu_dedicated_usage_bytes / kMb
       << " MB | DXGI Budget Local: " << vram.local_budget_bytes / kMb << " MB / Non-Local: "
       << vram.non_local_budget_bytes / kMb << " MB | PDH: " << (vram.valid ? "OK" : "UNAVAILABLE (RAM-only guard)");
    return ss.str();
}

std::string WhyThisProfileSection(const std::vector<std::string>& reasons) {
    if (reasons.empty()) return "";
    std::string out = "[WHY THIS PROFILE]\n";
    for (const std::string& line : reasons) out += "  - " + line + "\n";
    return out;
}

std::string MemoryKpiLine(uint64_t private_ws_bytes) {
    std::ostringstream ss;
    constexpr uint64_t kBudgetBytes = 15ULL * 1024 * 1024;
    ss << "[KPI] Daemon Memory Working Set: " << (static_cast<double>(private_ws_bytes) / (1024.0 * 1024.0)) << " MB "
       << (private_ws_bytes < kBudgetBytes ? "(< 15 MB KPI passed)" : "(>= 15 MB KPI FAILED)");
    return ss.str();
}

} // namespace vegaflow::app
