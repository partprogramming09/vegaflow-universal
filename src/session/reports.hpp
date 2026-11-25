#pragma once
// Console reports: the session log and the lines --status and --monitor print (the commands themselves: app/report_commands.hpp).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ports/memory_probe.hpp"
#include "session/config.hpp"

namespace vegaflow::app {

// The default session log, and the size past which it is renamed to <name>.1 (replacing the previous one).
inline constexpr const char* kSessionLogDefaultPath = "logs/session_reports.log";
inline constexpr uint64_t kSessionLogMaxBytes = 2ULL * 1024 * 1024;

// Where the log really goes: the default relative path lives next to the .exe (like config.json); with no exe folder it
// stays relative to the working directory. Any other path (absolute or relative) is used as given.
std::filesystem::path ResolveSessionLogPath(const std::string& file_path, const std::wstring& exe_dir);

// Appends one session block. Rotates first if the file is over kSessionLogMaxBytes. A named mutex (Local\VegaFlowLog)
// serializes the GUI and the daemon. Returns false, without throwing, when the log cannot be written.
bool SaveSessionReportToFile(
    const std::string& game_name,
    const std::string& api_name = "",
    uint32_t pid = 0,
    const std::string& file_path = kSessionLogDefaultPath);

// F3.3: appends one "[WARN] ..." line to the session log under the same lock; false, without throwing, if it cannot.
bool LogNoticeToSessionLog(const std::string& notice, const std::string& file_path = kSessionLogDefaultPath);

// F3.4: creates or opens the log lock (a HANDLE as void*; the caller closes it with CloseHandle). Null when it cannot.
// Opens with only SYNCHRONIZE | MUTEX_MODIFY_STATE, so a standard-user GUI can join a lock an elevated daemon made.
void* OpenSessionLogLock();

// The [WHY THIS PROFILE] block of --status: header plus one "  - " line per reason, and an empty string when there are none.
std::string WhyThisProfileSection(const std::vector<std::string>& reasons);

// The [KPI] line: the daemon's private working set against the 15 MB budget, "passed" only when it is under it.
std::string MemoryKpiLine(uint64_t private_ws_bytes);

// The [GPU] line of --status and --monitor: global shared/dedicated GPU usage (PDH), DXGI LOCAL and NON_LOCAL budgets,
// and whether PDH answered (vram.valid; false means the UMA guard decides on RAM alone).
std::string GpuMemoryLine(const VideoMemoryTelemetry& vram);

} // namespace vegaflow::app
