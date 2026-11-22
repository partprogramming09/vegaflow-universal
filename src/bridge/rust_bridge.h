#ifndef VEGAFLOW_CORE_RUST_BRIDGE_H
#define VEGAFLOW_CORE_RUST_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VF_MAX_LOGICAL_PROCESSORS 64

typedef struct VfMemoryPurgeInput {
    uint64_t total_physical_bytes;
    uint64_t free_and_zero_bytes;
    uint64_t standby_bytes;
    uint64_t local_budget_bytes;
    // E2.3: global GPU usage from PDH (all processes); DXGI CurrentUsage is per process and no longer an input.
    uint64_t gpu_shared_usage_bytes;
    uint64_t gpu_dedicated_usage_bytes;
    bool vram_valid;
    bool memory_lists_valid;
    bool is_uma_gpu;
    bool allow_ram_purge;
    uint64_t min_free_ram_mb;
    uint64_t min_standby_mb;
    uint32_t cooldown_seconds;
    int64_t elapsed_since_last_purge_ms;
    uint64_t game_disk_read_bytes_per_sec;
    uint64_t max_game_disk_read_bytes_per_sec;
    double pressure_threshold_override;
    uint32_t prev_interventions;
    uint8_t last_purge_level;  // E2.3: level applied earlier in this pressure episode (0 none, 1 low, 2 full)
    bool dry_run;
} VfMemoryPurgeInput;

// E2.3: purge levels carried by VfMemoryPurgeDecision.purge_level
#define VF_PURGE_LEVEL_NONE 0
#define VF_PURGE_LEVEL_LOW 1   // NT command 5: low-priority standby list + background trim
#define VF_PURGE_LEVEL_FULL 2  // NT command 4: whole standby list + background trim

typedef struct VfMemoryPurgeDecision {
    uint8_t purge_level;  // E2.3: replaces should_purge_standby
    bool should_trim_background;
    bool skipped_due_to_cooldown;
    bool skipped_due_to_disk_io;
    uint32_t cooldown_remaining_seconds;
    bool guard_triggered;
    bool dxgi_over_ceiling;
    bool free_ram_critical;
    bool memory_pressure;  // E2.3: pressure exists even if cooldown or disk I/O held the action back
    double dxgi_budget_utilization;
    uint64_t available_os_headroom_mb;
    uint32_t total_interventions;
} VfMemoryPurgeDecision;

