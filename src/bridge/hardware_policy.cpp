#include "bridge/hardware_policy.hpp"

#include "bridge/rust_bridge.h"
#include "ports/text_convert.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

namespace {

uint32_t VendorIdFromEnum(GpuVendor v) {
    switch (v) {
        case GpuVendor::AMD:    return 0x1002u;
        case GpuVendor::Intel:  return 0x8086u;
        case GpuVendor::NVIDIA: return 0x10DEu;
        default:                return 0u;
    }
}

const char* GuardModeForTierAndConn(ApuTier tier, GpuConnection conn) {
    if (conn == GpuConnection::External) return "eGPU Pass-Through Guard";
    if (conn == GpuConnection::Discrete || conn == GpuConnection::Unknown) return "Pass-Through Guard";
    switch (tier) {
        case ApuTier::Tier1_EntryUMA:   return "Survival Guard";
        case ApuTier::Tier2_MidVegaUMA: return "Sweet-Spot Guard";
        case ApuTier::Tier3_AdvancedUMA:return "Do-No-Harm Guard";
        case ApuTier::Tier4_DiscreteOrCustom:
        default:                        return "Pass-Through Guard";
    }
}

std::string TierLabelFor(ApuTier tier, GpuConnection conn) {
    if (conn == GpuConnection::External) return "Tier 4: External eGPU [Pass-Through Guard]";
    if (conn == GpuConnection::Unknown)  return "Tier 4: Unknown Connection [Prudent Pass-Through]";
    switch (tier) {
        case ApuTier::Tier1_EntryUMA:   return "Tier 1: Entry UMA [Survival Guard]";
        case ApuTier::Tier2_MidVegaUMA: return "Tier 2: Mid Vega UMA [Sweet-Spot Guard]";
        case ApuTier::Tier3_AdvancedUMA:return "Tier 3: Advanced UMA [Do-No-Harm Guard]";
        case ApuTier::Tier4_DiscreteOrCustom:
        default:                        return "Tier 4: Discrete GPU / Custom [Pass-Through Guard]";
    }
}

} // namespace

uint8_t SystemArchToFfi(SystemArch a) {
    switch (a) {
        case SystemArch::X64:            return VF_ARCH_X64;
        case SystemArch::ARM64_Emulated: return VF_ARCH_ARM64_EMULATED;
        case SystemArch::ARM64_Native:   return VF_ARCH_ARM64_NATIVE;
        case SystemArch::X86:            return VF_ARCH_X86;
        case SystemArch::Unknown:
        default:                         return VF_ARCH_UNKNOWN;
    }
}

