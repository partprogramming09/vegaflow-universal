#include "detect/hardware_profiler.hpp"
#include "bridge/rust_bridge.h"
#include "windows/device_probe.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

const char* HardwareProfiler::VendorToString(GpuVendor v) {
    switch (v) {
        case GpuVendor::AMD:    return "AMD";
        case GpuVendor::Intel:  return "Intel";
        case GpuVendor::NVIDIA: return "NVIDIA";
        case GpuVendor::Unknown:
        default:                return "Unknown";
    }
}

const char* HardwareProfiler::MemArchToString(MemoryArchitecture m) {
    switch (m) {
        case MemoryArchitecture::UMA_Integrated: return "UMA_Integrated";
        case MemoryArchitecture::Dedicated_VRAM: return "Dedicated_VRAM";
        case MemoryArchitecture::Unknown:
        default:                                 return "Unknown";
    }
}

const char* HardwareProfiler::ConnectionToString(GpuConnection c) {
    switch (c) {
        case GpuConnection::Integrated: return "Integrada";
        case GpuConnection::Discrete:   return "Dedicada";
        case GpuConnection::External:   return "Externa";
        case GpuConnection::Unknown:
        default:                        return "No se pudo leer";
    }
}

const char* HardwareProfiler::DiskTypeToString(GameDiskType d) {
    switch (d) {
        case GameDiskType::HDD:     return "HDD";
        case GameDiskType::SSD:     return "SSD";
        case GameDiskType::NVMe:    return "NVMe";
        case GameDiskType::Unknown:
        default:                    return "No se pudo leer";
    }
}

const char* HardwareProfiler::SystemArchToString(SystemArch a) {
    switch (a) {
        case SystemArch::X64:            return "x64";
        case SystemArch::ARM64_Emulated: return "ARM64 (x64 emulado)";
        case SystemArch::ARM64_Native:   return "ARM64";
        case SystemArch::X86:            return "x86";
        case SystemArch::Unknown:
        default:                         return "No se pudo leer";
    }
}

const char* HardwareProfiler::ApuTierToString(ApuTier tier) {
    switch (tier) {
        case ApuTier::Tier1_EntryUMA:        return "Tier1_EntryUMA";
        case ApuTier::Tier2_MidVegaUMA:      return "Tier2_MidVegaUMA";
        case ApuTier::Tier3_AdvancedUMA:     return "Tier3_AdvancedUMA";
        case ApuTier::Tier4_DiscreteOrCustom:return "Tier4_DiscreteOrCustom";
        default:                             return "Unknown";
    }
}

const char* HardwareProfiler::ArchGenToString(GpuArchitectureGen gen) {
    switch (gen) {
        case GpuArchitectureGen::AMD_GCN5_Vega:    return "AMD_GCN5_Vega";
        case GpuArchitectureGen::AMD_RDNA2:        return "AMD_RDNA2";
        case GpuArchitectureGen::AMD_RDNA3_Plus:   return "AMD_RDNA3_Plus";
        case GpuArchitectureGen::Intel_Gen9_11_UHD:return "Intel_Gen9_11_UHD";
        case GpuArchitectureGen::Intel_Xe_Arc:     return "Intel_Xe_Arc";
        case GpuArchitectureGen::Discrete_GPU:     return "Discrete_GPU";
        case GpuArchitectureGen::Unknown:
        default:                                   return "Unknown";
    }
}

