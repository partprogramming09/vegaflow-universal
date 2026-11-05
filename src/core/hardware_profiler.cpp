#include "hardware_profiler.hpp"

#include <dxgi.h>
#include <intrin.h>

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <utility>

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

std::string QueryCpuBrandString() {
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 0x80000000);
    const unsigned int max_extended_id = static_cast<unsigned int>(cpu_info[0]);
    if (max_extended_id < 0x80000004) {
        return {};
    }

    char brand[49] = {0};
    __cpuid(reinterpret_cast<int*>(brand), 0x80000002);
    __cpuid(reinterpret_cast<int*>(brand + 16), 0x80000003);
    __cpuid(reinterpret_cast<int*>(brand + 32), 0x80000004);

    const char* start = brand;
    while (*start == ' ' || *start == '\t') {
        ++start;
    }
    std::string s(start);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\0')) {
        s.pop_back();
    }
    return s;
}

void DetectGpu(GpuProfile& gpu_profile) {
    HMODULE dxgi_lib = LoadLibraryW(L"dxgi.dll");
    if (!dxgi_lib) {
        return;
    }

    using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pCreateDXGIFactory1 = reinterpret_cast<PFN_CreateDXGIFactory1>(
        GetProcAddress(dxgi_lib, "CreateDXGIFactory1"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (pCreateDXGIFactory1) {
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(pCreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory) {
            IDXGIAdapter1* adapter = nullptr;
            bool found_hardware_gpu = false;
            DXGI_ADAPTER_DESC1 best_desc{};

            for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                    const bool is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || (desc.VendorId == 0x1414);
                    if (!is_software) {
                        if (!found_hardware_gpu) {
                            best_desc = desc;
                            found_hardware_gpu = true;
                        } else if (desc.DedicatedVideoMemory > best_desc.DedicatedVideoMemory) {
                            best_desc = desc;
                        }
                    }
                }
                adapter->Release();
            }
            factory->Release();

            if (found_hardware_gpu) {
                gpu_profile.description = best_desc.Description;
                gpu_profile.vendor_id = best_desc.VendorId;
                gpu_profile.device_id = best_desc.DeviceId;
                gpu_profile.dedicated_vram_bytes = static_cast<uint64_t>(best_desc.DedicatedVideoMemory);
                gpu_profile.shared_system_bytes = static_cast<uint64_t>(best_desc.SharedSystemMemory);

                if (best_desc.VendorId == 0x1002) {
                    gpu_profile.vendor = GpuVendor::AMD;
                } else if (best_desc.VendorId == 0x8086) {
                    gpu_profile.vendor = GpuVendor::Intel;
                } else if (best_desc.VendorId == 0x10DE) {
                    gpu_profile.vendor = GpuVendor::NVIDIA;
                } else {
                    gpu_profile.vendor = GpuVendor::Unknown;
                }

                constexpr uint64_t two_gb = 2ULL * 1024 * 1024 * 1024;
                if (gpu_profile.dedicated_vram_bytes <= two_gb) {
                    gpu_profile.mem_arch = MemoryArchitecture::UMA_Integrated;
                } else {
                    gpu_profile.mem_arch = MemoryArchitecture::Dedicated_VRAM;
                }
            }
        }
    }
    FreeLibrary(dxgi_lib);
}

void DetectCpuTopology(CpuTopologyProfile& cpu_topology) {
    cpu_topology.brand_string = QueryCpuBrandString();

    ULONG buffer_length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &buffer_length, GetCurrentProcess(), 0);

    if (buffer_length > 0) {
        std::vector<BYTE> buffer(buffer_length);
        if (GetSystemCpuSetInformation(
                reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
                buffer_length,
                &buffer_length,
                GetCurrentProcess(),
                0)) {
            const BYTE* ptr = buffer.data();
            const BYTE* end = ptr + buffer_length;
            while (ptr < end) {
                const auto info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(ptr);
                if (info->Size == 0) {
                    break;
                }
                if (info->Type == CpuSetInformation) {
                    CpuCoreSetInfo core_set{};
                    core_set.cpu_set_id = info->CpuSet.Id;
                    core_set.core_index = info->CpuSet.CoreIndex;
                    core_set.logical_processor_index = info->CpuSet.LogicalProcessorIndex;
                    core_set.last_level_cache_index = info->CpuSet.LastLevelCacheIndex;
                    core_set.efficiency_class = info->CpuSet.EfficiencyClass;
                    cpu_topology.all_cpu_sets.push_back(core_set);
                }
                ptr += info->Size;
            }
        }
    }

    if (cpu_topology.all_cpu_sets.empty()) {
        SYSTEM_INFO sys_info{};
        GetNativeSystemInfo(&sys_info);
        cpu_topology.logical_processors = sys_info.dwNumberOfProcessors;
        cpu_topology.physical_cores = sys_info.dwNumberOfProcessors;
        return;
    }

    HardwareProfiler::PartitionCpuSetsFromTopology(cpu_topology);
}