ApuOptimizationPolicy BuildOptimizationPolicy(
    const SystemHardwareProfile& profile,
    uint64_t live_dxgi_local_budget_bytes) {

    static_assert(static_cast<int>(GpuArchitectureGen::AMD_RDNA2) == 2 &&
                  static_cast<int>(GpuArchitectureGen::AMD_RDNA3_Plus) == 3 &&
                  static_cast<int>(GpuArchitectureGen::Intel_Gen9_11_UHD) == 4 &&
                  static_cast<int>(GpuArchitectureGen::Intel_Xe_Arc) == 5 &&
                  static_cast<int>(GpuArchitectureGen::Discrete_GPU) == 6,
                  "GpuArchitectureGen must match device_profile.rs");
    static_assert(static_cast<int>(ApuTier::Tier1_EntryUMA) == 1 &&
                  static_cast<int>(ApuTier::Tier4_DiscreteOrCustom) == 4,
                  "ApuTier must match device_profile.rs");

    std::vector<std::string> adapter_names;
    std::vector<VfGpuAdapterInput> ffi_adapters;

    const auto append_gpu = [&](const GpuProfile& g, bool force_synthetic_dxcore) {
        adapter_names.push_back(app::WideToUtf8(g.description));
        VfGpuAdapterInput a{};
        a.vendor_id = (g.vendor_id != 0) ? g.vendor_id : VendorIdFromEnum(g.vendor);
        a.device_id = g.device_id;
        a.dedicated_vram_mb = g.dedicated_vram_bytes / (1024ULL * 1024ULL);
        a.shared_sys_mb = g.shared_system_bytes / (1024ULL * 1024ULL);

        if (g.connection != GpuConnection::Unknown) {
            a.dxcore_available = g.dxcore_available || force_synthetic_dxcore;
            a.dxcore_integrated = (g.connection == GpuConnection::Integrated);
            a.dxcore_detachable = (g.connection == GpuConnection::External);
            a.removal_detachable = g.removal_policy_detachable;
        } else if (g.dxcore_available) {
            a.dxcore_available = true;
            a.dxcore_integrated = g.dxcore_integrated;
            a.dxcore_detachable = g.dxcore_detachable;
            a.removal_detachable = g.removal_policy_detachable;
        } else if (force_synthetic_dxcore && g.mem_arch != MemoryArchitecture::Unknown) {
            // Backward compatibility for legacy synthetic tests setting only mem_arch.
            a.dxcore_available = true;
            a.dxcore_integrated = (g.mem_arch == MemoryArchitecture::UMA_Integrated);
            a.dxcore_detachable = false;
            a.removal_detachable = g.removal_policy_detachable;
        } else {
            a.dxcore_available = false;
            a.dxcore_integrated = false;
            a.dxcore_detachable = g.dxcore_detachable;
            a.removal_detachable = g.removal_policy_detachable;
        }
        ffi_adapters.push_back(a);
    };

    bool synthetic_dxcore = false;
    if (!profile.all_gpus.empty()) {
        for (const auto& g : profile.all_gpus) {
            append_gpu(g, false);
        }
    } else if (profile.gpu_probe_ok) {
        const bool has_any_primary_signal =
            (profile.primary_gpu.vendor_id != 0) ||
            (profile.primary_gpu.vendor != GpuVendor::Unknown) ||
            (profile.primary_gpu.mem_arch != MemoryArchitecture::Unknown) ||
            (profile.primary_gpu.connection != GpuConnection::Unknown) ||
            (profile.primary_gpu.arch_gen != GpuArchitectureGen::Unknown) ||
            !profile.primary_gpu.description.empty();
        if (has_any_primary_signal) {
            synthetic_dxcore = !profile.dxcore_available &&
                               (profile.primary_gpu.connection != GpuConnection::Unknown ||
                                profile.primary_gpu.mem_arch != MemoryArchitecture::Unknown);
            append_gpu(profile.primary_gpu, synthetic_dxcore);
        }
    }

    for (size_t i = 0; i < ffi_adapters.size(); ++i) {
        ffi_adapters[i].name_ptr = adapter_names[i].empty()
            ? nullptr
            : reinterpret_cast<const uint8_t*>(adapter_names[i].data());
        ffi_adapters[i].name_len = adapter_names[i].size();
    }

    const uint64_t total_ram_mb = profile.total_physical_ram_bytes / (1024ULL * 1024ULL);
    VfDeviceProfileInput input{};
    input.total_ram_mb = total_ram_mb;
    input.physical_cores = profile.cpu_topology.physical_cores;
    input.cpu_family = profile.cpu_family;
    input.cpu_model = profile.cpu_model;
    input.user_gpu_pref = profile.user_gpu_pref;
    input.system_arch = SystemArchToFfi(profile.system_arch);
    input.power_known = profile.power_status.known;
    input.has_battery = profile.power_status.has_battery;
    input.ac_online = profile.power_status.ac_online;
    input.battery_saver = profile.power_status.battery_saver;
    input.dxcore_available = profile.dxcore_available || synthetic_dxcore;
    input.ram_probe_ok = profile.ram_probe_ok && (profile.total_physical_ram_bytes > 0);
    input.experimental_egpu_streaming = profile.experimental_egpu_streaming;
    input.override_arch_gen = (profile.all_gpus.empty() && profile.primary_gpu.arch_gen != GpuArchitectureGen::Unknown)
        ? static_cast<uint8_t>(profile.primary_gpu.arch_gen)
        : 0u;

    VfDeviceProfileDecision dec{};
    vf_rust_evaluate_device_profile(
        ffi_adapters.empty() ? nullptr : ffi_adapters.data(),
        ffi_adapters.size(),
        &input,
        &dec);

    ApuOptimizationPolicy policy{};
    policy.tier = static_cast<ApuTier>(dec.apu_tier);
    policy.arch_gen = static_cast<GpuArchitectureGen>(dec.gpu_arch_gen);
    policy.gpu_connection = static_cast<GpuConnection>(dec.gpu_connection);
    policy.tier_label = TierLabelFor(policy.tier, policy.gpu_connection);
    policy.guard_mode_name = GuardModeForTierAndConn(policy.tier, policy.gpu_connection);

    policy.apply_visual_cuts = dec.apply_visual_cuts;
    policy.apply_uma_memory_guard = dec.apply_uma_memory_guard;
    policy.apply_uma_vram_reg_override = dec.apply_uma_vram_reg_override;
    policy.allow_ram_purge = dec.allow_ram_purge;
    policy.force_high_perf_power_plan = dec.force_high_perf_power_plan;
    policy.block_driver_tweaks = dec.block_driver_tweaks;
    policy.experimental_egpu_streaming = profile.experimental_egpu_streaming;
    policy.apply_egpu_streaming_tweaks = dec.apply_egpu_streaming_tweaks;

    policy.safe_vram_pool_mb = dec.safe_vram_pool_mb;
    policy.amd_tessellation_cap = dec.amd_tessellation_cap;
    policy.dxvk_compiler_threads = dec.dxvk_compiler_threads;
    policy.disable_dx12_async_compute = dec.disable_dx12_async_compute;
    policy.decision_reasons = dec.reasons;
    policy.cpu_stapm_cap_mhz = 0;
    policy.runtime_pressure_threshold = vf_rust_vram_pressure_threshold();

    switch (policy.tier) {
        case ApuTier::Tier1_EntryUMA:
            policy.os_reserve_floor_mb = 3072;
            policy.safe_shared_pool_mb = 4096;
            policy.max_anisotropy = 2;
            policy.recommend_dxvk_gplasync = true;
            policy.ue_max_csm_resolution = 512;
            policy.ue_max_shadow_cascades = 1;
            policy.disable_volumetric_fog = true;
            break;
        case ApuTier::Tier2_MidVegaUMA:
            policy.os_reserve_floor_mb = 4096;
            policy.safe_shared_pool_mb = 8192;
            policy.max_anisotropy = 4;
            policy.recommend_dxvk_gplasync = true;
            policy.ue_max_csm_resolution = 1024;
            policy.ue_max_shadow_cascades = 2;
            policy.disable_volumetric_fog = true;
            break;
        case ApuTier::Tier3_AdvancedUMA:
            policy.os_reserve_floor_mb = 4096;
            policy.safe_shared_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 50) / 100), 8192u, 16384u);
            policy.max_anisotropy = 8;
            policy.recommend_dxvk_gplasync = (policy.arch_gen == GpuArchitectureGen::AMD_GCN5_Vega);
            policy.ue_max_csm_resolution = 2048;
            policy.ue_max_shadow_cascades = 3;
            policy.disable_volumetric_fog = false;
            break;
        case ApuTier::Tier4_DiscreteOrCustom:
        default:
            policy.os_reserve_floor_mb = 4096;
            policy.safe_shared_pool_mb = 8192;
            policy.max_anisotropy = 16;
            policy.recommend_dxvk_gplasync = false;
            policy.ue_max_csm_resolution = 2048;
            policy.ue_max_shadow_cascades = 4;
            policy.disable_volumetric_fog = false;
            break;
    }

    if (live_dxgi_local_budget_bytes > 0 && policy.apply_uma_memory_guard &&
        policy.tier != ApuTier::Tier4_DiscreteOrCustom) {
        const uint32_t dxgi_80_mb = static_cast<uint32_t>((live_dxgi_local_budget_bytes * 80ULL / 100ULL) / (1024ULL * 1024ULL));
        if (policy.tier == ApuTier::Tier1_EntryUMA) {
            policy.safe_vram_pool_mb = std::min(policy.safe_vram_pool_mb, std::max(1536u, dxgi_80_mb));
        } else if (policy.tier == ApuTier::Tier3_AdvancedUMA) {
            const uint32_t floor_mb = (total_ram_mb >= 15000) ? 4096u : 1536u;
            policy.safe_vram_pool_mb = std::min(policy.safe_vram_pool_mb, std::max(floor_mb, dxgi_80_mb));
        }
    }

    return policy;
}

