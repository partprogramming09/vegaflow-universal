#include "session/system_info.hpp"
#include "detect/hardware_profiler.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"

#include <intrin.h>
#include <iomanip>
#include <sstream>
#include <vector>

namespace vegaflow {

namespace {

std::string EscapeJson(const std::string& input) {
    std::ostringstream ss;
    for (char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
}

std::string FormatUintVector(const std::vector<uint32_t>& vec) {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << vec[i];
    }
    ss << "]";
    return ss.str();
}

} // anonymous namespace

SystemInfo::SystemInfo()
    : snapshot_(Gather()) {}

SystemInfo::SystemInfo(const SystemInfoSnapshot& snapshot)
    : snapshot_(snapshot) {}

SystemInfoSnapshot SystemInfo::Gather() {
    SystemInfoSnapshot snapshot{};
    snapshot.cpu = GatherCpu();
    snapshot.gpu = GatherGpu();
    snapshot.ram = GatherRam();
    snapshot.os_kernel = GatherOsKernel();
    snapshot.apu_guard = GatherApuGuard();
    return snapshot;
}

CpuInfo SystemInfo::GatherCpu() {
    CpuInfo cpu{};
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    cpu.brand_string = hw.cpu_topology.brand_string;
    cpu.physical_cores = hw.cpu_topology.physical_cores;
    cpu.logical_processors = hw.cpu_topology.logical_processors;

    for (ULONG id : hw.cpu_topology.game_optimal_cpu_set_ids) {
        cpu.active_game_cpu_sets.push_back(static_cast<uint32_t>(id));
    }
    for (ULONG id : hw.cpu_topology.background_cpu_set_ids) {
        cpu.active_background_cpu_sets.push_back(static_cast<uint32_t>(id));
    }

    // 1. Detect Microarchitecture from CPUID Family/Model or Brand String
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 1);
    const int family = ((cpu_info[0] >> 8) & 0xF) + ((cpu_info[0] >> 20) & 0xFF);
    const int model = ((cpu_info[0] >> 4) & 0xF) | (((cpu_info[0] >> 16) & 0xF) << 4);

    if (cpu.brand_string.find("4600H") != std::string::npos ||
        (family == 0x17 && model >= 0x60 && model <= 0x6F)) {
        cpu.microarchitecture = "Zen 2 Renoir 7nm";
    } else if (family == 0x17) {
        cpu.microarchitecture = "Zen 2 7nm";
    } else if (family == 0x19) {
        if (model >= 0x40 && model <= 0x4F) {
            cpu.microarchitecture = "Zen 3+ Rembrandt 6nm";
        } else if (model >= 0x50 && model <= 0x5F) {
            cpu.microarchitecture = "Zen 3 Cezanne 7nm";
        } else {
            cpu.microarchitecture = "Zen 3 / Zen 4";
        }
    } else if (family == 0x1A) {
        cpu.microarchitecture = "Zen 5 4nm";
    } else if (cpu.brand_string.find("Ryzen") != std::string::npos) {
        cpu.microarchitecture = "Zen 2 Renoir 7nm";
    } else {
        cpu.microarchitecture = "x86_64";
    }

    // 2. Query L3 Cache via GetLogicalProcessorInformationEx (RelationCache) or CPUID
    uint64_t total_l3_bytes = 0;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
    if (len > 0) {
        std::vector<BYTE> buffer(len);
        if (GetLogicalProcessorInformationEx(RelationCache,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &len)) {
            const BYTE* ptr = buffer.data();
            const BYTE* end = ptr + len;
            while (ptr < end) {
                const auto info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
                if (info->Relationship == RelationCache && info->Cache.Level == 3) {
                    total_l3_bytes += info->Cache.CacheSize;
                }
                ptr += info->Size;
            }
        }
    }

    if (total_l3_bytes == 0) {
        int ext_info[4] = {0};
        __cpuid(ext_info, 0x80000006);
        total_l3_bytes = static_cast<uint64_t>(static_cast<unsigned int>(ext_info[3]) >> 18) * 512ULL * 1024ULL;
    }

    if (total_l3_bytes == 0) {
        total_l3_bytes = 8ULL * 1024 * 1024; // 8 MB default for Renoir 4600H
    }

