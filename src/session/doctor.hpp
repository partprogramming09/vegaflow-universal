#pragma once
// `--doctor`: a diagnosis a person can paste in an issue. Free disk space for the shader cache, the state of the shader
// caches and the manufacturer tools that fight VegaFlow over the power plan. It only reads; the rules are in Rust
// (policy/doctor.rs) and BuildDoctorReport is a pure function of the probe results, so tests give it plain data.

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow::session {

struct DoctorDisk {
    std::string label; // "System drive", "Game drive"
    std::string path;
    bool probed = false; // false when the Windows call failed
    uint64_t free_bytes = 0;
    uint64_t total_bytes = 0;
};

struct DoctorCacheDir {
    std::string path;
    uint64_t bytes = 0;
    uint32_t files = 0;
    bool exists = false;
};

struct DoctorInputs {
    std::string version;
    bool elevated = false;
    std::vector<DoctorDisk> disks; // the first one is the system drive
    bool caches_probed = true;
    std::vector<DoctorCacheDir> caches;
    std::vector<std::string> processes; // names or full paths of the running processes
    std::string computer_name;          // hidden in the output
    std::string domain_name;            // hidden in the output
};

struct DoctorReport {
    int exit_code = 0; // 0 all good, 1 warnings, 2 something critical
    std::string text;  // LF lines
    std::string json;
};

// User, computer and domain names are replaced (RedactReport); nothing is cut or reflowed.
DoctorReport BuildDoctorReport(const DoctorInputs& in);

// Reads the machine, prints the report (text, or JSON when `json`) and returns its exit code. Writes nothing.
int RunDoctor(bool json, const std::string& version);

} // namespace vegaflow::session