void PartitionCpuSets(CpuTopologyProfile& cpu_topology, uint16_t current_group) {
    cpu_topology.game_optimal_cpu_set_ids.clear();
    cpu_topology.background_cpu_set_ids.clear();

    if (cpu_topology.all_cpu_sets.empty()) {
        return;
    }

    uint16_t target_group = current_group;
    bool has_current_group = false;

    std::vector<VfCpuCoreEntry> entries;
    entries.reserve(cpu_topology.all_cpu_sets.size());
    std::set<std::pair<uint16_t, uint32_t>> total_phys_cores;
    for (const auto& cs : cpu_topology.all_cpu_sets) {
        if (cs.group == target_group) {
            has_current_group = true;
        }
        total_phys_cores.insert({cs.group, cs.core_index});
        VfCpuCoreEntry entry{};
        entry.cpu_set_id = cs.cpu_set_id;
        entry.core_index = cs.core_index;
        entry.logical_processor_index = cs.logical_processor_index;
        entry.group = cs.group;
        entry.last_level_cache_index = static_cast<uint8_t>(cs.last_level_cache_index);
        entry.efficiency_class = cs.efficiency_class;
        entries.push_back(entry);
    }
    if (!has_current_group) {
        target_group = cpu_topology.all_cpu_sets.front().group;
    }

    VfTopologyPartitionOut out{};
    if (vf_rust_partition_cpu_topology_group(
            entries.data(),
            static_cast<uint32_t>(entries.size()),
            target_group,
            &out)) {
        const bool multi_group = (cpu_topology.all_cpu_sets.size() > out.logical_processors);
        cpu_topology.logical_processors = multi_group
            ? static_cast<uint32_t>(cpu_topology.all_cpu_sets.size())
            : out.logical_processors;
        cpu_topology.physical_cores = multi_group
            ? static_cast<uint32_t>(total_phys_cores.size())
            : out.physical_cores;
        cpu_topology.has_multiple_ccx = out.has_multiple_ccx;
        cpu_topology.has_hybrid_cores = out.has_hybrid_cores;
        cpu_topology.game_optimal_cpu_set_ids.assign(out.game_cpu_set_ids, out.game_cpu_set_ids + out.game_count);
        cpu_topology.background_cpu_set_ids.assign(out.background_cpu_set_ids, out.background_cpu_set_ids + out.background_count);
    }
}

} // namespace vegaflow
