// Integration tests: the rust core through the c bridge. They run on a real Windows machine (registry, power plan, processes).
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
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/dxvk_manager.hpp"
#include "windows/sandbox_guard.hpp"
#include "session/system_info.hpp"
#include "bridge/rust_bridge.h"
#include <filesystem>
#include <fstream>

#include "../test_support.hpp"

namespace vftest {

void Test31() {
    // -------------------------------------------------------------------------
    // Test 31: Hybrid Rust FFI Core (Zero-Allocation UMA Guard, L3 Topology, Hysteresis & Anti-Cheat Scanner)
    // -------------------------------------------------------------------------
    {
        // 1. vf_rust_vram_pressure_threshold & vf_rust_evaluate_memory_purge
        const double threshold = vf_rust_vram_pressure_threshold();
        VfMemoryPurgeInput normal_input{};
        normal_input.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        normal_input.free_and_zero_bytes = 4000ULL * 1024 * 1024;
        normal_input.standby_bytes = 2000ULL * 1024 * 1024;
        normal_input.local_budget_bytes = 2048ULL * 1024 * 1024;
        normal_input.gpu_shared_usage_bytes = 1000ULL * 1024 * 1024; // ~48%
        normal_input.vram_valid = true;
        normal_input.memory_lists_valid = true;
        normal_input.is_uma_gpu = true;
        normal_input.allow_ram_purge = true;
        normal_input.min_free_ram_mb = 1200;
        normal_input.min_standby_mb = 1500;
        normal_input.cooldown_seconds = 90;
        normal_input.elapsed_since_last_purge_ms = -1;
        normal_input.max_game_disk_read_bytes_per_sec = 15ULL * 1024 * 1024;
        normal_input.pressure_threshold_override = 0.0;
        normal_input.prev_interventions = 10;

        VfMemoryPurgeDecision dec_normal = vf_rust_evaluate_memory_purge(&normal_input);
        bool normal_ok = (threshold > 0.80 && threshold < 0.90) &&
                         (!dec_normal.guard_triggered &&
                          !dec_normal.dxgi_over_ceiling &&
                          !dec_normal.free_ram_critical &&
                          dec_normal.purge_level == VF_PURGE_LEVEL_NONE &&
                          dec_normal.available_os_headroom_mb == 6000 &&
                          dec_normal.total_interventions == 10);

        VfMemoryPurgeInput crit_input = normal_input;
        crit_input.gpu_shared_usage_bytes = static_cast<uint64_t>(2048ULL * 1024 * 1024 * 0.90); // 90% DXGI
        VfMemoryPurgeDecision dec_crit = vf_rust_evaluate_memory_purge(&crit_input);
        bool crit_ok = (dec_crit.guard_triggered &&
                        dec_crit.dxgi_over_ceiling &&
                        !dec_crit.free_ram_critical &&
                        dec_crit.purge_level == VF_PURGE_LEVEL_LOW &&
                        dec_crit.should_trim_background &&
                        dec_crit.available_os_headroom_mb == 6000 &&
                        dec_crit.total_interventions == 11);

        // 2. Factory Do-Not-Trim protection via Rust FFI (OBS, Discord protected; browser allowed)
        const char obs_name[] = "C:\\Program Files\\obs-studio\\bin\\64bit\\obs64.exe";
        const char discord_name[] = "Discord.exe";
        const char chrome_name[] = "chrome.exe";
        bool part2_ok = vf_rust_is_factory_memory_protected_exe(obs_name, sizeof(obs_name) - 1) &&
                        vf_rust_is_factory_memory_protected_exe(discord_name, sizeof(discord_name) - 1) &&
                        !vf_rust_is_factory_memory_protected_exe(chrome_name, sizeof(chrome_name) - 1);

        // 3. vf_rust_partition_cpu_topology for Ryzen 5 4600H (12 threads, 2 CCX)
        VfCpuCoreEntry ryzen_entries[12]{};
        for (uint32_t i = 0; i < 12; ++i) {
            uint32_t core = i / 2;
            uint8_t llc = (core < 3) ? 0 : 1;
            ryzen_entries[i].cpu_set_id = 256 + i;
            ryzen_entries[i].core_index = core;
            ryzen_entries[i].logical_processor_index = i;
            ryzen_entries[i].last_level_cache_index = llc;
            ryzen_entries[i].efficiency_class = 0;
        }
        VfTopologyPartitionOut ryzen_out{};
        bool part_ok = vf_rust_partition_cpu_topology(ryzen_entries, 12, &ryzen_out);
        bool ryzen_ok = part_ok &&
                        (ryzen_out.physical_cores == 6) &&
                        (ryzen_out.logical_processors == 12) &&
                        ryzen_out.has_multiple_ccx &&
                        !ryzen_out.has_hybrid_cores &&
                        (ryzen_out.game_count == 8) &&
                        (ryzen_out.background_count == 2) &&
                        (ryzen_out.background_cpu_set_ids[0] == 266) &&
                        (ryzen_out.background_cpu_set_ids[1] == 267) &&
                        (ryzen_out.game_cpu_set_ids[0] == 258);

        // 4. vf_rust_eval_bg_confinement (histeresis)
        bool h1 = vf_rust_eval_bg_confinement(0.23, false);  // false
        bool h2 = vf_rust_eval_bg_confinement(0.50, false);  // true
        bool h3 = vf_rust_eval_bg_confinement(0.35, true);   // true
        bool h4 = vf_rust_eval_bg_confinement(0.24, true);   // false
        bool hyst_ok = (!h1 && h2 && h3 && !h4);

        // 5. vf_rust_has_anticheat_signature and vf_rust_is_critical_system_filename
        const char ac1[] = "C:\\Games\\Genshin\\HoYoKProtect.sys";
        const char ac2[] = "D:\\SteamLibrary\\steamapps\\common\\Apex\\EasyAntiCheat_EOS.dll";
        const char ac_safe[] = "C:\\Games\\MyGame\\Config\\Engine.ini";
        bool ac1_blocked = vf_rust_has_anticheat_signature(ac1, sizeof(ac1) - 1);
        bool ac2_blocked = vf_rust_has_anticheat_signature(ac2, sizeof(ac2) - 1);
        bool ac_safe_allowed = !vf_rust_has_anticheat_signature(ac_safe, sizeof(ac_safe) - 1);

        const char crit1[] = "pagefile.sys";
        const char crit_safe[] = "Engine.ini";
        bool crit1_blocked = vf_rust_is_critical_system_filename(crit1, sizeof(crit1) - 1);
        bool crit_safe_allowed = !vf_rust_is_critical_system_filename(crit_safe, sizeof(crit_safe) - 1);
        bool sec_ok = (ac1_blocked && ac2_blocked && ac_safe_allowed && crit1_blocked && crit_safe_allowed);

        // 6. Stress loop of 1,000 consecutive iterations over the Rust FFI functions
        bool stress_ok = true;
        for (int i = 0; i < 1000; ++i) {
            normal_input.prev_interventions = static_cast<uint32_t>(i);
            VfMemoryPurgeDecision d = vf_rust_evaluate_memory_purge(&normal_input);
            if (d.available_os_headroom_mb != 6000) { stress_ok = false; break; }

            VfTopologyPartitionOut t_out{};
            if (!vf_rust_partition_cpu_topology(ryzen_entries, 12, &t_out)) { stress_ok = false; break; }

            bool conf = vf_rust_eval_bg_confinement(0.55, false);
            if (!conf) { stress_ok = false; break; }

            if (!vf_rust_has_anticheat_signature(ac1, sizeof(ac1) - 1)) { stress_ok = false; break; }
            if (!vf_rust_is_critical_system_filename(crit1, sizeof(crit1) - 1)) { stress_ok = false; break; }
            if (!vf_rust_is_factory_memory_protected_exe(obs_name, sizeof(obs_name) - 1)) { stress_ok = false; break; }
        }

        uint64_t final_ws_bytes = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        bool kpi_ws_ok = (final_ws_bytes < 15ULL * 1024ULL * 1024ULL);

        bool t31_pass = normal_ok && crit_ok && part2_ok && ryzen_ok && hyst_ok && sec_ok && stress_ok && kpi_ws_ok;
        RecordResult(t31_pass,
                     "Test 31: Hybrid Rust FFI Core (Zero-Allocation Memory Purge, L3 Topology, Hysteresis & Anti-Cheat Scanner)",
                     "UMA: " + std::string(normal_ok && crit_ok ? "PASS" : "FAIL") +
                     ", DoNotTrim: " + std::string(part2_ok ? "PASS" : "FAIL") +
                     ", L3 Ryzen 4600H: " + std::string(ryzen_ok ? "PASS" : "FAIL") +
                     ", Hysteresis: " + std::string(hyst_ok ? "PASS" : "FAIL") +
                     ", Security/Anti-Cheat: " + std::string(sec_ok ? "PASS" : "FAIL") +
                     ", 1k Stress/WS < 15MB: " + std::string(stress_ok && kpi_ws_ok ? "PASS" : "FAIL") +
                     " (" + std::to_string(final_ws_bytes / 1024ULL) + " KB)");
    }
}

} // namespace vftest