namespace {

std::string WideToUtf8Profiler(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

uint32_t VendorIdFromEnum(GpuVendor v) {
    switch (v) {
        case GpuVendor::AMD:    return 0x1002u;
        case GpuVendor::Intel:  return 0x8086u;
        case GpuVendor::NVIDIA: return 0x10DEu;
        default:                return 0u;
    }
}

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

ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(
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
        adapter_names.push_back(WideToUtf8Profiler(g.description));
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

SystemHardwareProfile HardwareProfiler::DetectHardware(bool include_displays) {
    WindowsDeviceProbe probe;
    RawDeviceSnapshot snap = probe.ProbeSystem(include_displays);

    SystemHardwareProfile profile{};
    profile.total_physical_ram_bytes = snap.total_physical_ram_bytes;
    profile.ram_probe_ok = snap.ram_probe_ok;
    profile.gpu_probe_ok = snap.gpu_probe_ok;
    profile.cpu_probe_ok = snap.cpu_probe_ok;
    profile.dxcore_available = snap.dxcore_available;
    profile.all_gpus = std::move(snap.gpus);
    profile.cpu_topology = std::move(snap.cpu_topology);
    profile.cpu_family = snap.cpu_family;
    profile.cpu_model = snap.cpu_model;
    profile.power_status = snap.power_status;
    profile.system_arch = snap.system_arch;
    profile.windows_version = snap.windows_version;
    profile.displays = std::move(snap.displays);
    profile.adapter_signature = snap.adapter_signature;

    if (!profile.cpu_topology.all_cpu_sets.empty()) {
        PartitionCpuSetsFromTopology(profile.cpu_topology);
    } else {
        SYSTEM_INFO sys_info{};
        GetNativeSystemInfo(&sys_info);
        profile.cpu_topology.logical_processors = sys_info.dwNumberOfProcessors;
        profile.cpu_topology.physical_cores = sys_info.dwNumberOfProcessors;
    }

    // Determine primary GPU and resolved policy from Rust device_profile.rs.
    std::vector<std::string> adapter_names;
    std::vector<VfGpuAdapterInput> ffi_adapters;
    adapter_names.reserve(profile.all_gpus.size());
    ffi_adapters.reserve(profile.all_gpus.size());
    for (const auto& g : profile.all_gpus) {
        adapter_names.push_back(WideToUtf8Profiler(g.description));
        VfGpuAdapterInput a{};
        a.vendor_id = g.vendor_id;
        a.device_id = g.device_id;
        a.dedicated_vram_mb = g.dedicated_vram_bytes / (1024ULL * 1024ULL);
        a.shared_sys_mb = g.shared_system_bytes / (1024ULL * 1024ULL);
        a.dxcore_available = g.dxcore_available;
        a.dxcore_integrated = g.dxcore_integrated;
        a.dxcore_detachable = g.dxcore_detachable;
        a.removal_detachable = g.removal_policy_detachable;
        a.name_ptr = adapter_names.back().empty()
            ? nullptr
            : reinterpret_cast<const uint8_t*>(adapter_names.back().data());
        a.name_len = adapter_names.back().size();
        ffi_adapters.push_back(a);
    }

    VfDeviceProfileInput input{};
    input.total_ram_mb = profile.total_physical_ram_bytes / (1024ULL * 1024ULL);
    input.physical_cores = profile.cpu_topology.physical_cores;
    input.cpu_family = profile.cpu_family;
    input.cpu_model = profile.cpu_model;
    input.user_gpu_pref = profile.user_gpu_pref;
    input.system_arch = SystemArchToFfi(profile.system_arch);
    input.power_known = profile.power_status.known;
    input.has_battery = profile.power_status.has_battery;
    input.ac_online = profile.power_status.ac_online;
    input.battery_saver = profile.power_status.battery_saver;
    input.dxcore_available = profile.dxcore_available;
    input.ram_probe_ok = profile.ram_probe_ok;
    input.experimental_egpu_streaming = profile.experimental_egpu_streaming;
    input.override_arch_gen = 0;

    VfDeviceProfileDecision dec{};
    vf_rust_evaluate_device_profile(
        ffi_adapters.empty() ? nullptr : ffi_adapters.data(),
        ffi_adapters.size(),
        &input,
        &dec);

    if (dec.gpu_probe_ok && dec.selected_gpu_index < profile.all_gpus.size()) {
        profile.primary_gpu = profile.all_gpus[dec.selected_gpu_index];
    }
    profile.primary_gpu.connection = static_cast<GpuConnection>(dec.gpu_connection);
    profile.primary_gpu.arch_gen = static_cast<GpuArchitectureGen>(dec.gpu_arch_gen);
    profile.primary_gpu.mem_arch = (profile.primary_gpu.connection == GpuConnection::Integrated)
        ? MemoryArchitecture::UMA_Integrated
        : (dec.gpu_connection == VF_GPU_CONN_UNKNOWN ? MemoryArchitecture::Unknown : MemoryArchitecture::Dedicated_VRAM);
    profile.apu_tier = static_cast<ApuTier>(dec.apu_tier);

    return profile;
}

void HardwareProfiler::PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology) {
    cpu_topology.game_optimal_cpu_set_ids.clear();
    cpu_topology.background_cpu_set_ids.clear();

    if (cpu_topology.all_cpu_sets.empty()) {
        return;
    }

    if (cpu_topology.all_cpu_sets.size() <= VF_MAX_LOGICAL_PROCESSORS) {
        VfCpuCoreEntry entries[VF_MAX_LOGICAL_PROCESSORS]{};
        for (size_t i = 0; i < cpu_topology.all_cpu_sets.size(); ++i) {
            const auto& cs = cpu_topology.all_cpu_sets[i];
            entries[i].cpu_set_id = cs.cpu_set_id;
            entries[i].core_index = cs.core_index;
            entries[i].logical_processor_index = cs.logical_processor_index;
            entries[i].last_level_cache_index = static_cast<uint8_t>(cs.last_level_cache_index);
            entries[i].efficiency_class = cs.efficiency_class;
        }

        VfTopologyPartitionOut out{};
        if (vf_rust_partition_cpu_topology(entries, static_cast<uint32_t>(cpu_topology.all_cpu_sets.size()), &out)) {
            cpu_topology.logical_processors = out.logical_processors;
            cpu_topology.physical_cores = out.physical_cores;
            cpu_topology.has_multiple_ccx = out.has_multiple_ccx;
            cpu_topology.has_hybrid_cores = out.has_hybrid_cores;
            cpu_topology.game_optimal_cpu_set_ids.assign(out.game_cpu_set_ids, out.game_cpu_set_ids + out.game_count);
            cpu_topology.background_cpu_set_ids.assign(out.background_cpu_set_ids, out.background_cpu_set_ids + out.background_count);
            return;
        }
    }

    std::set<uint32_t> physical_core_indices;
    std::set<uint32_t> llc_indices;
    std::map<uint32_t, uint32_t> llc_counts;
    uint8_t min_eff = 255;
    uint8_t max_eff = 0;

    for (const auto& cs : cpu_topology.all_cpu_sets) {
        physical_core_indices.insert(cs.core_index);
        llc_indices.insert(cs.last_level_cache_index);
        llc_counts[cs.last_level_cache_index]++;
        if (cs.efficiency_class < min_eff) min_eff = cs.efficiency_class;
        if (cs.efficiency_class > max_eff) max_eff = cs.efficiency_class;
    }

    cpu_topology.logical_processors = static_cast<uint32_t>(cpu_topology.all_cpu_sets.size());
    cpu_topology.physical_cores = static_cast<uint32_t>(physical_core_indices.size());
    cpu_topology.has_multiple_ccx = (llc_indices.size() > 1);
    cpu_topology.has_hybrid_cores = (min_eff != max_eff);

    const size_t threads_per_core = std::max<size_t>(1, cpu_topology.logical_processors / std::max<uint32_t>(1, cpu_topology.physical_cores));
    const size_t bg_limit = (cpu_topology.physical_cores >= 4) ? threads_per_core : 1;

    if (cpu_topology.has_hybrid_cores) {
        std::set<uint32_t> high_perf_cores;
        for (const auto& cs : cpu_topology.all_cpu_sets) {
            if (cs.efficiency_class == max_eff) high_perf_cores.insert(cs.core_index);
        }
        const bool exclude_core_zero = (high_perf_cores.size() >= 4);
        for (const auto& cs : cpu_topology.all_cpu_sets) {
            if (cs.efficiency_class == max_eff) {
                if (!exclude_core_zero || cs.core_index != 0) cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
            } else {
                cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
            }
        }
        return;
    }

    if (cpu_topology.has_multiple_ccx && cpu_topology.physical_cores >= 6) {
        uint32_t min_count = UINT32_MAX;
        uint32_t max_count = 0;
        for (const auto& [llc, count] : llc_counts) {
            if (count < min_count) min_count = count;
            if (count > max_count) max_count = count;
        }
        uint32_t secondary_llc = *llc_indices.rbegin();
        if (min_count != max_count) {
            for (auto it = llc_indices.rbegin(); it != llc_indices.rend(); ++it) {
                if (llc_counts[*it] == min_count) {
                    secondary_llc = *it;
                    break;
                }
            }
        }
        if (llc_counts[secondary_llc] <= (cpu_topology.logical_processors / 3)) {
            const bool exclude_core_zero = (cpu_topology.physical_cores >= 4);
            std::vector<CpuCoreSetInfo> candidate_game_sets;
            for (const auto& cs : cpu_topology.all_cpu_sets) {
                if (cs.last_level_cache_index == secondary_llc) {
                    cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
                } else if (!exclude_core_zero || cs.core_index != 0) {
                    candidate_game_sets.push_back(cs);
                }
            }
            std::sort(candidate_game_sets.begin(), candidate_game_sets.end(),
                      [](const CpuCoreSetInfo& a, const CpuCoreSetInfo& b) {
                          if (a.last_level_cache_index != b.last_level_cache_index) return a.last_level_cache_index < b.last_level_cache_index;
                          if (a.core_index != b.core_index) return a.core_index < b.core_index;
                          return a.logical_processor_index < b.logical_processor_index;
                      });
            for (const auto& cs : candidate_game_sets) cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
            return;
        }
    }

    const uint32_t last_core_index = *physical_core_indices.rbegin();
    const bool exclude_core_zero = (cpu_topology.physical_cores >= 4);
    std::vector<CpuCoreSetInfo> candidate_game_sets;
    size_t bg_taken = 0;
    for (const auto& cs : cpu_topology.all_cpu_sets) {
        if (cs.core_index == last_core_index && bg_taken < bg_limit) {
            bg_taken++;
            cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
        } else if (!exclude_core_zero || cs.core_index != 0) {
            candidate_game_sets.push_back(cs);
        }
    }
    std::map<uint32_t, size_t> llc_core_counts;
    for (const auto& cs : candidate_game_sets) llc_core_counts[cs.last_level_cache_index]++;
    std::sort(candidate_game_sets.begin(), candidate_game_sets.end(),
              [&llc_core_counts](const CpuCoreSetInfo& a, const CpuCoreSetInfo& b) {
                  const size_t ca = llc_core_counts[a.last_level_cache_index];
                  const size_t cb = llc_core_counts[b.last_level_cache_index];
                  if (ca != cb) return ca > cb;
                  if (a.last_level_cache_index != b.last_level_cache_index) return a.last_level_cache_index < b.last_level_cache_index;
                  if (a.core_index != b.core_index) return a.core_index < b.core_index;
                  return a.logical_processor_index < b.logical_processor_index;
              });
    for (const auto& cs : candidate_game_sets) cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
}

} // namespace vegaflow