typedef struct VfCpuCoreEntry {
    uint32_t cpu_set_id;
    uint32_t core_index;
    uint32_t logical_processor_index;
    uint16_t group;
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

double vf_rust_vram_pressure_threshold(void);

VfMemoryPurgeDecision vf_rust_evaluate_memory_purge(
    const VfMemoryPurgeInput* input
);

bool vf_rust_is_factory_memory_protected_exe(
    const char* utf8_name,
    size_t len
);

bool vf_rust_partition_cpu_topology(
    const VfCpuCoreEntry* entries,
    uint32_t count,
    VfTopologyPartitionOut* out
);

bool vf_rust_partition_cpu_topology_group(
    const VfCpuCoreEntry* entries,
    uint32_t count,
    uint16_t target_group,
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
#define VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS   (1u << 2)
#define VF_CONFIG_KEY_MIN_FREE_RAM_MB          (1u << 3)
#define VF_CONFIG_KEY_MIN_STANDBY_MB           (1u << 4)
#define VF_CONFIG_KEY_COOLDOWN_SECONDS         (1u << 5)
#define VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S  (1u << 6)
#define VF_CONFIG_KEY_CUSTOM_GAME_EXES         (1u << 7)
#define VF_CONFIG_KEY_MEMORY                   (1u << 8)
#define VF_CONFIG_KEY_HEAVY_WORK_AS_GAME       (1u << 9)
#define VF_CONFIG_KEY_PROCESS_CATEGORY_ACTIONS (1u << 10)
#define VF_CONFIG_KEY_PROCESS_EXCEPTIONS       (1u << 11)
#define VF_CONFIG_KEY_EXCLUDED_GAME_EXES       (1u << 12)
/* E2.2 */
#define VF_CONFIG_KEY_AUTO_VRAM_REPORTING      (1u << 13)
#define VF_CONFIG_KEY_AUTO_PRIORITY_SEPARATION (1u << 14)
#define VF_CONFIG_KEY_AUTO_GAME_DVR_OFF        (1u << 15)
#define VF_CONFIG_KEY_AUTO_POWER_PROFILE       (1u << 16)
#define VF_CONFIG_KEY_AUTO_ENGINE_PRESETS      (1u << 17) /* F2.3 */
/* Results of vf_rust_config_save. */
#define VF_CONFIG_SAVE_OK       0u
#define VF_CONFIG_SAVE_BLOCKED  1u /* the current file has a VF_CONFIG_BLOCKS_SAVE status */
#define VF_CONFIG_SAVE_INVALID  2u /* a setting or game name breaks its rule, or malformed arguments */

/* Process classification & actions (policy/process_class.rs). */
#define VF_PROC_CLASS_UNKNOWN              0u
#define VF_PROC_CLASS_WINDOWS_SYSTEM       1u
#define VF_PROC_CLASS_RECORDING_STREAMING  2u
#define VF_PROC_CLASS_CALLS_VOICE          3u
#define VF_PROC_CLASS_MESSAGING            4u
#define VF_PROC_CLASS_BROWSERS             5u
#define VF_PROC_CLASS_STORES_LAUNCHERS     6u
#define VF_PROC_CLASS_MUSIC                7u
#define VF_PROC_CLASS_CLOUD_SYNC           8u
#define VF_PROC_CLASS_DEVELOPER_IDE        9u
#define VF_PROC_CLASS_HEAVY_WORK           10u
#define VF_PROC_CLASS_POSSIBLE_GAME        11u

#define VF_PROC_ACTION_DO_NOT_TOUCH        0u
#define VF_PROC_ACTION_ECO_ONLY            1u
#define VF_PROC_ACTION_THROTTLE            2u
#define VF_PROC_ACTION_BOOST_AS_GAME       3u

typedef struct VfProcessCategoryActions {
    uint8_t recording_streaming;
    uint8_t calls_voice;
    uint8_t messaging;
    uint8_t browsers;
    uint8_t stores_launchers;
    uint8_t music;
    uint8_t cloud_sync;
} VfProcessCategoryActions;

typedef struct VfProcessDecision {
    uint8_t process_class;
    uint8_t action;
    bool protected_by_anticheat;
    bool path_verified;
} VfProcessDecision;

typedef struct VfProcessPolicyConfig {
    VfProcessCategoryActions category_actions;
    bool heavy_work_as_game;
} VfProcessPolicyConfig;

typedef struct VfProcessExceptionEntry {
    const uint8_t* exe_ptr;
    size_t exe_len;
    uint8_t action;
} VfProcessExceptionEntry;

typedef struct VfProcessClassifyInput {
    const uint8_t* full_path_ptr;
    size_t full_path_len;
    const uint8_t* exe_name_ptr;
    size_t exe_name_len;
    VfProcessCategoryActions category_actions;
    const VfProcessExceptionEntry* exceptions_ptr;
    size_t exceptions_count;
    const uint8_t* custom_games_nul_ptr;
    size_t custom_games_nul_len;
    const uint8_t* excluded_games_nul_ptr;
    size_t excluded_games_nul_len;
    bool heavy_work_as_game;
    bool is_foreground_3d;
    bool has_anticheat_signal;
} VfProcessClassifyInput;

/* Every setting of config.json; the game names travel apart. */
typedef struct VfConfigSettings {
    uint32_t poll_interval_ms;
    uint32_t cooldown_seconds;
    bool restrict_game_cpu_sets;
    uint64_t min_free_ram_mb;
    uint64_t min_standby_mb;
    uint64_t max_game_disk_read_mb_s;
    bool auto_vram_reporting;        /* E2.2: opt-in tweaks the daemon applies by itself when true; false in a factory file */
    bool auto_priority_separation;
    bool auto_game_dvr_off;
    bool auto_power_profile;
    bool auto_engine_presets;        /* F2.3: the Unreal Engine preset written to Engine.ini */
} VfConfigSettings;

typedef struct VfConfigLoad {
    VfConfigSettings settings;
    uint32_t status;        /* VF_CONFIG_* bits */
    uint32_t rejected_keys; /* VF_CONFIG_KEY_* bits */
} VfConfigLoad;

/* Called once per string handed back: UTF-8, not NUL-terminated, valid only during the call. */
typedef void (*VfUtf8Fn)(void* ctx, const char* utf8, size_t len);
typedef void (*VfProcessExceptionFn)(void* ctx, const uint8_t* exe, size_t len, uint8_t action);

/* Reads the text of config.json into `out` and calls on_game_exe once per accepted game name (may be null).
   Returns false only for bad pointers. */
bool vf_rust_config_load(const char* bytes, size_t len, VfConfigLoad* out, VfUtf8Fn on_game_exe, void* ctx);

/* Extended config.json load including Phase 5 process policy, excluded games, and per-exe exceptions. */
bool vf_rust_config_load_ex(const char* bytes, size_t len, VfConfigLoad* out, VfProcessPolicyConfig* out_policy,
                            VfUtf8Fn on_game_exe, VfUtf8Fn on_excluded_game_exe, VfProcessExceptionFn on_exception,
                            void* ctx);

/* The new text of config.json: `original` (the current file) with the settings replaced, unknown keys kept and
   "version" set to the current one. `game_exes` holds each name followed by a NUL. On VF_CONFIG_SAVE_OK calls on_text
   once with the whole new file; otherwise nothing is called. */
uint32_t vf_rust_config_save(const char* original, size_t original_len, const VfConfigSettings* settings,
                             const char* game_exes, size_t game_exes_len, VfUtf8Fn on_text, void* ctx);

/* Extended config.json save including Phase 5 process policy, excluded games, and per-exe exceptions. */
uint32_t vf_rust_config_save_ex(const char* original, size_t original_len, const VfConfigSettings* settings,
                                const VfProcessPolicyConfig* policy, const uint8_t* game_exes, size_t game_exes_len,
                                const uint8_t* excluded_game_exes, size_t excluded_game_exes_len,
                                const VfProcessExceptionEntry* exceptions, size_t exceptions_count, VfUtf8Fn on_text,
                                void* ctx);

/* Pure process classification and action decision (policy/process_class.rs). */
bool vf_rust_classify_process(const VfProcessClassifyInput* input, VfProcessDecision* out);

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

/* Game INI / Unity boot.config editing (policy/ini_merge.rs). */
#define VF_INI_OK                       0u
#define VF_INI_ERR_INVALID_INPUT        1u
#define VF_INI_ERR_ENCODING             2u
#define VF_INI_ERR_MISSING_END_MARKER   3u
#define VF_INI_ERR_UNMATCHED_END_MARKER 4u

typedef void (*VfBytesFn)(void* ctx, const uint8_t* data, size_t len);
typedef void (*VfKvFn)(void* ctx, const char* key, size_t key_len, const char* val, size_t val_len);

typedef struct VfIniKv {
    const char* key;
    size_t key_len;
    const char* value;
    size_t value_len;
} VfIniKv;

uint32_t vf_rust_ini_merge_unreal(const uint8_t* original, size_t original_len, const char* block, size_t block_len,
                                  VfBytesFn on_out, void* ctx);
uint32_t vf_rust_ini_remove_unreal(const uint8_t* original, size_t original_len, bool* out_changed,
                                   VfBytesFn on_out, void* ctx);
uint32_t vf_rust_ini_merge_unity(const uint8_t* original, size_t original_len, const VfIniKv* pairs, size_t pair_count,
                                 VfBytesFn on_out, void* ctx);
uint32_t vf_rust_ini_apply_section(const uint8_t* original, size_t original_len, const char* section, size_t section_len,
                                   const VfIniKv* pairs, size_t pair_count, VfBytesFn on_out, void* ctx);
uint32_t vf_rust_ini_remove_section_keys(const uint8_t* original, size_t original_len, const char* section, size_t section_len,
                                         const VfIniKv* keys, size_t key_count, bool* out_changed,
                                         VfBytesFn on_out, void* ctx);
uint32_t vf_rust_ini_query_section(const uint8_t* original, size_t original_len, const char* section, size_t section_len,
                                   VfKvFn on_kv, void* ctx);

/* Device profile and GPU classification (policy/device_profile.rs). */
#define VF_GPU_CONN_UNKNOWN      0u
#define VF_GPU_CONN_INTEGRATED   1u
#define VF_GPU_CONN_DISCRETE     2u
#define VF_GPU_CONN_EXTERNAL     3u

#define VF_DISK_UNKNOWN          0u
#define VF_DISK_HDD              1u
#define VF_DISK_SSD              2u
#define VF_DISK_NVME             3u

#define VF_ARCH_UNKNOWN          0u
#define VF_ARCH_X64              1u
#define VF_ARCH_ARM64_EMULATED   2u
#define VF_ARCH_ARM64_NATIVE     3u
#define VF_ARCH_X86              4u

#define VF_GPU_PREF_DEFAULT      0u
#define VF_GPU_PREF_POWER_SAVING 1u
#define VF_GPU_PREF_HIGH_PERF    2u

typedef struct VfGpuAdapterInput {
    uint32_t vendor_id;
    uint32_t device_id;
    uint64_t dedicated_vram_mb;
    uint64_t shared_sys_mb;
    bool dxcore_available;
    bool dxcore_integrated;
    bool dxcore_detachable;
    bool removal_detachable;
    const uint8_t* name_ptr;
    size_t name_len;
} VfGpuAdapterInput;

typedef struct VfDeviceProfileInput {
    uint64_t total_ram_mb;
    uint32_t physical_cores;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint8_t user_gpu_pref;
    uint8_t system_arch;
    bool power_known;
    bool has_battery;
    bool ac_online;
    bool battery_saver;
    bool dxcore_available;
    bool ram_probe_ok;
    bool experimental_egpu_streaming;
    uint8_t override_arch_gen;
} VfDeviceProfileInput;

typedef struct VfDeviceProfileDecision {
    uint32_t selected_gpu_index;
    bool gpu_probe_ok;
    uint8_t gpu_connection;
    uint8_t gpu_arch_gen;
    uint8_t apu_tier;
    bool apply_visual_cuts;
    bool apply_uma_memory_guard;
    bool apply_uma_vram_reg_override;
    bool allow_ram_purge;
    bool force_high_perf_power_plan;
    bool block_driver_tweaks;
    bool apply_egpu_streaming_tweaks;
    uint32_t safe_vram_pool_mb;
    uint32_t amd_tessellation_cap;
    uint32_t dxvk_compiler_threads;
    bool disable_dx12_async_compute;
    uint64_t reasons;                /* F2.7: one VF_REASON_* bit per rule that decided the fields above (device_profile.rs) */
} VfDeviceProfileDecision;

/* Calls on_line once per bit set in `mask`, in bit order, with the plain sentence of that rule (UTF-8, not NUL-ended).
   Bits with no rule are skipped. Returns how many lines were sent. */
uint32_t vf_rust_decision_reason_texts(uint64_t mask, VfUtf8Fn on_line, void* ctx);

bool vf_rust_evaluate_device_profile(
    const VfGpuAdapterInput* adapters,
    size_t adapter_count,
    const VfDeviceProfileInput* input,
    VfDeviceProfileDecision* out
);

/* Layout check for the structs above (this header is written by hand). Writes, for each struct in the order
   VfMemoryPurgeInput, VfMemoryPurgeDecision, VfCpuCoreEntry, VfTopologyPartitionOut, VfTierInput, VfAllowedRoot,
   VfConfigSettings, VfConfigLoad, VfAceEntry, VfIniKv, VfGpuAdapterInput, VfDeviceProfileInput,
   VfDeviceProfileDecision, VfProcessCategoryActions, VfProcessDecision, VfProcessPolicyConfig,
   VfProcessExceptionEntry, VfProcessClassifyInput (the order of vf_rust_bridge_layout in rust/vegaflow-core/src/ffi.rs): its size, its alignment and the offset and size of each field in declaration order. Writes at most `cap`
   numbers and returns how many there are in total. Checked by C++ Test 34. */
size_t vf_rust_bridge_layout(size_t* out, size_t cap);

/* The #defines above that Rust also defines, in this order: VF_CONFIG_CORRUPT .. VF_CONFIG_EMPTY, VF_CONFIG_BLOCKS_SAVE,
   VF_CONFIG_KEY_VERSION .. VF_CONFIG_KEY_MEMORY, VF_CONFIG_SAVE_OK .. VF_CONFIG_SAVE_INVALID, VF_SID_OTHER ..
   VF_SID_TRUSTED_INSTALLER, VF_INI_OK .. VF_INI_ERR_UNMATCHED_END_MARKER, VF_GPU_CONN_UNKNOWN .. VF_GPU_PREF_HIGH_PERF. Writes at most `cap` and returns how many there are. Checked by C++ Test 34. */
size_t vf_rust_bridge_constants(uint32_t* out, size_t cap);

/* E2.5 --doctor (rust/vegaflow-core/src/ffi/doctor.rs). Plain arrays, no structs. */

/* 0 unknown (probe failed), 1 ok, 2 low, 3 critical. */
uint8_t vf_rust_doctor_disk_advice(uint64_t free_bytes, uint64_t total_bytes);

/* kind_exists[i]: 0 AMD, 1 NVIDIA, 2 generic D3D, plus 0x80 when the folder exists. `disk` is the value above. Writes up
   to cap/2 pairs (severity 0 info / 1 warn, code 1 no_cache_dirs, 2 vendor_cache_missing, 3 empty_cache_low_disk,
   4 cache_over_10gib) and returns how many findings there are. */
size_t vf_rust_doctor_cache_findings(const uint8_t* kind_exists, const uint64_t* bytes, size_t count, uint8_t disk,
                                     uint8_t* out, size_t cap);

/* names_nul: process names or paths in UTF-8, each ended by NUL. Writes up to cap indexes of OEM tools (each once);
   returns how many there are. */
size_t vf_rust_doctor_oem_conflicts(const uint8_t* names_nul, size_t len, uint32_t* out, size_t cap);

/* "product\tconflicts" of an OEM tool, UTF-8, no NUL. Returns the full length, 0 for an unknown index. */
size_t vf_rust_doctor_oem_info(uint32_t index, uint8_t* buf, size_t cap);

/* True when the bytes are one well-formed JSON value. */
bool vf_rust_doctor_json_is_valid(const uint8_t* bytes, size_t len);

/* E2.2 memory compression advice (policy/memory_compression.rs): 0 allowed, 1 discouraged, 2 unknown (probe failed or 0 MB).
   Discouraged at 16384 MB or less. Vectors: tests/vectors/memory_compression.tsv. */
uint8_t vf_rust_memory_compression_off_advice(uint64_t total_ram_mb, bool probe_ok);

// E2.7
/* True when the config.json number with this VF_CONFIG_KEY_* bit takes effect while VegaFlow runs; false (read once at
   start: Configuración says "restart") for the five numbers and for an unknown bit. */
bool vf_rust_config_number_applies_live(uint32_t key_bit);

#ifdef __cplusplus
}
#endif

#endif // VEGAFLOW_CORE_RUST_BRIDGE_H
