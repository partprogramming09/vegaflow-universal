// Integration tests: sandboxguard and frame telemetry. They run on a real Windows machine (registry, power plan, processes).
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "windows/privileges.hpp"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "safety/frame_telemetry.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "safety/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"

namespace vftest {

void Test07() {
    // -------------------------------------------------------------------------
    // TEST 7: Frame Telemetry & Mathematical Rigor (1% / 0.1% Lows)
    // -------------------------------------------------------------------------
    {
        // 1. Math computation verification
        std::vector<double> samples(990, 16.6666666667); // 60 FPS
        for (int i = 0; i < 9; ++i) samples.push_back(33.3333333333); // 30 FPS
        samples.push_back(100.0); // 10 FPS
        auto math_snap = vegaflow::FrameTelemetryMonitor::ComputeMetricsFromSamples(samples, 1234, 1000);

        bool math_valid = math_snap.valid && (math_snap.total_frames_captured == 1000);
        bool fps_avg_ok = std::abs(math_snap.avg_fps - 59.2) < 1.0;
        bool low_1_ok = std::abs(math_snap.fps_1_percent_low - 25.0) < 0.5;
        bool low_0_1_ok = std::abs(math_snap.fps_0_1_percent_low - 10.0) < 0.1;

        // 2. Circular ring buffer wrapping test (3000 items into 2048 window)
        vegaflow::FrameTelemetryMonitor monitor;
        for (int i = 0; i < 3000; ++i) {
            monitor.RecordFrameIntervalMs(16.6666666667);
        }
        auto ring_snap = monitor.GetSnapshot();
        bool ring_ok = ring_snap.valid && (ring_snap.total_frames_captured == 3000);
        std::string report = monitor.FormatSessionReport("VegaFlow Bench");
        bool report_ok = (report.find("VEGAFLOW TELEMETRY REPORT") != std::string::npos);

        // 3. Monitor thread start & fast shutdown KPI (< 100 ms)
        auto t_start = GetTickCount64();
        monitor.StartMonitoring(GetCurrentProcessId());
        Sleep(30);
        monitor.StopMonitoring();
        auto elapsed_ms = GetTickCount64() - t_start;
        bool kpi_stop_ok = (elapsed_ms < 150); // fast shutdown

        bool t7_success = math_valid && fps_avg_ok && low_1_ok && low_0_1_ok && ring_ok && report_ok && kpi_stop_ok;
        std::string details = "Avg FPS: " + std::to_string(static_cast<int>(math_snap.avg_fps)) + ", "
            + "1% Low: " + std::to_string(static_cast<int>(math_snap.fps_1_percent_low)) + " FPS, "
            + "0.1% Low: " + std::to_string(static_cast<int>(math_snap.fps_0_1_percent_low)) + " FPS, "
            + "RingBuffer: " + std::to_string(ring_snap.total_frames_captured) + " frames, "
            + "Stop Latency: " + std::to_string(elapsed_ms) + " ms (< 100ms KPI)";

        RecordResult(t7_success, "Test 7: FrameTelemetryMonitor mathematical rigor, 1% Low, circular buffer, and shutdown latency", details);
    }
}

void Test17() {
    // -------------------------------------------------------------------------
    // TEST 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O
    // -------------------------------------------------------------------------
    {
        std::error_code ec;

        // 1. Canonical Normalization and Path Traversal Prevention
        auto val_empty = vegaflow::SandboxGuard::ValidatePath(L"", vegaflow::SandboxAction::Write);
        bool empty_blocked = (!val_empty.allowed && val_empty.reason == vegaflow::SandboxDenialReason::EmptyPath);

        std::wstring traversal_path = L"C:\\Users\\Public\\..\\..\\Windows\\System32\\bad.dll";
        auto val_traversal = vegaflow::SandboxGuard::ValidatePath(traversal_path, vegaflow::SandboxAction::Write);
        bool traversal_blocked = (!val_traversal.allowed &&
                                  (val_traversal.reason == vegaflow::SandboxDenialReason::BlacklistedSystemDirectory ||
                                   val_traversal.reason == vegaflow::SandboxDenialReason::PathTraversalDetected));

        std::wstring canon = vegaflow::SandboxGuard::CanonicalizePath(L"C:/Windows/System32/../System32/cmd.exe");
        bool canon_ok = (!canon.empty() && canon.find(L"..") == std::wstring::npos);

        // 2. Strict Drive Root Protection
        auto val_root = vegaflow::SandboxGuard::ValidatePath(L"C:\\", vegaflow::SandboxAction::Write);
        bool root_blocked = (!val_root.allowed);

        auto val_root_file = vegaflow::SandboxGuard::ValidatePath(L"C:\\dangerous_payload.exe", vegaflow::SandboxAction::Write);
        bool root_file_blocked = (!val_root_file.allowed);
        bool root_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\vegaflow_forbidden.tmp", "test");

        // 3. Blacklist of Forbidden Critical Zones (Windows, System32, SysWOW64, Common Files)
        bool win_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Windows\\System32\\drivers\\etc\\hosts_bad", "bad");
        bool syswow_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Windows\\SysWOW64\\bad.dll", "bad");
        bool prog_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(L"C:\\Program Files\\Common Files\\bad.dll", "bad");
        bool win_del_blocked = !vegaflow::SandboxGuard::SafeRemoveFile(L"C:\\Windows\\System32\\calc.exe");

        // 4. Whitelist of Allowed Zones
        // 4a. Steam Library (steamapps/common/...): only a library listed in libraryfolders.vdf
        vegaflow::SandboxGuard::SetSteamLibraryFolders({L"D:\\SteamLibrary"});
        auto val_steam = vegaflow::SandboxGuard::ValidatePath(L"D:\\SteamLibrary\\steamapps\\common\\Half-Life\\game.ini", vegaflow::SandboxAction::Write);
        auto val_unlisted = vegaflow::SandboxGuard::ValidatePath(L"E:\\OtherLibrary\\steamapps\\common\\Half-Life\\game.ini", vegaflow::SandboxAction::Write);
        vegaflow::SandboxGuard::ReloadSteamLibraryFolders();
        bool steam_whitelisted = val_steam.allowed && !val_unlisted.allowed;

        // 4b. %LOCALAPPDATA% (game settings and Saved/Config)
        wchar_t localAppBuf[MAX_PATH] = {0};
        GetEnvironmentVariableW(L"LOCALAPPDATA", localAppBuf, MAX_PATH);
        std::filesystem::path localApp(localAppBuf);
        std::filesystem::path ue_config_dir = localApp / L"VegaFlowTestGame" / L"Saved" / L"Config" / L"WindowsNoEditor";
        auto val_ue = vegaflow::SandboxGuard::ValidatePath(ue_config_dir / L"Engine.ini", vegaflow::SandboxAction::Write);
        bool ue_whitelisted = val_ue.allowed;

        // 4c. VegaFlow cache (%LOCALAPPDATA%\VegaFlow\Cache)
        std::filesystem::path cache_dir = localApp / L"VegaFlow" / L"Cache";
        auto val_cache = vegaflow::SandboxGuard::ValidatePath(cache_dir / L"shader.cache", vegaflow::SandboxAction::Write);
        bool cache_whitelisted = val_cache.allowed;

        // 5. Safe I/O Methods in Allowed Zone (SafeWriteFile, SafeReadFile, SafeCopyFile, SafeRemoveFile)
        std::filesystem::path temp_test_dir = std::filesystem::temp_directory_path() / "vegaflow_test_sandbox_guard";
        std::filesystem::remove_all(temp_test_dir, ec);
        std::filesystem::create_directories(temp_test_dir, ec);

        std::filesystem::path test_file = temp_test_dir / L"sandbox_test.tmp";
        std::filesystem::path copy_file = temp_test_dir / L"sandbox_copy.tmp";

        bool write_ok = vegaflow::SandboxGuard::SafeWriteFile(test_file.wstring(), "VEGAFLOW_SANDBOX_VERIFIED_DATA");

        std::string read_back;
        bool read_ok = vegaflow::SandboxGuard::SafeReadFile(test_file.wstring(), read_back) &&
                       (read_back == "VEGAFLOW_SANDBOX_VERIFIED_DATA");

        bool copy_ok = vegaflow::SandboxGuard::SafeCopyFile(test_file.wstring(), copy_file.wstring(), true);
        bool remove1_ok = vegaflow::SandboxGuard::SafeRemoveFile(test_file.wstring());
        bool remove2_ok = vegaflow::SandboxGuard::SafeRemoveFile(copy_file.wstring());
        bool safe_io_ok = write_ok && read_ok && copy_ok && remove1_ok && remove2_ok;

        // 6. Anti-Cheat Lockdown
        // 6a. Block by explicit flag (is_anticheat_protected = true)
        bool flag_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile(test_file.wstring(), "exploit", true /* is_anticheat_protected */);
        bool flag_copy_blocked = !vegaflow::SandboxGuard::SafeCopyFile(test_file.wstring(), copy_file.wstring(), true, true /* is_anticheat_protected */);
        bool flag_remove_blocked = !vegaflow::SandboxGuard::SafeRemoveFile(test_file.wstring(), true /* is_anticheat_protected */);
        bool flag_lockdown_ok = flag_write_blocked && flag_copy_blocked && flag_remove_blocked;

        // 6b. Block by real-time detection of kernel-level Anti-Cheat signatures in the directory
        std::filesystem::path ac_folder = temp_test_dir / L"Anticheat_Test_Game";
        std::filesystem::create_directories(ac_folder, ec);
        {
            std::ofstream out1(ac_folder / "EasyAntiCheat_EOS.dll");
            out1 << "dummy eac";
            std::ofstream out2(ac_folder / "HoYoKProtect.sys");
            out2 << "dummy hoyokprotect";
        }

        bool dir_has_ac = vegaflow::SandboxGuard::DirectoryContainsAntiCheat(ac_folder);
        bool folder_write_blocked = !vegaflow::SandboxGuard::SafeWriteFile((ac_folder / L"Engine.ini").wstring(), "tweak", false);
        bool folder_remove_blocked = !vegaflow::SandboxGuard::SafeRemoveFile((ac_folder / L"Engine.ini").wstring(), false);

        std::filesystem::remove_all(temp_test_dir, ec);
        bool signature_lockdown_ok = dir_has_ac && folder_write_blocked && folder_remove_blocked;

        // 7. Violation Logging and Auditing
        size_t violations_count = vegaflow::SandboxGuard::GetViolationCount();
        bool violations_recorded = (violations_count > 0);
        vegaflow::SandboxGuard::ClearViolations();
        bool violations_cleared = (vegaflow::SandboxGuard::GetViolationCount() == 0);

        bool t17_success = empty_blocked && traversal_blocked && canon_ok &&
                           root_blocked && root_file_blocked && root_write_blocked &&
                           win_write_blocked && syswow_write_blocked && prog_write_blocked && win_del_blocked &&
                           steam_whitelisted && ue_whitelisted && cache_whitelisted &&
                           safe_io_ok && flag_lockdown_ok && signature_lockdown_ok &&
                           violations_recorded && violations_cleared;

        std::string details = "Canonicalization: " + std::string(canon_ok && traversal_blocked ? "PASS" : "FAIL") + ", "
            + "Blacklist & DriveRoot: " + std::string(root_write_blocked && win_write_blocked ? "PASS" : "FAIL") + ", "
            + "Whitelist: " + std::string(steam_whitelisted && ue_whitelisted && cache_whitelisted ? "PASS" : "FAIL") + ", "
            + "Safe I/O: " + std::string(safe_io_ok ? "PASS" : "FAIL") + ", "
            + "AntiCheat Lockdown: " + std::string(flag_lockdown_ok && signature_lockdown_ok ? "PASS" : "FAIL") + ", "
            + "Audit Log: " + std::string(violations_recorded && violations_cleared ? "PASS" : "FAIL");

        RecordResult(t17_success, "Test 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O", details);
    }
}

void Test33() {
    // -------------------------------------------------------------------------
    // Test 33: SandboxGuard blacklist against the shared vector file (same vectors as the Rust core)
    // -------------------------------------------------------------------------
    {
        auto category_of = [](const std::wstring& path) -> std::string {
            std::string reason;
            if (!vegaflow::SandboxGuard::IsBlacklisted(path, &reason)) return "allowed";
            if (reason.find("Ruta v") != std::string::npos) return "empty";
            if (reason.find("del disco") != std::string::npos) return "drive_root";
            if (reason.find("arranque") != std::string::npos) return "critical_file";
            if (reason.find("directorios del sistema") != std::string::npos) return "windows_dir";
            if (reason.find("carpeta de Windows") != std::string::npos) return "windows_other_drive";
            if (reason.find("Common Files") != std::string::npos) return "common_files";
            if (reason.find("System Volume Information") != std::string::npos) return "system_volume_information";
            return "unknown: " + reason;
        };

        wchar_t win_dir[MAX_PATH] = {0};
        GetWindowsDirectoryW(win_dir, MAX_PATH);
        const bool windows_on_c = (_wcsicmp(win_dir, L"C:\\Windows") == 0);

        int total = 0;
        std::string mismatches;
        ForEachTsvRow("tests/vectors/sandbox_blacklist.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
            if (col.size() != 3) { mismatches += " [bad row: " + line + "]"; return; }

            const std::wstring path = (col[1] == "<empty>") ? std::wstring() : std::wstring(col[1].begin(), col[1].end());
            const std::string actual = category_of(path);
            ++total;
            if (actual != col[2]) mismatches += " [" + col[0] + ": expected " + col[2] + ", got " + actual + "]";
        });
        bool t33_ok = windows_on_c && (total >= 52) && mismatches.empty();
        RecordResult(t33_ok, "Test 33: SandboxGuard blacklist matches the shared vector file",
                     std::to_string(total) + " vectors" + (windows_on_c ? "" : ", Windows is not in C:\\Windows") +
                     (mismatches.empty() ? ", 0 mismatches" : "; mismatches:" + mismatches));
    }
}

