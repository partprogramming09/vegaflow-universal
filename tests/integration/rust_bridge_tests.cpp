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

void Test31() {
    // -------------------------------------------------------------------------
    // Test 31: Hybrid Rust FFI Core (Zero-Allocation UMA Guard, L3 Topology, Hysteresis & Anti-Cheat Scanner)
    // -------------------------------------------------------------------------
    {
        // 1. vf_rust_evaluate_uma_pressure (normal case and critical case 90% DXGI)
        VfUmaTelemetryInput normal_input{};
        normal_input.total_physical_bytes = 16ULL * 1024 * 1024 * 1024;
        normal_input.free_and_zero_bytes = 4000ULL * 1024 * 1024;
        normal_input.standby_bytes = 2000ULL * 1024 * 1024;
        normal_input.local_budget_bytes = 2048ULL * 1024 * 1024;
        normal_input.local_usage_bytes = 1000ULL * 1024 * 1024; // ~48%
        normal_input.runtime_pressure_threshold = 0.85;
        normal_input.vram_valid = true;
        normal_input.is_discrete_tier = false;

        VfUmaGuardDecision dec_normal = vf_rust_evaluate_uma_pressure(&normal_input, 10);
        bool normal_ok = (!dec_normal.guard_triggered &&
                          !dec_normal.dxgi_over_ceiling &&
                          !dec_normal.free_ram_critical &&
                          dec_normal.available_os_headroom_mb == 6000 &&
                          dec_normal.total_interventions == 10);

        VfUmaTelemetryInput crit_input = normal_input;
        crit_input.local_usage_bytes = static_cast<uint64_t>(2048ULL * 1024 * 1024 * 0.90); // 90% DXGI
        VfUmaGuardDecision dec_crit = vf_rust_evaluate_uma_pressure(&crit_input, 10);
        bool crit_ok = (dec_crit.guard_triggered &&
                        dec_crit.dxgi_over_ceiling &&
                        !dec_crit.free_ram_critical &&
                        dec_crit.available_os_headroom_mb == 6000 &&
                        dec_crit.total_interventions == 11);

        // 2. vf_rust_query_nt_memory_lists and vf_rust_purge_nt_standby_list(true) in dry-run
        VfNtMemoryListSample nt_sample = vf_rust_query_nt_memory_lists();
        bool nt_query_ok = (nt_sample.valid ? (nt_sample.free_and_zero_bytes > 0 || nt_sample.standby_bytes > 0) : true);
        bool nt_purge_dry = vf_rust_purge_nt_standby_list(true);
        bool part2_ok = nt_query_ok && nt_purge_dry;

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
            VfUmaGuardDecision d = vf_rust_evaluate_uma_pressure(&normal_input, static_cast<uint32_t>(i));
            if (d.available_os_headroom_mb != 6000) { stress_ok = false; break; }

            VfTopologyPartitionOut t_out{};
            if (!vf_rust_partition_cpu_topology(ryzen_entries, 12, &t_out)) { stress_ok = false; break; }

            bool conf = vf_rust_eval_bg_confinement(0.55, false);
            if (!conf) { stress_ok = false; break; }

            if (!vf_rust_has_anticheat_signature(ac1, sizeof(ac1) - 1)) { stress_ok = false; break; }
            if (!vf_rust_is_critical_system_filename(crit1, sizeof(crit1) - 1)) { stress_ok = false; break; }
            if (!vf_rust_purge_nt_standby_list(true)) { stress_ok = false; break; }
        }

        uint64_t final_ws_bytes = vegaflow::MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
        bool kpi_ws_ok = (final_ws_bytes < 15ULL * 1024ULL * 1024ULL);

        bool t31_pass = normal_ok && crit_ok && part2_ok && ryzen_ok && hyst_ok && sec_ok && stress_ok && kpi_ws_ok;
        RecordResult(t31_pass,
                     "Test 31: Hybrid Rust FFI Core (Zero-Allocation UMA Guard, L3 Topology, Hysteresis & Anti-Cheat Scanner)",
                     "UMA: " + std::string(normal_ok && crit_ok ? "PASS" : "FAIL") +
                     ", NT Lists/Purge: " + std::string(part2_ok ? "PASS" : "FAIL") +
                     ", L3 Ryzen 4600H: " + std::string(ryzen_ok ? "PASS" : "FAIL") +
                     ", Hysteresis: " + std::string(hyst_ok ? "PASS" : "FAIL") +
                     ", Security/Anti-Cheat: " + std::string(sec_ok ? "PASS" : "FAIL") +
                     ", 1k Stress/WS < 15MB: " + std::string(stress_ok && kpi_ws_ok ? "PASS" : "FAIL") +
                     " (" + std::to_string(final_ws_bytes / 1024ULL) + " KB)");
    }
}

