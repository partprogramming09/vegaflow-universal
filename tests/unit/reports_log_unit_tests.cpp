// E2.6: one do-not-trim rule shared with the classifier (vectors), and a session log that rotates at 2 MB, lives next
// to the .exe and survives two writers. Works on temp files only; the named mutex is real (Windows).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "bridge/rust_bridge.h"
#include "session/reports.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

namespace fs = std::filesystem;
using vegaflow::app::kSessionLogMaxBytes;
using vegaflow::app::ResolveSessionLogPath;
using vegaflow::app::SaveSessionReportToFile;

std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& p, uint64_t size, char fill) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    const std::string chunk(1024 * 1024, fill);
    for (uint64_t left = size; left > 0;) {
        const uint64_t n = left < chunk.size() ? left : chunk.size();
        f.write(chunk.data(), static_cast<std::streamsize>(n));
        left -= n;
    }
}

uint64_t SizeOf(const fs::path& p) {
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    return ec ? UINT64_MAX : n;
}

} // namespace

void Test112() {
    std::string failed;
    auto check = [&failed](bool ok, const char* name) {
        if (!ok) failed += std::string(failed.empty() ? "" : ", ") + name;
    };

    // Rows 1-5: the shared vectors through the FFI (the same rule memory_manager.cpp calls).
    int rows = 0;
    bool vectors_ok = true;
    ForEachTsvRow("tests/vectors/memory_protected.tsv", [&](const std::vector<std::string>& c, const std::string&) {
        if (c.size() != 3) { vectors_ok = false; return; }
        std::string name = c[0] == "<empty>" ? "" : (c[0] == "<blank>" ? "   " : c[0]);
        vectors_ok = vectors_ok && (vf_rust_is_factory_memory_protected_exe(name.data(), name.size()) == (c[1] == "1"));
        ++rows;
    });
    check(vectors_ok && rows >= 40, "protection vectors");

    const fs::path dir = fs::temp_directory_path() / "vf_e26_log_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path log = dir / "logs" / "session_reports.log";
    const fs::path log1 = dir / "logs" / "session_reports.log.1";
    const uint64_t kBig = kSessionLogMaxBytes + 1;

    // Row 6: 2 MB + 1 byte is rotated to .log.1; the new file starts with the current line only.
    fs::create_directories(log.parent_path(), ec);
    WriteBytes(log, kBig, 'a');
    const bool saved6 = SaveSessionReportToFile("RotateGame", "DirectX 12", 4242, log.string());
    const std::string fresh = ReadAll(log);
    check(saved6 && SizeOf(log1) == kBig && fresh.find("Session Report: RotateGame") != std::string::npos &&
              fresh.find("aaaa") == std::string::npos && SizeOf(log) < 1024,
          "rotation at 2MB+1");

    // Row 7: an existing .log.1 is replaced, not appended to, so it does not grow.
    WriteBytes(log, kBig, 'b');
    WriteBytes(log1, 10, 'o');
    SaveSessionReportToFile("RotateAgain", "DirectX 11", 1, log.string());
    check(SizeOf(log1) == kBig && ReadAll(log1).compare(0, 4, "bbbb") == 0 && ReadAll(log).find("RotateAgain") != std::string::npos,
          ".log.1 replaced");

    // Edge: exactly 2 MB is not over the limit, so it is not rotated.
    fs::remove(log1, ec);
    WriteBytes(log, kSessionLogMaxBytes, 'c');
    SaveSessionReportToFile("NoRotate", "", 2, log.string());
    check(!fs::exists(log1) && SizeOf(log) > kSessionLogMaxBytes, "exactly 2MB stays");

    // Row 8: a folder that cannot be created (its parent is a file) returns false and does not throw.
    const fs::path blocker = dir / "not_a_folder";
    { std::ofstream(blocker) << "x"; }
    bool threw8 = false, saved8 = true;
    try { saved8 = SaveSessionReportToFile("G", "", 3, (blocker / "logs" / "s.log").string()); } catch (...) { threw8 = true; }
    check(!threw8 && !saved8, "unwritable folder");

    // Row 10: an empty name and a 10 000-character name give bounded lines; a newline in the name cannot add a line.
    const fs::path log10 = dir / "bounded.log";
    SaveSessionReportToFile("", "", 5, log10.string());
    const std::string empty_name = ReadAll(log10);
    fs::remove(log10, ec);
    SaveSessionReportToFile(std::string(10000, 'x'), std::string(10000, 'y'), 6, log10.string());
    const uint64_t huge_size = SizeOf(log10);
    fs::remove(log10, ec);
    SaveSessionReportToFile("evil\nINJECTED LINE\r\n", "", 7, log10.string());
    const std::string evil = ReadAll(log10);
    check(empty_name.find("(unknown)") != std::string::npos && huge_size < 2000 && evil.find("\nINJECTED LINE") == std::string::npos,
          "bounded name");

    // Row 11: the default path goes next to the .exe; with no exe folder the old relative path is kept.
    const std::string def = "logs/session_reports.log";
    check(ResolveSessionLogPath(def, L"C:\\VegaFlow") == fs::path(L"C:\\VegaFlow") / L"logs" / L"session_reports.log", "default next to exe");
    check(ResolveSessionLogPath(def, L"") == fs::path(def), "no exe folder keeps relative path");
    check(ResolveSessionLogPath("build/x.log", L"C:\\VegaFlow") == fs::path("build/x.log"), "custom relative path untouched");
    check(ResolveSessionLogPath("C:\\other\\x.log", L"C:\\VegaFlow") == fs::path("C:\\other\\x.log"), "absolute path untouched");

    // Row 9: two writers (GUI and daemon) never leave a half line: every block is 8 whole lines about one name.
    const fs::path log9 = dir / "shared.log";
    constexpr int kPerThread = 60;
    auto writer = [&](const std::string& tag) {
        for (int i = 0; i < kPerThread; ++i) SaveSessionReportToFile(tag + std::to_string(i), "DirectX 12", static_cast<uint32_t>(i), log9.string());
    };
    std::thread t1(writer, "A"), t2(writer, "B");
    t1.join();
    t2.join();
    const std::string shared = ReadAll(log9);
    std::vector<std::string> lines;
    for (size_t pos = 0; pos < shared.size();) {
        const size_t nl = shared.find('\n', pos);
        lines.push_back(shared.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? shared.size() : nl + 1;
    }
    bool blocks_ok = lines.size() == static_cast<size_t>(2 * kPerThread * 8);
    for (size_t b = 0; blocks_ok && b + 8 <= lines.size(); b += 8) {
        const size_t at = lines[b].find("Session Report: ");
        const std::string name = at == std::string::npos ? "?" : lines[b].substr(at + 16);
        blocks_ok = lines[b][0] == '[' && lines[b + 2] == " VEGAFLOW SESSION REPORT: " + name && lines[b + 7].empty();
    }
    check(blocks_ok, "two writers keep whole blocks");

    // F3.3: a notice lands in the log as one "[WARN]" line, with newlines inside it flattened.
    const fs::path notice_log = dir / "notice.log";
    const bool notice_written = vegaflow::app::LogNoticeToSessionLog("config.json rechazado:\nsegunda línea", notice_log.string());
    const std::string notice_text = ReadAll(notice_log);
    check(notice_written && notice_text.find("] [WARN] config.json rechazado:") != std::string::npos &&
              std::count(notice_text.begin(), notice_text.end(), '\n') == 1,
          "a notice is one WARN line in the log");

    fs::remove_all(dir, ec);
    RecordResult(failed.empty(), "Test 112: one do-not-trim rule and a session log that rotates at 2 MB next to the exe",
                 failed.empty() ? "all rows ok" : "FAILED: " + failed);
}

} // namespace vftest