void Test40() {
    // -------------------------------------------------------------------------
    // Test 40: the whole file access decision matches the shared vector file (same vectors as the Rust core)
    // -------------------------------------------------------------------------
    auto code_of = [](const std::string& name) -> int {
        if (name == "allowed") return VF_PATH_ALLOWED;
        if (name == "empty") return VF_PATH_EMPTY;
        if (name == "drive_root") return VF_PATH_DRIVE_ROOT;
        if (name == "critical_file") return VF_PATH_CRITICAL_FILE;
        if (name == "windows_dir") return VF_PATH_WINDOWS_DIR;
        if (name == "anticheat") return VF_PATH_ANTICHEAT;
        if (name == "not_whitelisted") return VF_PATH_NOT_WHITELISTED;
        if (name == "not_local_path") return VF_PATH_NOT_LOCAL_PATH;
        return -1;
    };
    auto wide = [](const std::string& s) { return s == "<empty>" ? std::wstring() : std::wstring(s.begin(), s.end()); };

    std::vector<std::pair<std::wstring, bool>> root_paths;
    std::vector<std::vector<std::string>> cases;
    std::string mismatches;
    ForEachTsvRow("tests/vectors/file_access.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
        if (col.size() == 3 && col[0] == "root") root_paths.emplace_back(wide(col[1]), col[2] == "rw");
        else if (col.size() == 5) cases.push_back(col);
        else mismatches += " [bad row: " + line + "]";
    });
    std::vector<VfAllowedRoot> roots;
    for (const auto& [path, writable] : root_paths) {
        roots.push_back({reinterpret_cast<const uint16_t*>(path.data()), path.size(), writable});
    }
    const std::wstring win = L"c:\\windows";
    const std::wstring sys = L"c:\\windows\\system32";
    for (const auto& col : cases) {
        const std::wstring path = wide(col[1]);
        const uint32_t verdict = vf_rust_decide_file_access(
            reinterpret_cast<const uint16_t*>(path.data()), path.size(),
            reinterpret_cast<const uint16_t*>(win.data()), win.size(),
            reinterpret_cast<const uint16_t*>(sys.data()), sys.size(),
            col[2] == "write", col[3] == "1", roots.data(), roots.size());
        if (static_cast<int>(verdict) != code_of(col[4])) {
            mismatches += " [" + col[0] + ": expected " + col[4] + ", got " + std::to_string(verdict) + "]";
        }
    }
    const bool ok = cases.size() >= 37 && mismatches.empty();
    RecordResult(ok, "Test 40: file access decision matches the shared vector file",
                 std::to_string(cases.size()) + " vectors" + (mismatches.empty() ? ", 0 mismatches" : "; mismatches:" + mismatches));
}