    cpu.l3_cache_bytes = total_l3_bytes;
    cpu.l3_cache_mb = static_cast<double>(total_l3_bytes) / (1024.0 * 1024.0);

    // 3. Boost Target and STAPM Ceiling (calibrated per APU Tier policy)
    const auto policy = HardwareProfiler::BuildOptimizationPolicy(hw);
    if (policy.tier == ApuTier::Tier1_EntryUMA) {
        cpu.boost_target_ghz = 3.5;
        cpu.stapm_target = "Smart Uncapped Boost (100% CPU/GPU Efficient-Aggressive + EPP 25)";
    } else if (policy.tier == ApuTier::Tier3_AdvancedUMA || policy.tier == ApuTier::Tier4_DiscreteOrCustom) {
        cpu.boost_target_ghz = 4.4;
        cpu.stapm_target = "Dynamic Boost (Efficient Aggressive / Uncapped Guard)";
    } else {
        cpu.boost_target_ghz = 4.0;
        cpu.stapm_target = "4.0 GHz Smart Uncapped (100% CPU + 100% GPU Efficient-Aggressive + EPP 25)";
    }

    return cpu;
}

GpuInfo SystemInfo::GatherGpu() {
    GpuInfo gpu{};
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    // Convert wide description to UTF-8
    std::string desc_utf8;
    if (!hw.primary_gpu.description.empty()) {
        int sz = WideCharToMultiByte(CP_UTF8, 0, hw.primary_gpu.description.data(),
                                     static_cast<int>(hw.primary_gpu.description.size()),
                                     nullptr, 0, nullptr, nullptr);
        if (sz > 0) {
            desc_utf8.resize(sz);
            WideCharToMultiByte(CP_UTF8, 0, hw.primary_gpu.description.data(),
                                static_cast<int>(hw.primary_gpu.description.size()),
                                desc_utf8.data(), sz, nullptr, nullptr);
        }
    }
    if (desc_utf8.empty()) {
        desc_utf8 = "AMD Radeon(TM) Graphics";
    }

    if (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated) {
        if (hw.primary_gpu.vendor == GpuVendor::AMD) {
            if (hw.primary_gpu.arch_gen == GpuArchitectureGen::AMD_RDNA2) {
                gpu.adapter_name = desc_utf8 + " (RDNA 2 UMA)";
            } else if (hw.primary_gpu.arch_gen == GpuArchitectureGen::AMD_RDNA3_Plus) {
                gpu.adapter_name = desc_utf8 + " (RDNA 3/3.5 UMA)";
            } else {
                // AMD_GCN5_Vega or Renoir 4600H fallback
                gpu.adapter_name = desc_utf8 + " (Vega 6 / GCN 5.0)";
            }
        } else if (hw.primary_gpu.vendor == GpuVendor::Intel) {
            if (hw.primary_gpu.arch_gen == GpuArchitectureGen::Intel_Xe_Arc) {
                gpu.adapter_name = desc_utf8 + " (Intel Xe/Arc UMA)";
            } else {
                gpu.adapter_name = desc_utf8;
            }
        } else {
            gpu.adapter_name = desc_utf8;
        }
    } else {
        gpu.adapter_name = desc_utf8;
    }

    gpu.architecture = "UMA (Unified Memory Architecture)";
    gpu.vendor_id = hw.primary_gpu.vendor_id;
    gpu.device_id = hw.primary_gpu.device_id;
    gpu.bios_dedicated_vram_bytes = hw.primary_gpu.dedicated_vram_bytes;
    gpu.bios_dedicated_vram_mb = static_cast<double>(gpu.bios_dedicated_vram_bytes) / (1024.0 * 1024.0);
    gpu.dynamic_shared_vram_bytes = hw.primary_gpu.shared_system_bytes;
    gpu.dynamic_shared_vram_gb = static_cast<double>(gpu.dynamic_shared_vram_bytes) / (1024.0 * 1024.0 * 1024.0);

    // DXGI Video Memory Budget and Real-Time Usage
    MemoryManager mem_mgr;
    SystemMemoryTelemetry telem = mem_mgr.SampleTelemetry();
    gpu.dxgi_budget.local_budget_bytes = telem.vram.local_budget_bytes;
    gpu.dxgi_budget.local_usage_bytes = telem.vram.local_usage_bytes;
    gpu.dxgi_budget.non_local_budget_bytes = telem.vram.non_local_budget_bytes;
    gpu.dxgi_budget.non_local_usage_bytes = telem.vram.non_local_usage_bytes;
    gpu.dxgi_budget.valid = telem.vram.valid;

    return gpu;
}

