#pragma once
// What `--doctor` reads about the machine, as plain data (the probe fills it, session/doctor builds the report from it).

#include <cstdint>
#include <string>

namespace vegaflow {

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

} // namespace vegaflow