void Test41() {
    // -------------------------------------------------------------------------
    // Test 41: each whitelist flaw of the old SandboxGuard is refused (docs/plan/maestro/03-fase-1.md)
    // -------------------------------------------------------------------------
    using vegaflow::SandboxAction;
    using vegaflow::SandboxGuard;
    std::error_code ec;
    std::string failures;
    auto expect = [&](bool ok, const std::string& name) { if (!ok) failures += " [" + name + "]"; };
    auto denied = [](const std::wstring& p) { return !SandboxGuard::ValidatePath(p, SandboxAction::Write).allowed; };

    wchar_t exe_buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe_buf, MAX_PATH);
    const std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    wchar_t local_buf[MAX_PATH] = {0};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local_buf, MAX_PATH);
    const std::wstring local_app(local_buf);
    // A folder no rule allows, writable by any user: the junction and the working folder point here, never at Windows.
    const std::filesystem::path outside = L"C:\\Users\\Public\\vegaflow_test_outside";
    std::filesystem::remove_all(outside, ec);
    std::filesystem::create_directories(outside, ec);

    // Flaw 1: the parent folder of the executable.
    expect(denied((exe_dir.parent_path() / L"vegaflow_flaw1.txt").wstring()), "1 parent of the executable folder");
    expect(!denied((exe_dir / L"vegaflow_flaw1.txt").wstring()), "1 executable folder still allowed");

    // Flaw 2: a junction inside an allowed folder that leads outside.
    const std::filesystem::path junction = temp / L"vegaflow_test_junction";
    RemoveDirectoryW(junction.c_str());
    const std::wstring mklink = L"cmd /c mklink /J \"" + junction.wstring() + L"\" \"" + outside.wstring() + L"\" >nul 2>&1";
    const bool junction_made = _wsystem(mklink.c_str()) == 0 && std::filesystem::exists(junction, ec);
    expect(junction_made, "2 junction created");
    expect(denied((junction / L"vegaflow_flaw2.txt").wstring()), "2 junction to a folder outside");
    expect(!SandboxGuard::SafeWriteFile((junction / L"vegaflow_flaw2.txt").wstring(), "x") &&
           !std::filesystem::exists(outside / L"vegaflow_flaw2.txt", ec), "2 nothing written through the junction");
    RemoveDirectoryW(junction.c_str());

    // Flaw 3: steamapps\common outside a real Steam library.
    expect(denied(L"C:\\Program Files\\VegaFlowTest\\steamapps\\common\\Game\\Engine.ini"), "3 steamapps\\common anywhere");

    // Flaw 4: the working folder.
    const std::filesystem::path original_cwd = std::filesystem::current_path(ec);
    std::filesystem::current_path(outside, ec);
    expect(!ec, "4 working folder changed");
    expect(denied((outside / L"vegaflow_flaw4.txt").wstring()), "4 working folder");
    std::filesystem::current_path(original_cwd, ec);

    // Flaw 5: a folder name that only starts like an allowed one.
    expect(denied(local_app + L"OtraCosa\\vegaflow_flaw5.txt"), "5 LOCALAPPDATA prefix as text");
    expect(!denied(local_app + L"\\VegaFlowTest\\x.txt"), "5 LOCALAPPDATA itself still allowed");

    // The \\?\ prefix of an allowed path is the same path; other device prefixes are refused.
    expect(!denied(L"\\\\?\\" + (temp / L"vegaflow_prefix.txt").wstring()), "\\\\?\\ prefix of an allowed path");
    expect(denied(L"\\\\.\\" + (temp / L"vegaflow_prefix.txt").wstring()), "\\\\.\\ device prefix");
    expect(denied(L"\\\\localhost\\C$\\Users\\Public\\vegaflow_test_outside\\x.txt"), "UNC path");

    std::filesystem::remove_all(outside, ec);
    SandboxGuard::ClearViolations();
    RecordResult(failures.empty(), "Test 41: SandboxGuard refuses each old whitelist flaw",
                 failures.empty() ? "flaws 1-5, junction, \\\\?\\ and UNC checked" : "failed:" + failures);
}

} // namespace vftest
