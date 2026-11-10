// Integration tests: privileges and the system report. They run on a real Windows machine (registry, power plan, processes).
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
#include "windows/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"

namespace vftest {

void Test01() {
    // -------------------------------------------------------------------------
    // TEST 1: Privileges
    // -------------------------------------------------------------------------
    {
        bool elevated = vegaflow::PrivilegeManager::IsProcessElevated();
        vegaflow::PrivilegeStatus status = vegaflow::PrivilegeManager::EnableRequiredPrivileges(false);
        bool t1_success = (status.is_elevated == elevated);
        RecordResult(
            t1_success,
            "Test 1: PrivilegeManager elevation check and privilege adjustment",
            elevated ? "Elevated/Admin" : "Standard User"
        );
    }
}

void Test18() {
    // -------------------------------------------------------------------------
    // TEST 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization
    // -------------------------------------------------------------------------
    {
        vegaflow::SystemInfo sys_info;
        const auto& snap = sys_info.Snapshot();

        // 1. CPU Validation
        bool cpu_brand_ok = !snap.cpu.brand_string.empty();
        bool cpu_cores_ok = (snap.cpu.physical_cores > 0 && snap.cpu.logical_processors >= snap.cpu.physical_cores);
        bool cpu_uarch_ok = !snap.cpu.microarchitecture.empty();
        bool cpu_l3_ok = (snap.cpu.l3_cache_bytes > 0 && snap.cpu.l3_cache_mb > 0.0);
        bool cpu_boost_ok = (snap.cpu.boost_target_ghz >= 3.0 && !snap.cpu.stapm_target.empty());
        bool cpu_sets_ok = (!snap.cpu.active_game_cpu_sets.empty() && !snap.cpu.active_background_cpu_sets.empty());
        bool cpu_all_ok = cpu_brand_ok && cpu_cores_ok && cpu_uarch_ok && cpu_l3_ok && cpu_boost_ok && cpu_sets_ok;

        // 2. GPU Validation
        bool gpu_name_ok = !snap.gpu.adapter_name.empty();
        bool gpu_arch_ok = (snap.gpu.architecture.find("UMA") != std::string::npos);
        bool gpu_vram_ok = (snap.gpu.bios_dedicated_vram_bytes > 0 && snap.gpu.dynamic_shared_vram_bytes > 0);
        bool gpu_all_ok = gpu_name_ok && gpu_arch_ok && gpu_vram_ok;

        // 3. RAM Validation
        bool ram_phys_ok = (snap.ram.total_physical_bytes > 0 && snap.ram.total_physical_gb >= 1.0);
        bool ram_free_ok = (snap.ram.free_real_bytes > 0);
        bool ram_channel_ok = (snap.ram.channel_topology.find("Channel") != std::string::npos &&
                               snap.ram.memory_bus_width_bits >= 64);
        bool ram_all_ok = ram_phys_ok && ram_free_ok && ram_channel_ok;

        // 4. OS & Kernel Validation
        bool os_name_ok = (snap.os_kernel.os_name.find("Windows") != std::string::npos);
        bool os_build_ok = (snap.os_kernel.build_number > 0);
        bool os_timer_ok = (snap.os_kernel.timer_resolution_ms > 0.0 && snap.os_kernel.timer_resolution_ns100 > 0);
        bool os_all_ok = os_name_ok && os_build_ok && os_timer_ok;

        // 5. JSON Serialization Validation
        std::string json_str = sys_info.ToJson();
        bool json_structure_ok = (!json_str.empty() && json_str.front() == '{' && json_str.back() == '\n' &&
                                  json_str[json_str.size() - 2] == '}');

        // Check required JSON keys
        bool has_cpu_key = (json_str.find("\"cpu\":") != std::string::npos);
        bool has_gpu_key = (json_str.find("\"gpu\":") != std::string::npos);
        bool has_ram_key = (json_str.find("\"ram\":") != std::string::npos);
        bool has_os_key = (json_str.find("\"os_kernel\":") != std::string::npos);
        bool has_uarch_key = (json_str.find("\"microarchitecture\":") != std::string::npos);
        bool has_l3_key = (json_str.find("\"l3_cache_bytes\":") != std::string::npos);
        bool has_boost_key = (json_str.find("\"boost_target_ghz\":") != std::string::npos);
        bool has_stapm_key = (json_str.find("\"stapm_target\":") != std::string::npos);
        bool has_uma_key = (json_str.find("\"architecture\":") != std::string::npos &&
                            json_str.find("UMA (Unified Memory Architecture)") != std::string::npos);
        bool has_vram_key = (json_str.find("\"bios_dedicated_vram_bytes\":") != std::string::npos &&
                             json_str.find("\"dynamic_shared_vram_bytes\":") != std::string::npos);
        bool has_dxgi_key = (json_str.find("\"dxgi_budget\":") != std::string::npos);
        bool has_ram_topo_key = (json_str.find("\"channel_topology\":") != std::string::npos);
        bool has_timer_key = (json_str.find("\"timer_resolution\":") != std::string::npos);
        bool has_mmcss_key = (json_str.find("\"mmcss_gaming_profile\":") != std::string::npos);
        bool has_comp_key = (json_str.find("\"memory_compression\":") != std::string::npos);

        bool json_keys_ok = has_cpu_key && has_gpu_key && has_ram_key && has_os_key &&
                            has_uarch_key && has_l3_key && has_boost_key && has_stapm_key &&
                            has_uma_key && has_vram_key && has_dxgi_key && has_ram_topo_key &&
                            has_timer_key && has_mmcss_key && has_comp_key;

        bool t18_success = cpu_all_ok && gpu_all_ok && ram_all_ok && os_all_ok &&
                           json_structure_ok && json_keys_ok;

        std::string details = "CPU: " + snap.cpu.brand_string + " (" + snap.cpu.microarchitecture + ", " +
            std::to_string(static_cast<int>(snap.cpu.l3_cache_mb)) + " MB L3, " +
            std::to_string(snap.cpu.physical_cores) + "C/" + std::to_string(snap.cpu.logical_processors) + "T), "
            + "GPU: " + snap.gpu.adapter_name + " [" + snap.gpu.architecture + "], "
            + "RAM: " + std::to_string(static_cast<int>(snap.ram.total_physical_gb)) + " GB " + snap.ram.channel_topology + ", "
            + "OS: " + snap.os_kernel.full_version_string + " (Timer: " + std::to_string(snap.os_kernel.timer_resolution_ms) + " ms), "
            + "JSON: " + (json_keys_ok ? "VALID" : "INVALID");

        RecordResult(t18_success, "Test 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization", details);
    }
}

} // namespace vftest