GpuArchitectureGen ResolveArchGen(const GpuProfile& gpu) {
    if (gpu.arch_gen != GpuArchitectureGen::Unknown) {
        return gpu.arch_gen;
    }

    if (gpu.mem_arch == MemoryArchitecture::Dedicated_VRAM || gpu.vendor == GpuVendor::NVIDIA) {
        return GpuArchitectureGen::Discrete_GPU;
    }

    std::wstring lower_desc = gpu.description;
    for (auto& c : lower_desc) {
        c = static_cast<wchar_t>(std::towlower(c));
    }

    const bool is_uma_or_default = (gpu.mem_arch == MemoryArchitecture::UMA_Integrated ||
                                    gpu.mem_arch == MemoryArchitecture::Unknown);

    if (gpu.vendor == GpuVendor::AMD && is_uma_or_default) {
        int cpu_info[4] = {0};
        __cpuid(cpu_info, 1);
        const int family = ((cpu_info[0] >> 8) & 0xF) + ((cpu_info[0] >> 20) & 0xFF);
        const int model = ((cpu_info[0] >> 4) & 0xF) | (((cpu_info[0] >> 16) & 0xF) << 4);

        if (lower_desc.find(L"780m") != std::wstring::npos ||
            lower_desc.find(L"760m") != std::wstring::npos ||
            lower_desc.find(L"740m") != std::wstring::npos ||
            lower_desc.find(L"880m") != std::wstring::npos ||
            lower_desc.find(L"890m") != std::wstring::npos ||
            family >= 0x1A ||
            (family == 0x19 && model >= 0x70)) {
            return GpuArchitectureGen::AMD_RDNA3_Plus;
        }

        if (lower_desc.find(L"680m") != std::wstring::npos ||
            lower_desc.find(L"660m") != std::wstring::npos ||
            lower_desc.find(L"610m") != std::wstring::npos ||
            (family == 0x19 && model >= 0x40 && model <= 0x4F)) {
            return GpuArchitectureGen::AMD_RDNA2;
        }

        return GpuArchitectureGen::AMD_GCN5_Vega;
    }

    if (gpu.vendor == GpuVendor::Intel && is_uma_or_default) {
        if (lower_desc.find(L"arc") != std::wstring::npos ||
            lower_desc.find(L"iris") != std::wstring::npos ||
            lower_desc.find(L"xe") != std::wstring::npos) {
            return GpuArchitectureGen::Intel_Xe_Arc;
        }
        return GpuArchitectureGen::Intel_Gen9_11_UHD;
    }

    return GpuArchitectureGen::Unknown;
}

} // namespace

ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(
    const SystemHardwareProfile& profile,
    uint64_t live_dxgi_local_budget_bytes) {

    ApuOptimizationPolicy policy{};
    const GpuArchitectureGen arch_gen = ResolveArchGen(profile.primary_gpu);
    policy.arch_gen = arch_gen;

    const uint64_t total_ram_mb = profile.total_physical_ram_bytes / (1024ULL * 1024ULL);
    const uint32_t cores = (profile.cpu_topology.physical_cores > 0) ? profile.cpu_topology.physical_cores : 4u;

    ApuTier tier = ApuTier::Tier2_MidVegaUMA;
    if (profile.primary_gpu.mem_arch == MemoryArchitecture::Dedicated_VRAM ||
        arch_gen == GpuArchitectureGen::Discrete_GPU ||
        profile.primary_gpu.vendor == GpuVendor::NVIDIA) {
        tier = ApuTier::Tier4_DiscreteOrCustom;
    } else if (total_ram_mb <= 9216 || cores <= 4 || arch_gen == GpuArchitectureGen::Intel_Gen9_11_UHD) {
        tier = ApuTier::Tier1_EntryUMA;
    } else if (arch_gen == GpuArchitectureGen::AMD_RDNA2 ||
               arch_gen == GpuArchitectureGen::AMD_RDNA3_Plus ||
               arch_gen == GpuArchitectureGen::Intel_Xe_Arc ||
               total_ram_mb >= 22528) {
        tier = ApuTier::Tier3_AdvancedUMA;
    } else {
        tier = ApuTier::Tier2_MidVegaUMA;
    }

    policy.tier = tier;

    switch (tier) {
        case ApuTier::Tier1_EntryUMA: {
            policy.tier_label = "Tier 1: Entry UMA [Survival Guard]";
            policy.guard_mode_name = "Survival Guard";
            policy.os_reserve_floor_mb = 3072;
            policy.safe_vram_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 22) / 100), 1536u, 2048u);
            policy.safe_shared_pool_mb = 4096;
            policy.runtime_pressure_threshold = 0.85;
            policy.dxvk_compiler_threads = std::clamp(cores > 1 ? cores - 1 : 1u, 1u, 3u);
            policy.amd_tessellation_cap = 8;
            policy.max_anisotropy = 2;
            policy.recommend_dxvk_gplasync = true;
            policy.disable_dx12_async_compute = true;
            policy.ue_max_csm_resolution = 512;
            policy.ue_max_shadow_cascades = 1;
            policy.disable_volumetric_fog = true;
            policy.cpu_stapm_cap_mhz = 0;
            policy.apply_uma_vram_reg_override = true;
            break;
        }
        case ApuTier::Tier2_MidVegaUMA: {
            policy.tier_label = "Tier 2: Mid Vega UMA [Sweet-Spot Guard]";
            policy.guard_mode_name = "Sweet-Spot Guard";
            policy.os_reserve_floor_mb = 4096;
            policy.safe_vram_pool_mb = 4096;
            policy.safe_shared_pool_mb = 8192;
            policy.runtime_pressure_threshold = 0.85;
            policy.dxvk_compiler_threads = std::clamp(cores, 4u, 6u);
            policy.amd_tessellation_cap = 16;
            policy.max_anisotropy = 4;
            policy.recommend_dxvk_gplasync = true;
            policy.disable_dx12_async_compute = true;
            policy.ue_max_csm_resolution = 1024;
            policy.ue_max_shadow_cascades = 2;
            policy.disable_volumetric_fog = true;
            policy.cpu_stapm_cap_mhz = 0;
            policy.apply_uma_vram_reg_override = true;
            break;
        }
        case ApuTier::Tier3_AdvancedUMA: {
            policy.tier_label = "Tier 3: Advanced UMA [Do-No-Harm Guard]";
            policy.guard_mode_name = "Do-No-Harm Guard";
            policy.os_reserve_floor_mb = 4096;
            policy.safe_vram_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 30) / 100), 4096u, 8192u);
            policy.safe_shared_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 50) / 100), 8192u, 16384u);
            policy.runtime_pressure_threshold = 0.85;
            policy.dxvk_compiler_threads = std::clamp(cores > 2 ? cores - 2 : 4u, 4u, 8u);
            policy.amd_tessellation_cap = 32;
            policy.max_anisotropy = 8;
            policy.recommend_dxvk_gplasync = (arch_gen == GpuArchitectureGen::AMD_GCN5_Vega);
            policy.disable_dx12_async_compute = false;
            policy.ue_max_csm_resolution = 2048;
            policy.ue_max_shadow_cascades = 3;
            policy.disable_volumetric_fog = false;
            policy.cpu_stapm_cap_mhz = 0;
            policy.apply_uma_vram_reg_override = true;
            break;
        }
        case ApuTier::Tier4_DiscreteOrCustom:
        default: {
            policy.tier_label = "Tier 4: Discrete GPU / Custom [Pass-Through Guard]";
            policy.guard_mode_name = "Pass-Through Guard";
            policy.os_reserve_floor_mb = 4096;
            policy.safe_vram_pool_mb = 4096;
            policy.safe_shared_pool_mb = 8192;
            policy.runtime_pressure_threshold = 0.85;
            policy.dxvk_compiler_threads = std::clamp(cores > 2 ? cores - 2 : 4u, 4u, 8u);
            policy.amd_tessellation_cap = 64;
            policy.max_anisotropy = 16;
            policy.recommend_dxvk_gplasync = false;
            policy.disable_dx12_async_compute = false;
            policy.ue_max_csm_resolution = 2048;
            policy.ue_max_shadow_cascades = 4;
            policy.disable_volumetric_fog = false;
            policy.cpu_stapm_cap_mhz = 0;
            policy.apply_uma_vram_reg_override = false;
            break;
        }
    }

    if (live_dxgi_local_budget_bytes > 0 && tier != ApuTier::Tier4_DiscreteOrCustom) {
        const uint32_t dxgi_80_mb = static_cast<uint32_t>((live_dxgi_local_budget_bytes * 80ULL / 100ULL) / (1024ULL * 1024ULL));
        if (tier == ApuTier::Tier1_EntryUMA) {
            policy.safe_vram_pool_mb = std::min(policy.safe_vram_pool_mb, std::max(1536u, dxgi_80_mb));
        } else if (tier == ApuTier::Tier3_AdvancedUMA) {
            const uint32_t floor_mb = (total_ram_mb >= 15000) ? 4096u : 1536u;
            policy.safe_vram_pool_mb = std::min(policy.safe_vram_pool_mb, std::max(floor_mb, dxgi_80_mb));
        }
    }

    return policy;
}

