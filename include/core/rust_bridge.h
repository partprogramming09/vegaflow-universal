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

#ifdef __cplusplus
}
#endif

#endif // VEGAFLOW_CORE_RUST_BRIDGE_H