RamInfo SystemInfo::GatherRam() {
    RamInfo ram{};
    MemoryManager mem_mgr;
    SystemMemoryTelemetry telem = mem_mgr.SampleTelemetry();

    ram.total_physical_bytes = telem.total_physical_bytes;
    ram.total_physical_mb = static_cast<double>(ram.total_physical_bytes) / (1024.0 * 1024.0);
    ram.total_physical_gb = static_cast<double>(ram.total_physical_bytes) / (1024.0 * 1024.0 * 1024.0);

    ram.free_real_bytes = telem.free_and_zero_bytes;
    ram.free_real_mb = static_cast<double>(ram.free_real_bytes) / (1024.0 * 1024.0);

    ram.standby_cache_bytes = telem.standby_bytes;
    ram.standby_cache_mb = static_cast<double>(ram.standby_cache_bytes) / (1024.0 * 1024.0);

    ram.modified_bytes = telem.modified_bytes;
    ram.modified_mb = static_cast<double>(ram.modified_bytes) / (1024.0 * 1024.0);

    const uint64_t non_in_use = ram.free_real_bytes + ram.standby_cache_bytes;
    if (ram.total_physical_bytes > non_in_use) {
        ram.in_use_bytes = ram.total_physical_bytes - non_in_use;
    } else {
        ram.in_use_bytes = 0;
    }
    ram.in_use_mb = static_cast<double>(ram.in_use_bytes) / (1024.0 * 1024.0);

    // RAM Channel Topology via SMBIOS 'RSMB' (0x52534D42) Type 17
    constexpr DWORD kRsmbSignature = 0x52534D42;
    DWORD smbios_size = GetSystemFirmwareTable(kRsmbSignature, 0, nullptr, 0);
    int populated_slots = 0;

    if (smbios_size > 8) {
        std::vector<BYTE> smbios_buf(smbios_size);
        if (GetSystemFirmwareTable(kRsmbSignature, 0, smbios_buf.data(), smbios_size) == smbios_size) {
            const BYTE* table = smbios_buf.data() + 8;
            const DWORD table_len = smbios_size - 8;
            const BYTE* ptr = table;
            const BYTE* end = table + table_len;

            while (ptr + 4 <= end) {
                const BYTE type = ptr[0];
                const BYTE length = ptr[1];
                if (length < 4) break;
                if (type == 127) break; // End of table
                if (type == 17) {       // Memory Device
                    if (length >= 0x0E) {
                        const WORD size_val = *reinterpret_cast<const WORD*>(ptr + 0x0C);
                        if (size_val > 0 && size_val != 0xFFFF) {
                            populated_slots++;
                        }
                    }
                }
                ptr += length;
                while (ptr + 1 < end && !(ptr[0] == 0 && ptr[1] == 0)) {
                    ptr++;
                }
                ptr += 2;
            }
        }
    }

    if (populated_slots >= 2) {
        ram.channel_topology = "Dual-Channel (128-bit bus bandwidth)";
        ram.populated_dimm_slots = static_cast<uint32_t>(populated_slots);
        ram.memory_bus_width_bits = 128;
    } else if (populated_slots == 1) {
        ram.channel_topology = "Single-Channel (64-bit bus bandwidth)";
        ram.populated_dimm_slots = 1;
        ram.memory_bus_width_bits = 64;
    } else {
        // Fallback for virtualized or restricted firmware environments
        if (ram.total_physical_bytes >= 12ULL * 1024 * 1024 * 1024) {
            ram.channel_topology = "Dual-Channel (128-bit bus bandwidth)";
            ram.populated_dimm_slots = 2;
            ram.memory_bus_width_bits = 128;
        } else {
            ram.channel_topology = "Single-Channel (64-bit bus bandwidth)";
            ram.populated_dimm_slots = 1;
            ram.memory_bus_width_bits = 64;
        }
    }

    return ram;
}

