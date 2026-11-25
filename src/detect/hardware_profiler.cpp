#include "detect/hardware_profiler.hpp"
#include "bridge/rust_bridge.h"
#include "bridge/hardware_policy.hpp"
#include "model/hardware_names.hpp"
#include "windows/device_probe.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

const char* HardwareProfiler::VendorToString(GpuVendor v) { return ::vegaflow::VendorToString(v); }

const char* HardwareProfiler::MemArchToString(MemoryArchitecture m) { return ::vegaflow::MemArchToString(m); }

const char* HardwareProfiler::ConnectionToString(GpuConnection c) { return ::vegaflow::ConnectionToString(c); }

const char* HardwareProfiler::DiskTypeToString(GameDiskType d) { return ::vegaflow::DiskTypeToString(d); }

const char* HardwareProfiler::SystemArchToString(SystemArch a) { return ::vegaflow::SystemArchToString(a); }

const char* HardwareProfiler::ApuTierToString(ApuTier tier) { return ::vegaflow::ApuTierToString(tier); }

const char* HardwareProfiler::ArchGenToString(GpuArchitectureGen gen) { return ::vegaflow::ArchGenToString(gen); }

namespace {

std::string WideToUtf8Profiler(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

} // namespace

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

ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes) {
    return ::vegaflow::BuildOptimizationPolicy(profile, live_dxgi_local_budget_bytes);
}

void HardwareProfiler::PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology) {
    // The group of the processor this thread runs on (a machine with more than 64 logical processors has several groups).
    PROCESSOR_NUMBER proc_num{};
    GetCurrentProcessorNumberEx(&proc_num);
    ::vegaflow::PartitionCpuSets(cpu_topology, proc_num.Group);
}

} // namespace vegaflow
