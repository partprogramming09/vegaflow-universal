#ifndef VEGAFLOW_CORE_RUST_BRIDGE_H
#define VEGAFLOW_CORE_RUST_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VF_MAX_LOGICAL_PROCESSORS 64

typedef struct VfUmaTelemetryInput {
    uint64_t total_physical_bytes;
    uint64_t free_and_zero_bytes;
    uint64_t standby_bytes;
    uint64_t local_budget_bytes;
    uint64_t local_usage_bytes;
    double runtime_pressure_threshold;
    bool vram_valid;
    bool is_discrete_tier;
} VfUmaTelemetryInput;

typedef struct VfUmaGuardDecision {
    uint64_t available_os_headroom_mb;
    double dxgi_budget_utilization;
    bool guard_triggered;
    bool dxgi_over_ceiling;
    bool free_ram_critical;
    uint32_t total_interventions;
} VfUmaGuardDecision;

typedef struct VfNtMemoryListSample {
    uint64_t free_and_zero_bytes;
    uint64_t standby_bytes;
    uint64_t modified_bytes;
    bool valid;
} VfNtMemoryListSample;

typedef struct VfCpuCoreEntry {
    uint32_t cpu_set_id;
    uint32_t core_index;
    uint32_t logical_processor_index;
    uint8_t last_level_cache_index;
    uint8_t efficiency_class;
} VfCpuCoreEntry;

typedef struct VfTopologyPartitionOut {
    uint32_t game_cpu_set_ids[VF_MAX_LOGICAL_PROCESSORS];
    uint32_t game_count;
    uint32_t background_cpu_set_ids[VF_MAX_LOGICAL_PROCESSORS];
    uint32_t background_count;
    uint32_t physical_cores;
    uint32_t logical_processors;
    bool has_multiple_ccx;
    bool has_hybrid_cores;
} VfTopologyPartitionOut;

VfUmaGuardDecision vf_rust_evaluate_uma_pressure(
    const VfUmaTelemetryInput* input,
    uint32_t prev_interventions
);

VfNtMemoryListSample vf_rust_query_nt_memory_lists(void);

bool vf_rust_purge_nt_standby_list(bool dry_run);

bool vf_rust_partition_cpu_topology(
    const VfCpuCoreEntry* entries,
    uint32_t count,
    VfTopologyPartitionOut* out
);

bool vf_rust_eval_bg_confinement(
    double logical_cores_used,
    bool currently_confined
);

bool vf_rust_has_anticheat_signature(
    const char* utf8_path,
    size_t len
);

bool vf_rust_is_critical_system_filename(
    const char* utf8_name,
    size_t len
);

typedef struct VfTierInput {
    uint64_t total_ram_mb;
    uint32_t physical_cores; /* 0 = unknown */
    uint8_t arch_gen;        /* GpuArchitectureGen as a number */
    bool dedicated_vram;
    bool is_nvidia;
} VfTierInput;

/* Returns the APU tier: 1 entry, 2 mid, 3 advanced, 4 discrete or custom. */
uint8_t vf_rust_classify_apu_tier(const VfTierInput* input);

/* SandboxGuard verdicts: 1-8 come from the blacklist, 9-11 from the rest of vf_rust_decide_file_access. */
enum VfPathVerdict {
    VF_PATH_ALLOWED = 0,
    VF_PATH_EMPTY = 1,
    VF_PATH_DRIVE_ROOT = 2,
    VF_PATH_CRITICAL_FILE = 3,
    VF_PATH_WINDOWS_DIR = 4,
    VF_PATH_WINDOWS_OTHER_DRIVE = 5,
    VF_PATH_COMMON_FILES = 6,
    VF_PATH_SYSTEM_VOLUME_INFORMATION = 7,
    VF_PATH_INTERNAL_ERROR = 8, /* the Rust classifier failed: refuse the path */
    VF_PATH_ANTICHEAT = 9,
    VF_PATH_NOT_WHITELISTED = 10,
    VF_PATH_NOT_LOCAL_PATH = 11 /* device or UNC prefix, relative, "." or ".." segment, or a ":" stream */
};

/* Classifies a canonical path against the blacklist. The path and both directories are UTF-16 and must already be
   lower-cased by the caller (towlower); the verdict is VF_PATH_ALLOWED when no rule matches. */
uint32_t vf_rust_classify_blacklisted_path(
    const uint16_t* path, size_t path_len,
    const uint16_t* win_dir, size_t win_dir_len,
    const uint16_t* sys_dir, size_t sys_dir_len
);

/* One allowed folder for vf_rust_decide_file_access: UTF-16, lower-cased, not owned by Rust. */
typedef struct VfAllowedRoot {
    const uint16_t* path;
    size_t len;
    bool writable; /* false: reading only */
} VfAllowedRoot;

/* SandboxGuard's whole decision: blacklist, plain local path, anti-cheat (writes only), then the allowed folders,
   compared folder by folder. Same string rules as vf_rust_classify_blacklisted_path; null roots allow nothing. */
uint32_t vf_rust_decide_file_access(
    const uint16_t* path, size_t path_len,
    const uint16_t* win_dir, size_t win_dir_len,
    const uint16_t* sys_dir, size_t sys_dir_len,
    bool write, bool anticheat,
    const VfAllowedRoot* roots, size_t root_count
);

/* Layout check for the structs above (this header is written by hand). Writes, for each struct in the order
   VfUmaTelemetryInput, VfUmaGuardDecision, VfNtMemoryListSample, VfCpuCoreEntry, VfTopologyPartitionOut,
   VfTierInput, VfAllowedRoot: its size, its alignment and the offset and size of each field in declaration order. Writes at most `cap`
   numbers and returns how many there are in total. Checked by C++ Test 34. */
size_t vf_rust_bridge_layout(size_t* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // VEGAFLOW_CORE_RUST_BRIDGE_H
