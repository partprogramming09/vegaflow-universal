// Test 34 (moved out of the integration file because it needs no Windows): the #[repr(C)] structs of the Rust core
// have the same layout as rust_bridge.h. It runs on the host (scripts/ci/host-unit-tests.sh) and on Windows.

#include "bridge/rust_bridge.h"

#include "../test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vftest {

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
        VF_LAYOUT_BEGIN(VfMemoryPurgeInput)
        VF_FIELD(VfMemoryPurgeInput, total_physical_bytes) VF_FIELD(VfMemoryPurgeInput, free_and_zero_bytes)
        VF_FIELD(VfMemoryPurgeInput, standby_bytes) VF_FIELD(VfMemoryPurgeInput, local_budget_bytes)
        VF_FIELD(VfMemoryPurgeInput, gpu_shared_usage_bytes) VF_FIELD(VfMemoryPurgeInput, gpu_dedicated_usage_bytes)
        VF_FIELD(VfMemoryPurgeInput, vram_valid)
        VF_FIELD(VfMemoryPurgeInput, memory_lists_valid) VF_FIELD(VfMemoryPurgeInput, is_uma_gpu)
        VF_FIELD(VfMemoryPurgeInput, allow_ram_purge) VF_FIELD(VfMemoryPurgeInput, min_free_ram_mb)
        VF_FIELD(VfMemoryPurgeInput, min_standby_mb) VF_FIELD(VfMemoryPurgeInput, cooldown_seconds)
        VF_FIELD(VfMemoryPurgeInput, elapsed_since_last_purge_ms) VF_FIELD(VfMemoryPurgeInput, game_disk_read_bytes_per_sec)
        VF_FIELD(VfMemoryPurgeInput, max_game_disk_read_bytes_per_sec) VF_FIELD(VfMemoryPurgeInput, pressure_threshold_override)
        VF_FIELD(VfMemoryPurgeInput, prev_interventions) VF_FIELD(VfMemoryPurgeInput, last_purge_level)
        VF_FIELD(VfMemoryPurgeInput, dry_run)
        VF_LAYOUT_BEGIN(VfMemoryPurgeDecision)
        VF_FIELD(VfMemoryPurgeDecision, purge_level) VF_FIELD(VfMemoryPurgeDecision, should_trim_background)
        VF_FIELD(VfMemoryPurgeDecision, skipped_due_to_cooldown) VF_FIELD(VfMemoryPurgeDecision, skipped_due_to_disk_io)
        VF_FIELD(VfMemoryPurgeDecision, cooldown_remaining_seconds) VF_FIELD(VfMemoryPurgeDecision, guard_triggered)
        VF_FIELD(VfMemoryPurgeDecision, dxgi_over_ceiling) VF_FIELD(VfMemoryPurgeDecision, free_ram_critical)
        VF_FIELD(VfMemoryPurgeDecision, memory_pressure)
        VF_FIELD(VfMemoryPurgeDecision, dxgi_budget_utilization) VF_FIELD(VfMemoryPurgeDecision, available_os_headroom_mb)
        VF_FIELD(VfMemoryPurgeDecision, total_interventions)
        VF_LAYOUT_BEGIN(VfCpuCoreEntry)
        VF_FIELD(VfCpuCoreEntry, cpu_set_id) VF_FIELD(VfCpuCoreEntry, core_index)
        VF_FIELD(VfCpuCoreEntry, logical_processor_index) VF_FIELD(VfCpuCoreEntry, group)
        VF_FIELD(VfCpuCoreEntry, last_level_cache_index) VF_FIELD(VfCpuCoreEntry, efficiency_class)
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
        VF_FIELD(VfConfigSettings, poll_interval_ms) VF_FIELD(VfConfigSettings, cooldown_seconds)
        VF_FIELD(VfConfigSettings, restrict_game_cpu_sets) VF_FIELD(VfConfigSettings, min_free_ram_mb)
        VF_FIELD(VfConfigSettings, min_standby_mb) VF_FIELD(VfConfigSettings, max_game_disk_read_mb_s)
        VF_FIELD(VfConfigSettings, auto_vram_reporting) VF_FIELD(VfConfigSettings, auto_priority_separation)
        VF_FIELD(VfConfigSettings, auto_game_dvr_off) VF_FIELD(VfConfigSettings, auto_power_profile)
        VF_FIELD(VfConfigSettings, auto_engine_presets)
        VF_LAYOUT_BEGIN(VfConfigLoad)
        VF_FIELD(VfConfigLoad, settings) VF_FIELD(VfConfigLoad, status) VF_FIELD(VfConfigLoad, rejected_keys)
        VF_LAYOUT_BEGIN(VfAceEntry)
        VF_FIELD(VfAceEntry, mask) VF_FIELD(VfAceEntry, sid_kind) VF_FIELD(VfAceEntry, allows)
        VF_FIELD(VfAceEntry, inherit_only)
        VF_LAYOUT_BEGIN(VfIniKv)
        VF_FIELD(VfIniKv, key) VF_FIELD(VfIniKv, key_len)
        VF_FIELD(VfIniKv, value) VF_FIELD(VfIniKv, value_len)
        VF_LAYOUT_BEGIN(VfGpuAdapterInput)
        VF_FIELD(VfGpuAdapterInput, vendor_id) VF_FIELD(VfGpuAdapterInput, device_id)
        VF_FIELD(VfGpuAdapterInput, dedicated_vram_mb) VF_FIELD(VfGpuAdapterInput, shared_sys_mb)
        VF_FIELD(VfGpuAdapterInput, dxcore_available) VF_FIELD(VfGpuAdapterInput, dxcore_integrated)
        VF_FIELD(VfGpuAdapterInput, dxcore_detachable) VF_FIELD(VfGpuAdapterInput, removal_detachable)
        VF_FIELD(VfGpuAdapterInput, name_ptr) VF_FIELD(VfGpuAdapterInput, name_len)
        VF_LAYOUT_BEGIN(VfDeviceProfileInput)
        VF_FIELD(VfDeviceProfileInput, total_ram_mb) VF_FIELD(VfDeviceProfileInput, physical_cores)
        VF_FIELD(VfDeviceProfileInput, cpu_family) VF_FIELD(VfDeviceProfileInput, cpu_model)
        VF_FIELD(VfDeviceProfileInput, user_gpu_pref) VF_FIELD(VfDeviceProfileInput, system_arch)
        VF_FIELD(VfDeviceProfileInput, power_known) VF_FIELD(VfDeviceProfileInput, has_battery)
        VF_FIELD(VfDeviceProfileInput, ac_online) VF_FIELD(VfDeviceProfileInput, battery_saver)
        VF_FIELD(VfDeviceProfileInput, dxcore_available) VF_FIELD(VfDeviceProfileInput, ram_probe_ok)
        VF_FIELD(VfDeviceProfileInput, experimental_egpu_streaming) VF_FIELD(VfDeviceProfileInput, override_arch_gen)
        VF_LAYOUT_BEGIN(VfDeviceProfileDecision)
        VF_FIELD(VfDeviceProfileDecision, selected_gpu_index) VF_FIELD(VfDeviceProfileDecision, gpu_probe_ok)
        VF_FIELD(VfDeviceProfileDecision, gpu_connection) VF_FIELD(VfDeviceProfileDecision, gpu_arch_gen)
        VF_FIELD(VfDeviceProfileDecision, apu_tier) VF_FIELD(VfDeviceProfileDecision, apply_visual_cuts)
        VF_FIELD(VfDeviceProfileDecision, apply_uma_memory_guard) VF_FIELD(VfDeviceProfileDecision, apply_uma_vram_reg_override)
        VF_FIELD(VfDeviceProfileDecision, allow_ram_purge) VF_FIELD(VfDeviceProfileDecision, force_high_perf_power_plan)
        VF_FIELD(VfDeviceProfileDecision, block_driver_tweaks) VF_FIELD(VfDeviceProfileDecision, apply_egpu_streaming_tweaks)
        VF_FIELD(VfDeviceProfileDecision, safe_vram_pool_mb) VF_FIELD(VfDeviceProfileDecision, amd_tessellation_cap)
        VF_FIELD(VfDeviceProfileDecision, dxvk_compiler_threads) VF_FIELD(VfDeviceProfileDecision, disable_dx12_async_compute) VF_FIELD(VfDeviceProfileDecision, reasons)
        VF_LAYOUT_BEGIN(VfProcessCategoryActions)
        VF_FIELD(VfProcessCategoryActions, recording_streaming) VF_FIELD(VfProcessCategoryActions, calls_voice)
        VF_FIELD(VfProcessCategoryActions, messaging) VF_FIELD(VfProcessCategoryActions, browsers)
        VF_FIELD(VfProcessCategoryActions, stores_launchers) VF_FIELD(VfProcessCategoryActions, music)
        VF_FIELD(VfProcessCategoryActions, cloud_sync)
        VF_LAYOUT_BEGIN(VfProcessDecision)
        VF_FIELD(VfProcessDecision, process_class) VF_FIELD(VfProcessDecision, action)
        VF_FIELD(VfProcessDecision, protected_by_anticheat) VF_FIELD(VfProcessDecision, path_verified)
        VF_LAYOUT_BEGIN(VfProcessPolicyConfig)
        VF_FIELD(VfProcessPolicyConfig, category_actions) VF_FIELD(VfProcessPolicyConfig, heavy_work_as_game)
        VF_LAYOUT_BEGIN(VfProcessExceptionEntry)
        VF_FIELD(VfProcessExceptionEntry, exe_ptr) VF_FIELD(VfProcessExceptionEntry, exe_len)
        VF_FIELD(VfProcessExceptionEntry, action)
        VF_LAYOUT_BEGIN(VfProcessClassifyInput)
        VF_FIELD(VfProcessClassifyInput, full_path_ptr) VF_FIELD(VfProcessClassifyInput, full_path_len)
        VF_FIELD(VfProcessClassifyInput, exe_name_ptr) VF_FIELD(VfProcessClassifyInput, exe_name_len)
        VF_FIELD(VfProcessClassifyInput, category_actions) VF_FIELD(VfProcessClassifyInput, exceptions_ptr)
        VF_FIELD(VfProcessClassifyInput, exceptions_count) VF_FIELD(VfProcessClassifyInput, custom_games_nul_ptr)
        VF_FIELD(VfProcessClassifyInput, custom_games_nul_len) VF_FIELD(VfProcessClassifyInput, excluded_games_nul_ptr)
        VF_FIELD(VfProcessClassifyInput, excluded_games_nul_len) VF_FIELD(VfProcessClassifyInput, heavy_work_as_game)
        VF_FIELD(VfProcessClassifyInput, is_foreground_3d) VF_FIELD(VfProcessClassifyInput, has_anticheat_signal)
        VF_LAYOUT_BEGIN(VfProcessTreeNode)
        VF_FIELD(VfProcessTreeNode, pid) VF_FIELD(VfProcessTreeNode, parent_pid)
        VF_FIELD(VfProcessTreeNode, creation_time) VF_FIELD(VfProcessTreeNode, process_class)
        VF_FIELD(VfProcessTreeNode, action) VF_FIELD(VfProcessTreeNode, has_user_exception)
        VF_FIELD(VfProcessTreeNode, has_anticheat_signal)
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
            VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB, VF_CONFIG_KEY_MIN_STANDBY_MB, VF_CONFIG_KEY_COOLDOWN_SECONDS,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, VF_CONFIG_KEY_CUSTOM_GAME_EXES, VF_CONFIG_KEY_MEMORY,
            VF_CONFIG_KEY_HEAVY_WORK_AS_GAME, VF_CONFIG_KEY_PROCESS_CATEGORY_ACTIONS,
            VF_CONFIG_KEY_PROCESS_EXCEPTIONS, VF_CONFIG_KEY_EXCLUDED_GAME_EXES,
            VF_CONFIG_SAVE_OK, VF_CONFIG_SAVE_BLOCKED, VF_CONFIG_SAVE_INVALID, VF_SID_OTHER, VF_SID_SYSTEM,
            VF_SID_ADMINISTRATORS, VF_SID_TRUSTED_INSTALLER,
            VF_INI_OK, VF_INI_ERR_INVALID_INPUT, VF_INI_ERR_ENCODING, VF_INI_ERR_MISSING_END_MARKER,
            VF_INI_ERR_UNMATCHED_END_MARKER,
            VF_GPU_CONN_UNKNOWN, VF_GPU_CONN_INTEGRATED, VF_GPU_CONN_DISCRETE, VF_GPU_CONN_EXTERNAL,
            VF_DISK_UNKNOWN, VF_DISK_HDD, VF_DISK_SSD, VF_DISK_NVME,
            VF_ARCH_UNKNOWN, VF_ARCH_X64, VF_ARCH_ARM64_EMULATED, VF_ARCH_ARM64_NATIVE, VF_ARCH_X86,
            VF_GPU_PREF_DEFAULT, VF_GPU_PREF_POWER_SAVING, VF_GPU_PREF_HIGH_PERF,
            VF_PROC_CLASS_UNKNOWN, VF_PROC_CLASS_WINDOWS_SYSTEM, VF_PROC_CLASS_RECORDING_STREAMING,
            VF_PROC_CLASS_CALLS_VOICE, VF_PROC_CLASS_MESSAGING, VF_PROC_CLASS_BROWSERS,
            VF_PROC_CLASS_STORES_LAUNCHERS, VF_PROC_CLASS_MUSIC, VF_PROC_CLASS_CLOUD_SYNC,
            VF_PROC_CLASS_DEVELOPER_IDE, VF_PROC_CLASS_HEAVY_WORK, VF_PROC_CLASS_POSSIBLE_GAME,
            VF_PROC_ACTION_DO_NOT_TOUCH, VF_PROC_ACTION_ECO_ONLY,
            VF_PROC_ACTION_THROTTLE, VF_PROC_ACTION_BOOST_AS_GAME};
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