OsKernelInfo SystemInfo::GatherOsKernel() {
    OsKernelInfo os{};

    // Read Windows NT CurrentVersion from registry
    HKEY hKey = nullptr;
    std::string current_build;
    std::string display_version;
    DWORD ubr = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD buf_size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "CurrentBuild", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buffer), &buf_size) == ERROR_SUCCESS) {
            current_build = buffer;
        }
        buf_size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "DisplayVersion", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buffer), &buf_size) == ERROR_SUCCESS) {
            display_version = buffer;
        }
        DWORD ubr_val = 0;
        DWORD ubr_sz = sizeof(ubr_val);
        if (RegQueryValueExA(hKey, "UBR", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&ubr_val), &ubr_sz) == ERROR_SUCCESS) {
            ubr = ubr_val;
        }
        RegCloseKey(hKey);
    }

    uint32_t build_num = 0;
    try {
        if (!current_build.empty()) {
            build_num = static_cast<uint32_t>(std::stoul(current_build));
        }
    } catch (...) {}

    if (build_num == 0) {
        using PFN_RtlGetVersion = LONG(NTAPI*)(PRTL_OSVERSIONINFOW);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto pRtlGetVersion = reinterpret_cast<PFN_RtlGetVersion>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (pRtlGetVersion) {
            RTL_OSVERSIONINFOW os_ver{};
            os_ver.dwOSVersionInfoSize = sizeof(os_ver);
            if (pRtlGetVersion(&os_ver) == 0) {
                build_num = os_ver.dwBuildNumber;
            }
        }
    }
    if (build_num == 0) {
        build_num = 22631;
    }

    os.build_number = build_num;
    os.ubr = ubr;
    os.display_version = display_version.empty() ? "23H2" : display_version;
    os.os_name = (build_num >= 22000) ? "Windows 11" : "Windows 10";

    os.full_version_string = os.os_name + " Build " + std::to_string(build_num);
    if (ubr > 0) {
        os.full_version_string += "." + std::to_string(ubr);
    }

    // High precision timer resolution
    TimerResolutionState timer = GraphicsOptimizer::QueryTimerResolution();
    os.timer_resolution_ns100 = timer.current_ns100;
    os.timer_resolution_ms = static_cast<double>(timer.current_ns100) / 10000.0;
    os.is_high_resolution_timer = timer.active_high_res;

    // MMCSS and Memory Compression status
    SystemGraphicsTweaksState tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
    os.mmcss_gaming_optimized = tweaks.mmcss_gaming_optimized;
    os.mmcss_gpu_priority = 8;
    os.memory_compression_enabled = tweaks.memory_compression_enabled;

    return os;
}

ApuGuardInfo SystemInfo::GatherApuGuard() {
    ApuGuardInfo guard{};
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    MemoryManager mem_mgr;
    SystemMemoryTelemetry telem = mem_mgr.SampleTelemetry();
    uint64_t live_budget = telem.vram.valid ? telem.vram.local_budget_bytes : 0;

    ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw, live_budget);

    guard.tier_name = HardwareProfiler::ApuTierToString(policy.tier);
    guard.guard_mode = policy.guard_mode_name;
    guard.arch_generation = HardwareProfiler::ArchGenToString(policy.arch_gen);
    guard.safe_vram_pool_mb = policy.safe_vram_pool_mb;
    guard.os_reserve_floor_mb = policy.os_reserve_floor_mb;
    guard.dxvk_compiler_threads = policy.dxvk_compiler_threads;
    guard.amd_tessellation_cap = policy.amd_tessellation_cap;
    guard.dx12_async_compute_allowed = !policy.disable_dx12_async_compute;
    guard.cpu_stapm_cap_mhz = policy.cpu_stapm_cap_mhz;

    return guard;
}

std::string SystemInfo::ToJson() const {
    return ToJson(snapshot_);
}

