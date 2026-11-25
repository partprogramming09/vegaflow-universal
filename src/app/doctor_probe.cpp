#include "app/doctor_probe.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include "detect/hardware_profiler.hpp"
#include "detect/process_detector.hpp"
#include "ports/text_convert.hpp"
#include "session/config.hpp"
#include "session/profile_reasons.hpp"
#include "ports/config_location.hpp"
#include "windows/privileges.hpp"
#include "windows/registry.hpp"
#include "windows/safe_file_system.hpp"

namespace vegaflow::app {

namespace {

std::string Narrow(const std::wstring& w) { return WideToUtf8(w); }

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

std::string NameOf(COMPUTER_NAME_FORMAT format) {
    wchar_t buf[256] = {};
    DWORD n = 256;
    return GetComputerNameExW(format, buf, &n) ? Narrow(std::wstring(buf, n)) : std::string();
}

} // namespace

bool WindowsDoctorProbe::IsElevated() { return PrivilegeManager::IsProcessElevated(); }

DoctorDisk WindowsDoctorProbe::ProbeSystemDrive() {
    wchar_t sys_dir[MAX_PATH] = {};
    GetSystemDirectoryW(sys_dir, MAX_PATH);
    return ProbeDisk("System drive", sys_dir);
}

std::optional<DoctorDisk> WindowsDoctorProbe::ProbeActiveGameDrive() {
    if (const auto game = ProcessDetector::DetectActiveGame()) return ProbeDisk("Game drive", game->full_path);
    return std::nullopt;
}

std::vector<DoctorCacheDir> WindowsDoctorProbe::ProbeShaderCaches() {
    std::vector<DoctorCacheDir> out;
    WindowsRegistryPort reg;
    for (const ShaderCacheReport& c : reg.InspectShaderCaches()) out.push_back({Narrow(c.path), c.total_size_bytes, c.file_count, c.exists});
    return out;
}

std::vector<std::string> WindowsDoctorProbe::ProfileReasons() {
    const SystemHardwareProfile hw = HardwareProfiler::DetectHardware();
    return session::DecisionReasonTexts(HardwareProfiler::BuildOptimizationPolicy(hw).decision_reasons);
}

std::string WindowsDoctorProbe::ConfigWarning() {
    SafeFileSystem fs;
    const std::wstring path = ConfigFilePath();
    ConfigStore store(fs, path, ConfigLocationTrusted(path));
    store.Load();
    return store.Report().Warning();
}

std::vector<std::string> WindowsDoctorProbe::RunningProcesses() {
    std::vector<std::string> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{};
        e.dwSize = sizeof e;
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) out.push_back(Narrow(e.szExeFile));
        CloseHandle(snap);
    }
    return out;
}

std::string WindowsDoctorProbe::ComputerName() {
    std::string name = NameOf(ComputerNameDnsHostname);
    wchar_t netbios[64] = {};
    DWORD nb = 64;
    if (name.empty() && GetComputerNameW(netbios, &nb)) name = Narrow(std::wstring(netbios, nb));
    return name;
}

std::string WindowsDoctorProbe::DomainName() { return NameOf(ComputerNameDnsDomain); }

} // namespace vegaflow::app