SystemHardwareProfile HardwareProfiler::DetectHardware() {
    SystemHardwareProfile profile{};

    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        profile.total_physical_ram_bytes = mem_status.ullTotalPhys;
    }

    DetectGpu(profile.primary_gpu);
    DetectCpuTopology(profile.cpu_topology);

    const ApuOptimizationPolicy policy = BuildOptimizationPolicy(profile);
    profile.primary_gpu.arch_gen = policy.arch_gen;
    profile.apu_tier = policy.tier;

    return profile;
}

void HardwareProfiler::PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology) {
    cpu_topology.game_optimal_cpu_set_ids.clear();
    cpu_topology.background_cpu_set_ids.clear();

    if (cpu_topology.all_cpu_sets.empty()) {
        return;
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
        if (cs.efficiency_class < min_eff) {
            min_eff = cs.efficiency_class;
        }
        if (cs.efficiency_class > max_eff) {
            max_eff = cs.efficiency_class;
        }
    }

    cpu_topology.logical_processors = static_cast<uint32_t>(cpu_topology.all_cpu_sets.size());
    cpu_topology.physical_cores = static_cast<uint32_t>(physical_core_indices.size());
    cpu_topology.has_multiple_ccx = (llc_indices.size() > 1);
    cpu_topology.has_hybrid_cores = (min_eff != max_eff);

    // Con 4 o mas nucleos el fondo recibe un nucleo fisico completo (sus hilos SMT); con menos, un solo hilo.
    const size_t threads_per_core = std::max<size_t>(1, cpu_topology.logical_processors / std::max<uint32_t>(1, cpu_topology.physical_cores));
    const size_t bg_limit = (cpu_topology.physical_cores >= 4) ? threads_per_core : 1;

    if (cpu_topology.has_hybrid_cores) {
        std::set<uint32_t> high_perf_cores;
        for (const auto& cs : cpu_topology.all_cpu_sets) {
            if (cs.efficiency_class == max_eff) {
                high_perf_cores.insert(cs.core_index);
            }
        }

        const bool exclude_core_zero = (high_perf_cores.size() >= 4);

        for (const auto& cs : cpu_topology.all_cpu_sets) {
            if (cs.efficiency_class == max_eff) {
                if (!exclude_core_zero || cs.core_index != 0) {
                    cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
                }
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
        } else {
            secondary_llc = *llc_indices.rbegin();
        }

        if (llc_counts[secondary_llc] <= (cpu_topology.logical_processors / 3)) {
            // Isla compacta asimétrica (Zen 4c/5c)
            const bool exclude_core_zero = (cpu_topology.physical_cores >= 4);
            std::vector<CpuCoreSetInfo> candidate_game_sets;
            for (const auto& cs : cpu_topology.all_cpu_sets) {
                if (cs.last_level_cache_index == secondary_llc) {
                    cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
                } else {
                    if (!exclude_core_zero || cs.core_index != 0) {
                        candidate_game_sets.push_back(cs);
                    }
                }
            }
            std::sort(
                candidate_game_sets.begin(),
                candidate_game_sets.end(),
                [](const CpuCoreSetInfo& a, const CpuCoreSetInfo& b) {
                    if (a.last_level_cache_index != b.last_level_cache_index) {
                        return a.last_level_cache_index < b.last_level_cache_index;
                    }
                    if (a.core_index != b.core_index) {
                        return a.core_index < b.core_index;
                    }
                    return a.logical_processor_index < b.logical_processor_index;
                }
            );
            for (const auto& cs : candidate_game_sets) {
                cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
            }
            return;
        }

        // Islas L3 simétricas (ej. 2 CCX de 3 núcleos / 6 hilos en Ryzen 5 4600H)
        const uint32_t last_core_index = *physical_core_indices.rbegin();
        const bool exclude_core_zero = (cpu_topology.physical_cores >= 4);

        std::vector<CpuCoreSetInfo> candidate_game_sets;
        std::map<uint32_t, size_t> bg_sets_per_llc;
        size_t bg_taken = 0;

        for (const auto& cs : cpu_topology.all_cpu_sets) {
            if (cs.core_index == last_core_index && bg_taken < bg_limit) {
                bg_taken++;
                cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
                bg_sets_per_llc[cs.last_level_cache_index]++;
            } else {
                if (!exclude_core_zero || cs.core_index != 0) {
                    candidate_game_sets.push_back(cs);
                }
            }
        }

        std::map<uint32_t, size_t> available_sets_per_llc;
        for (const auto& cs : candidate_game_sets) {
            available_sets_per_llc[cs.last_level_cache_index]++;
        }

        std::sort(
            candidate_game_sets.begin(),
            candidate_game_sets.end(),
            [&](const CpuCoreSetInfo& a, const CpuCoreSetInfo& b) {
                size_t bg_a = bg_sets_per_llc[a.last_level_cache_index];
                size_t bg_b = bg_sets_per_llc[b.last_level_cache_index];
                if (bg_a != bg_b) {
                    return bg_a < bg_b;
                }
                size_t avail_a = available_sets_per_llc[a.last_level_cache_index];
                size_t avail_b = available_sets_per_llc[b.last_level_cache_index];
                if (avail_a != avail_b) {
                    return avail_a > avail_b;
                }
                if (a.last_level_cache_index != b.last_level_cache_index) {
                    return a.last_level_cache_index < b.last_level_cache_index;
                }
                if (a.core_index != b.core_index) {
                    return a.core_index < b.core_index;
                }
                return a.logical_processor_index < b.logical_processor_index;
            }
        );

        for (const auto& cs : candidate_game_sets) {
            cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
        }
        return;
    }

    // Default / Monolítico / Uniforme o fallback
    const uint32_t last_core_index = *physical_core_indices.rbegin();
    const bool exclude_core_zero = (cpu_topology.physical_cores >= 4);

    std::vector<CpuCoreSetInfo> candidate_game_sets;

    size_t bg_taken = 0;
    for (const auto& cs : cpu_topology.all_cpu_sets) {
        if (cs.core_index == last_core_index && bg_taken < bg_limit) {
            bg_taken++;
            cpu_topology.background_cpu_set_ids.push_back(cs.cpu_set_id);
        } else {
            if (!exclude_core_zero || cs.core_index != 0) {
                candidate_game_sets.push_back(cs);
            }
        }
    }

    std::map<uint32_t, size_t> llc_core_counts;
    for (const auto& cs : candidate_game_sets) {
        llc_core_counts[cs.last_level_cache_index]++;
    }

    std::sort(
        candidate_game_sets.begin(),
        candidate_game_sets.end(),
        [&llc_core_counts](const CpuCoreSetInfo& a, const CpuCoreSetInfo& b) {
            const size_t count_a = llc_core_counts[a.last_level_cache_index];
            const size_t count_b = llc_core_counts[b.last_level_cache_index];
            if (count_a != count_b) {
                return count_a > count_b;
            }
            if (a.last_level_cache_index != b.last_level_cache_index) {
                return a.last_level_cache_index < b.last_level_cache_index;
            }
            if (a.core_index != b.core_index) {
                return a.core_index < b.core_index;
            }
            return a.logical_processor_index < b.logical_processor_index;
        }
    );

    for (const auto& cs : candidate_game_sets) {
        cpu_topology.game_optimal_cpu_set_ids.push_back(cs.cpu_set_id);
    }
}

} // namespace vegaflow