std::string SystemInfo::ToJson(const SystemInfoSnapshot& snapshot) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "{\n";

    // 1. CPU Section
    ss << "  \"cpu\": {\n";
    ss << "    \"brand_string\": \"" << EscapeJson(snapshot.cpu.brand_string) << "\",\n";
    ss << "    \"physical_cores\": " << snapshot.cpu.physical_cores << ",\n";
    ss << "    \"logical_processors\": " << snapshot.cpu.logical_processors << ",\n";
    ss << "    \"microarchitecture\": \"" << EscapeJson(snapshot.cpu.microarchitecture) << "\",\n";
    ss << "    \"l3_cache_bytes\": " << snapshot.cpu.l3_cache_bytes << ",\n";
    ss << "    \"l3_cache_mb\": " << snapshot.cpu.l3_cache_mb << ",\n";
    ss << "    \"boost_target_ghz\": " << snapshot.cpu.boost_target_ghz << ",\n";
    ss << "    \"stapm_target\": \"" << EscapeJson(snapshot.cpu.stapm_target) << "\",\n";
    ss << "    \"active_game_cpu_sets\": " << FormatUintVector(snapshot.cpu.active_game_cpu_sets) << ",\n";
    ss << "    \"active_background_cpu_sets\": " << FormatUintVector(snapshot.cpu.active_background_cpu_sets) << ",\n";
    ss << "    \"cpu_sets_active\": {\n";
    ss << "      \"game_sets\": " << FormatUintVector(snapshot.cpu.active_game_cpu_sets) << ",\n";
    ss << "      \"background_sets\": " << FormatUintVector(snapshot.cpu.active_background_cpu_sets) << "\n";
    ss << "    }\n";
    ss << "  },\n";

    // 2. GPU Section
    ss << "  \"gpu\": {\n";
    ss << "    \"adapter_name\": \"" << EscapeJson(snapshot.gpu.adapter_name) << "\",\n";
    ss << "    \"architecture\": \"" << EscapeJson(snapshot.gpu.architecture) << "\",\n";
    ss << "    \"memory_architecture\": \"" << EscapeJson(snapshot.gpu.architecture) << "\",\n";
    ss << "    \"vendor_id\": " << snapshot.gpu.vendor_id << ",\n";
    ss << "    \"device_id\": " << snapshot.gpu.device_id << ",\n";
    ss << "    \"bios_dedicated_vram_bytes\": " << snapshot.gpu.bios_dedicated_vram_bytes << ",\n";
    ss << "    \"bios_dedicated_vram_mb\": " << snapshot.gpu.bios_dedicated_vram_mb << ",\n";
    ss << "    \"dynamic_shared_vram_bytes\": " << snapshot.gpu.dynamic_shared_vram_bytes << ",\n";
    ss << "    \"dynamic_shared_vram_gb\": " << snapshot.gpu.dynamic_shared_vram_gb << ",\n";
    ss << "    \"dxgi_budget\": {\n";
    ss << "      \"local_budget_bytes\": " << snapshot.gpu.dxgi_budget.local_budget_bytes << ",\n";
    ss << "      \"local_usage_bytes\": " << snapshot.gpu.dxgi_budget.local_usage_bytes << ",\n";
    ss << "      \"non_local_budget_bytes\": " << snapshot.gpu.dxgi_budget.non_local_budget_bytes << ",\n";
    ss << "      \"non_local_usage_bytes\": " << snapshot.gpu.dxgi_budget.non_local_usage_bytes << ",\n";
    ss << "      \"valid\": " << (snapshot.gpu.dxgi_budget.valid ? "true" : "false") << "\n";
    ss << "    }\n";
    ss << "  },\n";

    // 3. RAM Section
    ss << "  \"ram\": {\n";
    ss << "    \"total_physical_bytes\": " << snapshot.ram.total_physical_bytes << ",\n";
    ss << "    \"total_physical_mb\": " << snapshot.ram.total_physical_mb << ",\n";
    ss << "    \"total_physical_gb\": " << snapshot.ram.total_physical_gb << ",\n";
    ss << "    \"free_real_bytes\": " << snapshot.ram.free_real_bytes << ",\n";
    ss << "    \"free_real_mb\": " << snapshot.ram.free_real_mb << ",\n";
    ss << "    \"in_use_bytes\": " << snapshot.ram.in_use_bytes << ",\n";
    ss << "    \"in_use_mb\": " << snapshot.ram.in_use_mb << ",\n";
    ss << "    \"standby_cache_bytes\": " << snapshot.ram.standby_cache_bytes << ",\n";
    ss << "    \"standby_cache_mb\": " << snapshot.ram.standby_cache_mb << ",\n";
    ss << "    \"modified_bytes\": " << snapshot.ram.modified_bytes << ",\n";
    ss << "    \"modified_mb\": " << snapshot.ram.modified_mb << ",\n";
    ss << "    \"channel_topology\": \"" << EscapeJson(snapshot.ram.channel_topology) << "\",\n";
    ss << "    \"populated_dimm_slots\": " << snapshot.ram.populated_dimm_slots << ",\n";
    ss << "    \"memory_bus_width_bits\": " << snapshot.ram.memory_bus_width_bits << "\n";
    ss << "  },\n";

    // 4. OS and Kernel Section
    ss << "  \"os_kernel\": {\n";
    ss << "    \"os_name\": \"" << EscapeJson(snapshot.os_kernel.os_name) << "\",\n";
    ss << "    \"display_version\": \"" << EscapeJson(snapshot.os_kernel.display_version) << "\",\n";
    ss << "    \"build_number\": " << snapshot.os_kernel.build_number << ",\n";
    ss << "    \"ubr\": " << snapshot.os_kernel.ubr << ",\n";
    ss << "    \"full_version_string\": \"" << EscapeJson(snapshot.os_kernel.full_version_string) << "\",\n";
    ss << "    \"timer_resolution\": {\n";
    ss << "      \"current_ms\": " << snapshot.os_kernel.timer_resolution_ms << ",\n";
    ss << "      \"current_ns100\": " << snapshot.os_kernel.timer_resolution_ns100 << ",\n";
    ss << "      \"is_high_precision\": " << (snapshot.os_kernel.is_high_resolution_timer ? "true" : "false") << "\n";
    ss << "    },\n";
    ss << "    \"mmcss_gaming_profile\": {\n";
    ss << "      \"is_optimized\": " << (snapshot.os_kernel.mmcss_gaming_optimized ? "true" : "false") << ",\n";
    ss << "      \"gpu_priority\": " << snapshot.os_kernel.mmcss_gpu_priority << "\n";
    ss << "    },\n";
    ss << "    \"memory_compression\": {\n";
    ss << "      \"enabled\": " << (snapshot.os_kernel.memory_compression_enabled ? "true" : "false") << "\n";
    ss << "    }\n";
    ss << "  },\n";

    // 5. APU Guard Section
    ss << "  \"apu_guard\": {\n";
    ss << "    \"tier\": \"" << EscapeJson(snapshot.apu_guard.tier_name) << "\",\n";
    ss << "    \"tier_name\": \"" << EscapeJson(snapshot.apu_guard.tier_name) << "\",\n";
    ss << "    \"guard_mode\": \"" << EscapeJson(snapshot.apu_guard.guard_mode) << "\",\n";
    ss << "    \"arch_generation\": \"" << EscapeJson(snapshot.apu_guard.arch_generation) << "\",\n";
    ss << "    \"architecture_generation\": \"" << EscapeJson(snapshot.apu_guard.arch_generation) << "\",\n";
    ss << "    \"safe_vram_pool_mb\": " << snapshot.apu_guard.safe_vram_pool_mb << ",\n";
    ss << "    \"os_reserve_floor_mb\": " << snapshot.apu_guard.os_reserve_floor_mb << ",\n";
    ss << "    \"dxvk_compiler_threads\": " << snapshot.apu_guard.dxvk_compiler_threads << ",\n";
    ss << "    \"amd_tessellation_cap\": " << snapshot.apu_guard.amd_tessellation_cap << ",\n";
    ss << "    \"tessellation_cap\": " << snapshot.apu_guard.amd_tessellation_cap << ",\n";
    ss << "    \"dx12_async_compute_allowed\": " << (snapshot.apu_guard.dx12_async_compute_allowed ? "true" : "false") << ",\n";
    ss << "    \"cpu_stapm_cap_mhz\": " << snapshot.apu_guard.cpu_stapm_cap_mhz << "\n";
    ss << "  }\n";

    ss << "}\n";

    return ss.str();
}

} // namespace vegaflow
