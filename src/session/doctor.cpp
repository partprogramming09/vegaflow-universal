#include "session/doctor.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

#include "bridge/rust_bridge.h"
#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "session/report.hpp"
#include "windows/privileges.hpp"
#include "windows/registry.hpp"
#include "windows/text_convert.hpp"

namespace vegaflow::session {

namespace {

enum Severity { kInfo = 0, kWarn = 1, kCritical = 2 };

struct Finding {
    Severity severity;
    std::string text;
};

std::string Gib(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// MiB under 1 GiB, GiB above: a 144 MB cache reads better as 144.0 MiB than as 0.1 GiB.
std::string Size(uint64_t bytes) {
    if (bytes >= 1024ull * 1024 * 1024) return Gib(bytes);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

std::string JsonString(const std::string& s) {
    std::string out = "\"";
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

bool ContainsNoCase(const std::string& s, const char* part) {
    const std::string needle = part;
    return std::search(s.begin(), s.end(), needle.begin(), needle.end(),
                       [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != s.end();
}

// Kind byte of vf_rust_doctor_cache_findings: 0 AMD, 1 NVIDIA, 2 generic D3D, +0x80 when the folder exists.
uint8_t CacheKindByte(const DoctorCacheDir& d) {
    uint8_t k = ContainsNoCase(d.path, "\\AMD\\") ? 0 : ContainsNoCase(d.path, "NVIDIA") ? 1 : 2;
    return static_cast<uint8_t>(k | (d.exists ? 0x80 : 0));
}

const char* SeverityWord(Severity s) { return s == kCritical ? "CRITICAL" : s == kWarn ? "WARN" : "INFO"; }

const char* CacheFindingText(uint8_t code) {
    switch (code) {
    case 1: return "no shader cache folders were found; the graphics driver creates them when a game compiles shaders";
    case 2: return "no AMD or NVIDIA shader cache folder exists, so shaders may compile again at every launch";
    case 3: return "the shader caches are empty and the disk is almost full, so shaders cannot be saved";
    case 4: return "a shader cache is over 10 GiB: you can delete it and the driver rebuilds it (VegaFlow deletes nothing)";
    default: return "unknown shader cache finding";
    }
}

} // namespace

DoctorReport BuildDoctorReport(const DoctorInputs& in) {
    const auto hide = [&](const std::string& s) { return RedactReport(s, in.computer_name, in.domain_name); };

    std::vector<Finding> findings;
    uint8_t system_disk = 0; // the verdict of the first drive, used for the cache rules
    std::vector<std::string> disk_lines;
    std::string disks_json;

    for (size_t i = 0; i < in.disks.size(); ++i) {
        const DoctorDisk& d = in.disks[i];
        const std::string where = hide(d.label + " (" + d.path + ")");
        const uint8_t advice = d.probed ? vf_rust_doctor_disk_advice(d.free_bytes, d.total_bytes) : 0;
        if (i == 0) system_disk = advice;
        const char* word = advice == 3 ? "critical" : advice == 2 ? "low" : advice == 1 ? "ok" : "unknown";
        if (advice == 0) {
            disk_lines.push_back(where + ": could not read");
            findings.push_back({kWarn, where + ": could not read the free space"});
        } else {
            disk_lines.push_back(where + ": " + Gib(d.free_bytes) + " free of " + Gib(d.total_bytes));
            if (advice == 3) {
                findings.push_back({kCritical, where + " has only " + Gib(d.free_bytes) + " free: the shader cache cannot grow"});
            } else if (advice == 2) {
                findings.push_back({kWarn, where + " is running low: " + Gib(d.free_bytes) + " free of " + Gib(d.total_bytes)});
            }
        }
        disks_json += std::string(i ? "," : "") + "{\"label\":" + JsonString(hide(d.label)) + ",\"path\":" + JsonString(hide(d.path)) +
                      ",\"free_bytes\":" + std::to_string(d.probed ? d.free_bytes : 0) + ",\"total_bytes\":" + std::to_string(d.probed ? d.total_bytes : 0) +
                      ",\"advice\":\"" + word + "\"}";
    }

    if (!in.elevated) findings.push_back({kInfo, "running as a standard user (not elevated): some data may be missing"});

    if (!in.caches_probed) {
        findings.push_back({kInfo, "could not read the shader caches"});
    } else {
        std::vector<uint8_t> kinds;
        std::vector<uint64_t> sizes;
        for (const DoctorCacheDir& c : in.caches) {
            kinds.push_back(CacheKindByte(c));
            sizes.push_back(c.bytes);
        }
        uint8_t out[16] = {};
        const size_t n = vf_rust_doctor_cache_findings(kinds.data(), sizes.data(), kinds.size(), system_disk, out, sizeof out);
        for (size_t i = 0; i < std::min<size_t>(n, sizeof out / 2); ++i) {
            findings.push_back({out[2 * i] == 1 ? kWarn : kInfo, CacheFindingText(out[2 * i + 1])});
        }
    }

    std::string names; // NUL-separated, as the Rust side wants it
    for (const std::string& p : in.processes) names += p + '\0';
    uint32_t hits[32] = {};
    const size_t found = vf_rust_doctor_oem_conflicts(reinterpret_cast<const uint8_t*>(names.data()), names.size(), hits, 32);
    for (size_t i = 0; i < std::min<size_t>(found, 32); ++i) {
        char buf[256] = {};
        const size_t len = vf_rust_doctor_oem_info(hits[i], reinterpret_cast<uint8_t*>(buf), sizeof buf);
        const std::string info(buf, std::min(len, sizeof buf));
        const size_t tab = info.find('\t');
        findings.push_back({kWarn, info.substr(0, tab) + " is running and also changes the " + info.substr(tab + 1) + ": it can fight VegaFlow over them"});
    }

    DoctorReport r;
    int worst = 0;
    for (const Finding& f : findings) worst = std::max(worst, static_cast<int>(f.severity));
    r.exit_code = worst;

    r.text = "VegaFlow doctor " + hide(in.version) + "\nAdministrator: " + (in.elevated ? "yes" : "no") + "\nDisks:\n";
    for (const std::string& l : disk_lines) r.text += "  " + l + "\n";
    r.text += "Shader caches:\n";
    std::string caches_json;
    if (!in.caches_probed) r.text += "  could not read\n";
    for (const DoctorCacheDir& c : in.caches) {
        if (!c.exists) continue; // a folder that is not there has nothing to show
        const uint8_t k = CacheKindByte(c) & 0x7F;
        const char* kind = k == 0 ? "AMD" : k == 1 ? "NVIDIA" : "D3D";
        const std::string path = hide(c.path);
        r.text += std::string("  ") + kind + ": " + path + ": " + Size(c.bytes) + " (" + std::to_string(c.files) + " files)\n";
        caches_json += std::string(caches_json.empty() ? "" : ",") + "{\"kind\":\"" + kind + "\",\"path\":" + JsonString(path) +
                       ",\"bytes\":" + std::to_string(c.bytes) + ",\"files\":" + std::to_string(c.files) + "}";
    }
    if (in.caches_probed && caches_json.empty()) r.text += "  none found\n";
    std::string reasons_json;
    if (!in.profile_reasons.empty()) r.text += "Hardware profile, why:\n";
    for (size_t i = 0; i < in.profile_reasons.size(); ++i) {
        const std::string t = hide(in.profile_reasons[i]);
        r.text += "  - " + t + "\n";
        reasons_json += std::string(i ? "," : "") + JsonString(t);
    }
    r.text += "Findings:\n";
    if (findings.empty()) r.text += "  [OK] nothing to report\n";
    std::string findings_json;
    for (size_t i = 0; i < findings.size(); ++i) {
        const std::string t = hide(findings[i].text);
        r.text += std::string("  [") + SeverityWord(findings[i].severity) + "] " + t + "\n";
        findings_json += std::string(i ? "," : "") + "{\"severity\":\"" + SeverityWord(findings[i].severity) + "\",\"text\":" + JsonString(t) + "}";
    }
    r.json = "{\"version\":" + JsonString(hide(in.version)) + ",\"elevated\":" + (in.elevated ? "true" : "false") +
             ",\"exit_code\":" + std::to_string(r.exit_code) + ",\"disks\":[" + disks_json + "],\"shader_caches\":[" + caches_json + "],\"profile_reasons\":[" + reasons_json +
             "],\"findings\":[" + findings_json + "]}";
    return r;
}

namespace {

std::string Narrow(const std::wstring& w) { return app::WideToUtf8(w); }

DoctorDisk ProbeDisk(const std::string& label, const std::wstring& any_path_on_drive) {
    DoctorDisk d;
    d.label = label;
    wchar_t root[MAX_PATH] = {};
    if (!GetVolumePathNameW(any_path_on_drive.c_str(), root, MAX_PATH)) {
        d.path = Narrow(any_path_on_drive);
        return d;
    }
    d.path = Narrow(root);
    ULARGE_INTEGER avail{}, total{}, free_all{};
    if (GetDiskFreeSpaceExW(root, &avail, &total, &free_all)) {
        d.probed = true;
        d.free_bytes = avail.QuadPart;
        d.total_bytes = total.QuadPart;
    }
    return d;
}

std::string ComputerName(COMPUTER_NAME_FORMAT format) {
    wchar_t buf[256] = {};
    DWORD n = 256;
    return GetComputerNameExW(format, buf, &n) ? Narrow(std::wstring(buf, n)) : std::string();
}

} // namespace

DoctorReport ReadDoctorReport(const std::string& version) {
    DoctorInputs in;
    in.version = version;
    in.elevated = PrivilegeManager::IsProcessElevated();

    wchar_t sys_dir[MAX_PATH] = {};
    GetSystemDirectoryW(sys_dir, MAX_PATH);
    in.disks.push_back(ProbeDisk("System drive", sys_dir));

    // The active game's drive, when a game is open and it lives on another drive.
    if (const auto game = ProcessDetector::DetectActiveGame()) {
        DoctorDisk g = ProbeDisk("Game drive", game->full_path);
        if (g.path != in.disks[0].path) in.disks.push_back(g);
    }

    try {
        WindowsRegistryPort reg;
        for (const ShaderCacheReport& c : reg.InspectShaderCaches()) in.caches.push_back({Narrow(c.path), c.total_size_bytes, c.file_count, c.exists});
    } catch (...) {
        in.caches_probed = false;
    }

    // F2.7: why the profile is what it is (read-only: the same detection the daemon runs at start).
    try {
        const SystemHardwareProfile hw = HardwareProfiler::DetectHardware();
        in.profile_reasons = HardwareProfiler::DecisionReasonTexts(HardwareProfiler::BuildOptimizationPolicy(hw).decision_reasons);
    } catch (...) {
        in.profile_reasons.clear(); // the section is left out; the rest of the report stands
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{};
        e.dwSize = sizeof e;
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) in.processes.push_back(Narrow(e.szExeFile));
        CloseHandle(snap);
    }

    in.computer_name = ComputerName(ComputerNameDnsHostname);
    in.domain_name = ComputerName(ComputerNameDnsDomain);
    wchar_t netbios[64] = {};
    DWORD nb = 64;
    if (in.computer_name.empty() && GetComputerNameW(netbios, &nb)) in.computer_name = Narrow(std::wstring(netbios, nb));

    return BuildDoctorReport(in);
}

int RunDoctor(bool json, const std::string& version) {
    const DoctorReport r = ReadDoctorReport(version);
    std::cout << (json ? r.json : r.text) << (json ? "\n" : "");
    return r.exit_code;
}

} // namespace vegaflow::session