void Test34() {
    // -------------------------------------------------------------------------
    // Test 34: the structs of rust_bridge.h have the same layout in C++ and in Rust
    // -------------------------------------------------------------------------
    {
        // rust_bridge.h is written by hand: if one side gains, loses or reorders a field, the other side reads
        // garbage. Rust reports size, alignment and every field offset and size in the same order as this list.
#define VF_LAYOUT_BEGIN(T) expected.push_back(sizeof(T)); expected.push_back(alignof(T));
#define VF_FIELD(T, f) expected.push_back(offsetof(T, f)); expected.push_back(sizeof(static_cast<T*>(nullptr)->f));
        std::vector<size_t> expected;
        VF_LAYOUT_BEGIN(VfUmaTelemetryInput)
        VF_FIELD(VfUmaTelemetryInput, total_physical_bytes) VF_FIELD(VfUmaTelemetryInput, free_and_zero_bytes)
        VF_FIELD(VfUmaTelemetryInput, standby_bytes) VF_FIELD(VfUmaTelemetryInput, local_budget_bytes)
        VF_FIELD(VfUmaTelemetryInput, local_usage_bytes) VF_FIELD(VfUmaTelemetryInput, runtime_pressure_threshold)
        VF_FIELD(VfUmaTelemetryInput, vram_valid) VF_FIELD(VfUmaTelemetryInput, is_discrete_tier)
        VF_LAYOUT_BEGIN(VfUmaGuardDecision)
        VF_FIELD(VfUmaGuardDecision, available_os_headroom_mb) VF_FIELD(VfUmaGuardDecision, dxgi_budget_utilization)
        VF_FIELD(VfUmaGuardDecision, guard_triggered) VF_FIELD(VfUmaGuardDecision, dxgi_over_ceiling)
        VF_FIELD(VfUmaGuardDecision, free_ram_critical) VF_FIELD(VfUmaGuardDecision, total_interventions)
        VF_LAYOUT_BEGIN(VfNtMemoryListSample)
        VF_FIELD(VfNtMemoryListSample, free_and_zero_bytes) VF_FIELD(VfNtMemoryListSample, standby_bytes)
        VF_FIELD(VfNtMemoryListSample, modified_bytes) VF_FIELD(VfNtMemoryListSample, valid)
        VF_LAYOUT_BEGIN(VfCpuCoreEntry)
        VF_FIELD(VfCpuCoreEntry, cpu_set_id) VF_FIELD(VfCpuCoreEntry, core_index)
        VF_FIELD(VfCpuCoreEntry, logical_processor_index) VF_FIELD(VfCpuCoreEntry, last_level_cache_index)
        VF_FIELD(VfCpuCoreEntry, efficiency_class)
        VF_LAYOUT_BEGIN(VfTopologyPartitionOut)
        VF_FIELD(VfTopologyPartitionOut, game_cpu_set_ids) VF_FIELD(VfTopologyPartitionOut, game_count)
        VF_FIELD(VfTopologyPartitionOut, background_cpu_set_ids) VF_FIELD(VfTopologyPartitionOut, background_count)
        VF_FIELD(VfTopologyPartitionOut, physical_cores) VF_FIELD(VfTopologyPartitionOut, logical_processors)
        VF_FIELD(VfTopologyPartitionOut, has_multiple_ccx) VF_FIELD(VfTopologyPartitionOut, has_hybrid_cores)
        VF_LAYOUT_BEGIN(VfTierInput)
        VF_FIELD(VfTierInput, total_ram_mb) VF_FIELD(VfTierInput, physical_cores) VF_FIELD(VfTierInput, arch_gen)
        VF_FIELD(VfTierInput, dedicated_vram) VF_FIELD(VfTierInput, is_nvidia)
        VF_LAYOUT_BEGIN(VfAllowedRoot)
        VF_FIELD(VfAllowedRoot, path) VF_FIELD(VfAllowedRoot, len) VF_FIELD(VfAllowedRoot, writable)
        VF_LAYOUT_BEGIN(VfConfigSettings)
        VF_FIELD(VfConfigSettings, poll_interval_ms) VF_FIELD(VfConfigSettings, timer_resolution_ns100)
        VF_FIELD(VfConfigSettings, cooldown_seconds) VF_FIELD(VfConfigSettings, timer_forced_manual)
        VF_FIELD(VfConfigSettings, restrict_game_cpu_sets) VF_FIELD(VfConfigSettings, min_free_ram_mb)
        VF_FIELD(VfConfigSettings, min_standby_mb) VF_FIELD(VfConfigSettings, max_game_disk_read_mb_s)
        VF_LAYOUT_BEGIN(VfConfigLoad)
        VF_FIELD(VfConfigLoad, settings) VF_FIELD(VfConfigLoad, status) VF_FIELD(VfConfigLoad, rejected_keys)
        VF_LAYOUT_BEGIN(VfAceEntry)
        VF_FIELD(VfAceEntry, mask) VF_FIELD(VfAceEntry, sid_kind) VF_FIELD(VfAceEntry, allows)
        VF_FIELD(VfAceEntry, inherit_only)
#undef VF_FIELD
#undef VF_LAYOUT_BEGIN

        std::vector<size_t> actual(expected.size() + 8, 0);
        const size_t rust_count = vf_rust_bridge_layout(actual.data(), actual.size());
        actual.resize(std::min(rust_count, actual.size()));
        std::string diffs;
        for (size_t i = 0; i < std::max(expected.size(), actual.size()); ++i) {
            const size_t e = i < expected.size() ? expected[i] : SIZE_MAX;
            const size_t a = i < actual.size() ? actual[i] : SIZE_MAX;
            if (e != a) diffs += " [#" + std::to_string(i) + ": C++ " + std::to_string(e) + ", Rust " + std::to_string(a) + "]";
        }
        // The #defines Rust also defines: a value changed on one side only would be read as another status or key.
        const std::vector<uint32_t> expected_constants = {
            VF_CONFIG_CORRUPT, VF_CONFIG_KEY_REJECTED, VF_CONFIG_NEWER_VERSION, VF_CONFIG_OLD_VERSION, VF_CONFIG_EMPTY,
            VF_CONFIG_BLOCKS_SAVE, VF_CONFIG_KEY_VERSION, VF_CONFIG_KEY_POLL_INTERVAL_MS,
            VF_CONFIG_KEY_TIMER_RESOLUTION_NS100, VF_CONFIG_KEY_TIMER_FORCED_MANUAL, VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB, VF_CONFIG_KEY_MIN_STANDBY_MB, VF_CONFIG_KEY_COOLDOWN_SECONDS,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, VF_CONFIG_KEY_CUSTOM_GAME_EXES, VF_CONFIG_KEY_MEMORY,
            VF_CONFIG_SAVE_OK, VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_SID_OTHER, VF_SID_SYSTEM,
            VF_SID_ADMINISTRATORS, VF_SID_TRUSTED_INSTALLER};
        std::vector<uint32_t> rust_constants(expected_constants.size() + 8, 0);
        const size_t constant_count = vf_rust_bridge_constants(rust_constants.data(), rust_constants.size());
        rust_constants.resize(std::min(constant_count, rust_constants.size()));
        if (rust_constants != expected_constants) diffs += " [constants differ]";
        const bool t34_ok = (rust_count == expected.size()) && diffs.empty();
        RecordResult(t34_ok, "Test 34: rust_bridge.h structs have the same layout in C++ and Rust",
                     std::to_string(expected.size()) + " numbers from C++, " + std::to_string(rust_count) + " from Rust" +
                     (diffs.empty() ? ", 0 differences" : "; differences:" + diffs));
    }
}

} // namespace vftest
