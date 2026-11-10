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
    VF_PATH_NOT_LOCAL_PATH = 11 /* device or UNC prefix, relative, ":" stream, or not canonical: "/", empty name, name ending in "." or space, NUL */
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

/* Steam's libraryfolders.vdf: calls on_path(ctx, path, len) for each library folder listed (absolute drive paths only,
   UTF-8, not NUL-terminated, valid only during the call) and returns how many there were. on_path may be null. */
typedef void (*VfSteamLibraryFn)(void* ctx, const char* utf8_path, size_t len);
size_t vf_rust_steam_library_paths(const char* vdf, size_t len, VfSteamLibraryFn on_path, void* ctx);

/* config.json (policy/config_file.rs). Status bits of VfConfigLoad.status: */
#define VF_CONFIG_CORRUPT        (1u << 0) /* not JSON or not an object: every setting at its factory value */
#define VF_CONFIG_KEY_REJECTED   (1u << 1) /* some keys broke their rule and took their factory value */
#define VF_CONFIG_NEWER_VERSION  (1u << 2) /* written by a newer VegaFlow: read, never overwritten */
#define VF_CONFIG_OLD_VERSION    (1u << 3) /* no "version": the format before phase 1b; saving updates it */
#define VF_CONFIG_EMPTY          (1u << 4) /* empty or only whitespace: factory values, may be saved */
#define VF_CONFIG_BLOCKS_SAVE    (VF_CONFIG_CORRUPT | VF_CONFIG_KEY_REJECTED | VF_CONFIG_NEWER_VERSION)
/* Bits of VfConfigLoad.rejected_keys, one per key. */
#define VF_CONFIG_KEY_VERSION                  (1u << 0)
#define VF_CONFIG_KEY_POLL_INTERVAL_MS         (1u << 1)
#define VF_CONFIG_KEY_TIMER_RESOLUTION_NS100   (1u << 2)
#define VF_CONFIG_KEY_TIMER_FORCED_MANUAL      (1u << 3)
#define VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS   (1u << 4)
#define VF_CONFIG_KEY_MIN_FREE_RAM_MB          (1u << 5)
#define VF_CONFIG_KEY_MIN_STANDBY_MB           (1u << 6)
#define VF_CONFIG_KEY_COOLDOWN_SECONDS         (1u << 7)
#define VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S  (1u << 8)
#define VF_CONFIG_KEY_CUSTOM_GAME_EXES         (1u << 9)
#define VF_CONFIG_KEY_MEMORY                   (1u << 10)
/* Results of vf_rust_config_save. */
#define VF_CONFIG_SAVE_OK       0u
#define VF_CONFIG_SAVE_BLOCKED  1u /* the current file has a VF_CONFIG_BLOCKS_SAVE status */
#define VF_CONFIG_SAVE_INVALID  2u /* a setting or game name breaks its rule, or malformed arguments */

/* Every setting of config.json; the game names travel apart. */
typedef struct VfConfigSettings {
    uint32_t poll_interval_ms;
    uint32_t timer_resolution_ns100;
    uint32_t cooldown_seconds;
    bool timer_forced_manual;
    bool restrict_game_cpu_sets;
    uint64_t min_free_ram_mb;
    uint64_t min_standby_mb;
    uint64_t max_game_disk_read_mb_s;
} VfConfigSettings;

typedef struct VfConfigLoad {
    VfConfigSettings settings;
    uint32_t status;        /* VF_CONFIG_* bits */
    uint32_t rejected_keys; /* VF_CONFIG_KEY_* bits */
} VfConfigLoad;

/* Called once per string handed back: UTF-8, not NUL-terminated, valid only during the call. */
typedef void (*VfUtf8Fn)(void* ctx, const char* utf8, size_t len);

/* Reads the text of config.json into `out` and calls on_game_exe once per accepted game name (may be null).
   Returns false only for bad pointers. */
bool vf_rust_config_load(const char* bytes, size_t len, VfConfigLoad* out, VfUtf8Fn on_game_exe, void* ctx);

/* The new text of config.json: `original` (the current file) with the settings replaced, unknown keys kept and
   "version" set to the current one. `game_exes` holds each name followed by a NUL. On VF_CONFIG_SAVE_OK calls on_text
   once with the whole new file; otherwise nothing is called. */
uint32_t vf_rust_config_save(const char* original, size_t original_len, const VfConfigSettings* settings,
                             const char* game_exes, size_t game_exes_len, VfUtf8Fn on_text, void* ctx);

/* The rule for a name in custom_game_exes (also applied to names found in the Steam library): a bare .exe file name
   that Windows reads as written, not a device name and not a Windows process. UTF-8; false for invalid UTF-8. */
bool vf_rust_is_allowed_game_exe(const char* utf8_name, size_t len);

/* Who a SID is, for vf_rust_only_admins_can_write; any other number counts as another account. */
#define VF_SID_OTHER              0
#define VF_SID_SYSTEM             1
#define VF_SID_ADMINISTRATORS     2
#define VF_SID_TRUSTED_INSTALLER  3

/* One DACL entry. */
typedef struct VfAceEntry {
    uint32_t mask;
    uint8_t sid_kind;  /* VF_SID_* */
    bool allows;       /* any allow type; deny entries only take rights away */
    bool inherit_only; /* applies only to children */
} VfAceEntry;

/* True when only SYSTEM, Administrators and TrustedInstaller can change the object: the owner is one of them, the
   DACL is not NULL (has_dacl) and no entry gives another account a right to write, delete or change permissions. */
bool vf_rust_only_admins_can_write(bool has_dacl, uint8_t owner_kind, const VfAceEntry* aces, size_t count);

/* Layout check for the structs above (this header is written by hand). Writes, for each struct in the order
   VfUmaTelemetryInput, VfUmaGuardDecision, VfNtMemoryListSample, VfCpuCoreEntry, VfTopologyPartitionOut,
   VfTierInput, VfAllowedRoot, VfConfigSettings, VfConfigLoad, VfAceEntry: its size, its alignment and the offset and size of each field in declaration order. Writes at most `cap`
   numbers and returns how many there are in total. Checked by C++ Test 34. */
size_t vf_rust_bridge_layout(size_t* out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif // VEGAFLOW_CORE_RUST_BRIDGE_H
