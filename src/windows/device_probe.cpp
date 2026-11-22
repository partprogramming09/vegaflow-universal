#include "windows/device_probe.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgi.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <winioctl.h>
#include <intrin.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

namespace dxcore_abi {

// Minimal COM interface declarations for DXCore (dxcore_interface.h is absent from MinGW).
// Kept outside the anonymous namespace (external linkage) so the compiler never assumes a closed
// hierarchy and devirtualizes external COM calls into __cxa_pure_virtual.
enum class VfDXCoreAdapterProperty : uint32_t {
    InstanceLuid = 0,
    DriverVersion = 1,
    DriverDescription = 2,
    HardwareID = 3,
    KmdModelVersion = 4,
    ComputePreemptionGranularity = 5,
    GraphicsPreemptionGranularity = 6,
    DedicatedAdapterMemory = 7,
    DedicatedSystemMemory = 8,
    SharedSystemMemory = 9,
    AcgCompatible = 10,
    IsHardware = 11,
    IsIntegrated = 12,
    IsDetachable = 13,
    HardwareIDParts = 14,
};

struct VfDXCoreHardwareID {
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t subSysID;
    uint32_t revision;
};

struct IVfDXCoreAdapter : public IUnknown {
    virtual bool STDMETHODCALLTYPE IsValid() = 0;
    virtual bool STDMETHODCALLTYPE IsAttributeSupported(REFGUID attribute) = 0;
    virtual bool STDMETHODCALLTYPE IsPropertySupported(VfDXCoreAdapterProperty property) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(VfDXCoreAdapterProperty property, size_t buffer_size, void* buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertySize(VfDXCoreAdapterProperty property, size_t* buffer_size) = 0;
    virtual bool STDMETHODCALLTYPE IsQueryStateSupported(uint32_t state) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryState(uint32_t state, size_t details_sz, const void* details, size_t buf_sz, void* buf) = 0;
    virtual bool STDMETHODCALLTYPE IsSetStateSupported(uint32_t state) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetState(uint32_t state, size_t details_sz, const void* details, size_t buf_sz, const void* buf) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFactory(REFIID riid, void** ppv) = 0;
};

struct IVfDXCoreAdapterList : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetAdapter(uint32_t index, REFIID riid, void** ppv) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetAdapterCount() = 0;
    virtual bool STDMETHODCALLTYPE IsStale() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFactory(REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE Sort(uint32_t num_preferences, const uint32_t* preferences) = 0;
    virtual bool STDMETHODCALLTYPE IsAdapterPreferenceSupported(uint32_t preference) = 0;
};

struct IVfDXCoreAdapterFactory : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateAdapterList(uint32_t num_attributes, const GUID* filter_attributes, REFIID riid, void** ppv) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdapterByLuid(const LUID& adapter_luid, REFIID riid, void** ppv) = 0;
    virtual bool STDMETHODCALLTYPE IsNotificationTypeSupported(uint32_t type) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterEventNotification(IUnknown* obj, uint32_t type, void* cb, void* ctx, uint32_t* cookie) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterEventNotification(uint32_t cookie) = 0;
};

} // namespace dxcore_abi

namespace {

using dxcore_abi::IVfDXCoreAdapter;
using dxcore_abi::IVfDXCoreAdapterFactory;
using dxcore_abi::IVfDXCoreAdapterList;
using dxcore_abi::VfDXCoreAdapterProperty;
using dxcore_abi::VfDXCoreHardwareID;

const GUID kIID_IDXCoreAdapterFactory = {0x78ee5945, 0xc36e, 0x4b13, {0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06}};
const GUID kIID_IDXCoreAdapterList = {0x526c7776, 0x40e9, 0x459b, {0xb7, 0x11, 0xf3, 0x2a, 0xd7, 0x6d, 0xfc, 0x28}};
const GUID kIID_IDXCoreAdapter = {0xf0db4c7f, 0xfe5a, 0x42a2, {0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e}};
const GUID kDXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS = {0x0c9ece4d, 0x2f6e, 0x4f01, {0x8c, 0x96, 0xe8, 0x9e, 0x33, 0x1b, 0x47, 0xb1}};
const GUID kDXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS = {0x8c47866b, 0x7583, 0x450d, {0xf0, 0xf0, 0x6b, 0xad, 0xa8, 0x95, 0xaf, 0x4b}};

// Display device class & removal policy SetupAPI key
const GUID kGuidDevClassDisplay = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
const DEVPROPKEY kDevPkeyDeviceRemovalPolicy = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 33};
constexpr uint32_t kCmRemovalPolicyExpectOrderly = 2;
constexpr uint32_t kCmRemovalPolicyExpectSurprise = 3;

std::string WideToUtf8Local(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

GpuVendor VendorFromId(uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x1002: return GpuVendor::AMD;
        case 0x8086: return GpuVendor::Intel;
        case 0x10DE: return GpuVendor::NVIDIA;
        default:     return GpuVendor::Unknown;
    }
}

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
    while (*start == ' ' || *start == '\t') ++start;
    std::string s(start);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\0')) s.pop_back();
    return s;
}

void QueryCpuFamilyModel(uint32_t& out_family, uint32_t& out_model) {
    int cpu_info[4] = {0};
    __cpuid(cpu_info, 1);
    const uint32_t raw = static_cast<uint32_t>(cpu_info[0]);
    out_family = ((raw >> 8) & 0xFu) + ((raw >> 20) & 0xFFu);
    out_model = ((raw >> 4) & 0xFu) | (((raw >> 16) & 0xFu) << 4);
}

bool QueryDxCoreBoolProperty(IVfDXCoreAdapter* adapter, VfDXCoreAdapterProperty prop, bool& out_val) {
    size_t sz = 0;
    if (FAILED(adapter->GetPropertySize(prop, &sz)) || sz == 0 || sz > 8) {
        return false;
    }
    uint8_t buf[8]{};
    if (FAILED(adapter->GetProperty(prop, sz, buf))) {
        return false;
    }
    if (sz == 1) {
        out_val = (buf[0] != 0);
        return true;
    }
    if (sz == 4) {
        uint32_t v = 0;
        std::copy(buf, buf + 4, reinterpret_cast<uint8_t*>(&v));
        out_val = (v != 0);
        return true;
    }
    return false;
}

bool ParseHexAfterTag(const std::wstring& lower, const wchar_t* tag, uint32_t& out_val) {
    size_t pos = lower.find(tag);
    if (pos == std::wstring::npos) return false;
    pos += wcslen(tag);
    wchar_t* end_ptr = nullptr;
    unsigned long v = wcstoul(lower.c_str() + pos, &end_ptr, 16);
    if (end_ptr == lower.c_str() + pos) return false;
    out_val = static_cast<uint32_t>(v);
    return true;
}

// Queries SetupAPI for display devices whose removal policy expects orderly or surprise removal (eGPU indicator).
std::set<std::pair<uint32_t, uint32_t>> QueryDetachableDisplayDevices() {
    std::set<std::pair<uint32_t, uint32_t>> detachable;
    HMODULE hSetupApi = LoadLibraryW(L"setupapi.dll");
    if (!hSetupApi) return detachable;

    using PFN_GetClassDevsW = HDEVINFO(WINAPI*)(const GUID*, PCWSTR, HWND, DWORD);
    using PFN_DestroyDeviceInfoList = BOOL(WINAPI*)(HDEVINFO);
    using PFN_EnumDeviceInfo = BOOL(WINAPI*)(HDEVINFO, DWORD, PSP_DEVINFO_DATA);
    using PFN_GetDevicePropertyW = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, const DEVPROPKEY*, DEVPROPTYPE*, PBYTE, DWORD, PDWORD, DWORD);
    using PFN_GetDeviceRegistryPropertyW = BOOL(WINAPI*)(HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pGetClassDevsW = reinterpret_cast<PFN_GetClassDevsW>(GetProcAddress(hSetupApi, "SetupDiGetClassDevsW"));
    auto pDestroyList = reinterpret_cast<PFN_DestroyDeviceInfoList>(GetProcAddress(hSetupApi, "SetupDiDestroyDeviceInfoList"));
    auto pEnumInfo = reinterpret_cast<PFN_EnumDeviceInfo>(GetProcAddress(hSetupApi, "SetupDiEnumDeviceInfo"));
    auto pGetDevPropW = reinterpret_cast<PFN_GetDevicePropertyW>(GetProcAddress(hSetupApi, "SetupDiGetDevicePropertyW"));
    auto pGetRegPropW = reinterpret_cast<PFN_GetDeviceRegistryPropertyW>(GetProcAddress(hSetupApi, "SetupDiGetDeviceRegistryPropertyW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (pGetClassDevsW && pDestroyList && pEnumInfo && pGetDevPropW && pGetRegPropW) {
        HDEVINFO devs = pGetClassDevsW(&kGuidDevClassDisplay, nullptr, nullptr, DIGCF_PRESENT);
        if (devs != INVALID_HANDLE_VALUE) {
            SP_DEVINFO_DATA did{};
            did.cbSize = sizeof(did);
            for (DWORD i = 0; pEnumInfo(devs, i, &did); ++i) {
                DEVPROPTYPE prop_type = 0;
                uint32_t removal_policy = 0;
                if (!pGetDevPropW(devs, &did, &kDevPkeyDeviceRemovalPolicy, &prop_type,
                                  reinterpret_cast<PBYTE>(&removal_policy), sizeof(removal_policy), nullptr, 0)) {
                    continue;
                }
                if (removal_policy != kCmRemovalPolicyExpectOrderly &&
                    removal_policy != kCmRemovalPolicyExpectSurprise) {
                    continue;
                }
                wchar_t hw_id[1024]{};
                DWORD reg_type = 0;
                if (pGetRegPropW(devs, &did, SPDRP_HARDWAREID, &reg_type,
                                 reinterpret_cast<PBYTE>(hw_id), sizeof(hw_id) - sizeof(wchar_t), nullptr)) {
                    std::wstring lower(hw_id);
                    for (auto& c : lower) c = static_cast<wchar_t>(std::towlower(c));
                    uint32_t ven = 0, dev = 0;
                    if (ParseHexAfterTag(lower, L"ven_", ven) && ParseHexAfterTag(lower, L"dev_", dev)) {
                        detachable.insert({ven, dev});
                    }
                }
            }
            pDestroyList(devs);
        }
    }

    FreeLibrary(hSetupApi);
    return detachable;
}

bool ProbeGpusWithDxCore(
    const std::set<std::pair<uint32_t, uint32_t>>& detachable_devs,
    std::vector<GpuProfile>& out_gpus,
    bool& out_dxcore_available) {

    out_dxcore_available = false;
    HMODULE hDxCore = LoadLibraryW(L"dxcore.dll");
    if (!hDxCore) return false;

    using PFN_DXCoreCreateAdapterFactory = HRESULT(WINAPI*)(REFIID, void**);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pCreateFactory = reinterpret_cast<PFN_DXCoreCreateAdapterFactory>(
        GetProcAddress(hDxCore, "DXCoreCreateAdapterFactory"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (!pCreateFactory) {
        FreeLibrary(hDxCore);
        return false;
    }

    IVfDXCoreAdapterFactory* factory = nullptr;
    if (FAILED(pCreateFactory(kIID_IDXCoreAdapterFactory, reinterpret_cast<void**>(&factory))) || !factory) {
        FreeLibrary(hDxCore);
        return false;
    }

    // DXCore exported and created a factory: feature exists on this OS build.
    out_dxcore_available = true;

    const GUID attrs[2] = {kDXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS, kDXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS};
    IVfDXCoreAdapterList* list = nullptr;
    HRESULT hr = factory->CreateAdapterList(1, &attrs[0], kIID_IDXCoreAdapterList, reinterpret_cast<void**>(&list));
    if (FAILED(hr) || !list || list->GetAdapterCount() == 0) {
        if (list) list->Release();
        list = nullptr;
        hr = factory->CreateAdapterList(1, &attrs[1], kIID_IDXCoreAdapterList, reinterpret_cast<void**>(&list));
    }

    if (SUCCEEDED(hr) && list) {
        const uint32_t count = list->GetAdapterCount();
        std::set<uint64_t> seen_luids;
        for (uint32_t i = 0; i < count; ++i) {
            IVfDXCoreAdapter* adapter = nullptr;
            if (FAILED(list->GetAdapter(i, kIID_IDXCoreAdapter, reinterpret_cast<void**>(&adapter))) || !adapter) {
                continue;
            }

            bool is_hw = true;
            if (QueryDxCoreBoolProperty(adapter, VfDXCoreAdapterProperty::IsHardware, is_hw) && !is_hw) {
                adapter->Release();
                continue;
            }

            VfDXCoreHardwareID hwid{};
            if (FAILED(adapter->GetProperty(VfDXCoreAdapterProperty::HardwareID, sizeof(hwid), &hwid))) {
                adapter->Release();
                continue;
            }
            if (hwid.vendorID == 0x1414) { // Microsoft Basic Render Driver
                adapter->Release();
                continue;
            }

            LUID luid{};
            uint64_t luid_u64 = 0;
            if (SUCCEEDED(adapter->GetProperty(VfDXCoreAdapterProperty::InstanceLuid, sizeof(luid), &luid))) {
                luid_u64 = (static_cast<uint64_t>(static_cast<uint32_t>(luid.HighPart)) << 32) |
                           static_cast<uint64_t>(luid.LowPart);
                if (luid_u64 != 0 && !seen_luids.insert(luid_u64).second) {
                    adapter->Release();
                    continue;
                }
            }

            GpuProfile gpu{};
            gpu.vendor_id = hwid.vendorID;
            gpu.device_id = hwid.deviceID;
            gpu.luid = luid_u64;
            gpu.vendor = VendorFromId(hwid.vendorID);
            gpu.dxcore_available = true;

            size_t desc_sz = 0;
            if (SUCCEEDED(adapter->GetPropertySize(VfDXCoreAdapterProperty::DriverDescription, &desc_sz)) &&
                desc_sz > 0 && desc_sz <= 1024) {
                std::string utf8_desc(desc_sz, '\0');
                if (SUCCEEDED(adapter->GetProperty(VfDXCoreAdapterProperty::DriverDescription, desc_sz, utf8_desc.data()))) {
                    while (!utf8_desc.empty() && utf8_desc.back() == '\0') utf8_desc.pop_back();
                    gpu.description = Utf8ToWideLocal(utf8_desc);
                }
            }

            uint64_t drv_ver = 0;
            if (SUCCEEDED(adapter->GetProperty(VfDXCoreAdapterProperty::DriverVersion, sizeof(drv_ver), &drv_ver)) && drv_ver != 0) {
                const unsigned v1 = static_cast<unsigned>((drv_ver >> 48) & 0xFFFFu);
                const unsigned v2 = static_cast<unsigned>((drv_ver >> 32) & 0xFFFFu);
                const unsigned v3 = static_cast<unsigned>((drv_ver >> 16) & 0xFFFFu);
                const unsigned v4 = static_cast<unsigned>(drv_ver & 0xFFFFu);
                gpu.driver_version = std::to_string(v1) + "." + std::to_string(v2) + "." +
                                     std::to_string(v3) + "." + std::to_string(v4);
            }

            uint64_t ded_vram = 0;
            if (SUCCEEDED(adapter->GetProperty(VfDXCoreAdapterProperty::DedicatedAdapterMemory, sizeof(ded_vram), &ded_vram))) {
                gpu.dedicated_vram_bytes = ded_vram;
            }
            uint64_t shared_mem = 0;
            if (SUCCEEDED(adapter->GetProperty(VfDXCoreAdapterProperty::SharedSystemMemory, sizeof(shared_mem), &shared_mem))) {
                gpu.shared_system_bytes = shared_mem;
            }

            QueryDxCoreBoolProperty(adapter, VfDXCoreAdapterProperty::IsIntegrated, gpu.dxcore_integrated);
            QueryDxCoreBoolProperty(adapter, VfDXCoreAdapterProperty::IsDetachable, gpu.dxcore_detachable);
            gpu.removal_policy_detachable = (detachable_devs.count({gpu.vendor_id, gpu.device_id}) > 0);

            if (gpu.dxcore_detachable || (gpu.removal_policy_detachable && !gpu.dxcore_integrated)) {
                gpu.connection = GpuConnection::External;
                gpu.mem_arch = MemoryArchitecture::Dedicated_VRAM;
            } else if (gpu.dxcore_integrated) {
                gpu.connection = GpuConnection::Integrated;
                gpu.mem_arch = MemoryArchitecture::UMA_Integrated;
            } else {
                gpu.connection = GpuConnection::Discrete;
                gpu.mem_arch = MemoryArchitecture::Dedicated_VRAM;
            }

            out_gpus.push_back(std::move(gpu));
            adapter->Release();
        }
        list->Release();
    }

    factory->Release();
    FreeLibrary(hDxCore);
    return !out_gpus.empty();
}

void SupplementOrFallbackWithDxgi(
    const std::set<std::pair<uint32_t, uint32_t>>& detachable_devs,
    bool dxcore_available,
    std::vector<GpuProfile>& io_gpus) {

    HMODULE dxgi_lib = LoadLibraryW(L"dxgi.dll");
    if (!dxgi_lib) return;

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
            for (UINT i = 0;; ++i) {
                // Any failure ends the walk: only DXGI_ERROR_NOT_FOUND is the normal end, and on others `adapter` is not valid.
                if (FAILED(factory->EnumAdapters1(i, &adapter)) || !adapter) break;
                DXGI_ADAPTER_DESC1 desc{};
                if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                    const bool is_software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || (desc.VendorId == 0x1414);
                    if (!is_software) {
                        const uint64_t luid_u64 = (static_cast<uint64_t>(static_cast<uint32_t>(desc.AdapterLuid.HighPart)) << 32) |
                                                  static_cast<uint64_t>(desc.AdapterLuid.LowPart);
                        GpuProfile* existing = nullptr;
                        for (auto& g : io_gpus) {
                            if ((luid_u64 != 0 && g.luid == luid_u64) ||
                                (g.vendor_id == desc.VendorId && g.device_id == desc.DeviceId)) {
                                existing = &g;
                                break;
                            }
                        }
                        if (existing) {
                            if (existing->description.empty()) existing->description = desc.Description;
                            if (existing->dedicated_vram_bytes == 0) existing->dedicated_vram_bytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
                            if (existing->shared_system_bytes == 0) existing->shared_system_bytes = static_cast<uint64_t>(desc.SharedSystemMemory);
                        } else if (!dxcore_available || io_gpus.empty()) {
                            GpuProfile gpu{};
                            gpu.description = desc.Description;
                            gpu.vendor_id = desc.VendorId;
                            gpu.device_id = desc.DeviceId;
                            gpu.luid = luid_u64;
                            gpu.vendor = VendorFromId(desc.VendorId);
                            gpu.dedicated_vram_bytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
                            gpu.shared_system_bytes = static_cast<uint64_t>(desc.SharedSystemMemory);
                            gpu.dxcore_available = false;
                            gpu.removal_policy_detachable = (detachable_devs.count({gpu.vendor_id, gpu.device_id}) > 0);
                            // Phase 3 rule: without DXCore, never infer connection from the 2 GB heuristic.
                            gpu.connection = gpu.removal_policy_detachable ? GpuConnection::External : GpuConnection::Unknown;
                            gpu.mem_arch = MemoryArchitecture::Unknown;
                            io_gpus.push_back(std::move(gpu));
                        }
                    }
                }
                adapter->Release();
                adapter = nullptr;
            }
            factory->Release();
        }
    }
    FreeLibrary(dxgi_lib);
}

PowerSupplyStatus ProbePowerSupply() {
    PowerSupplyStatus p{};
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) {
        return p;
    }
    if (sps.ACLineStatus == 255) {
        // 255 means unknown AC status -> keep p.known = false (prudent rule: never touch power plan).
        p.known = false;
        return p;
    }
    p.known = true;
    p.ac_online = (sps.ACLineStatus == 1);
    p.has_battery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
    p.battery_saver = (sps.SystemStatusFlag == 1);
    p.battery_percent = sps.BatteryLifePercent;
    return p;
}

SystemArch ProbeSystemArchitecture() {
    HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
    if (hKernel) {
        using PFN_IsWow64Process2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto pIsWow64Process2 = reinterpret_cast<PFN_IsWow64Process2>(
            GetProcAddress(hKernel, "IsWow64Process2"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (pIsWow64Process2) {
            USHORT process_machine = 0;
            USHORT native_machine = 0;
            if (pIsWow64Process2(GetCurrentProcess(), &process_machine, &native_machine)) {
                constexpr USHORT kArm64 = 0xAA64;
                constexpr USHORT kAmd64 = 0x8664;
                constexpr USHORT kI386 = 0x014C;
                if (native_machine == kArm64) {
#if defined(_M_ARM64) || defined(__aarch64__)
                    return SystemArch::ARM64_Native;
#else
                    return SystemArch::ARM64_Emulated;
#endif
                }
                if (native_machine == kAmd64) return SystemArch::X64;
                if (native_machine == kI386) return SystemArch::X86;
            }
        }
    }

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return SystemArch::X64;
        case PROCESSOR_ARCHITECTURE_INTEL: return SystemArch::X86;
#ifdef PROCESSOR_ARCHITECTURE_ARM64
        case PROCESSOR_ARCHITECTURE_ARM64:
#if defined(_M_ARM64) || defined(__aarch64__)
            return SystemArch::ARM64_Native;
#else
            return SystemArch::ARM64_Emulated;
#endif
#endif
        default: return SystemArch::Unknown;
    }
}

WindowsVersionInfo ProbeWindowsVersion(bool dxcore_available) {
    WindowsVersionInfo info{};
    info.dxcore_available = dxcore_available;

    HKEY hKey = nullptr;
    std::string current_build;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buf[256]{};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExA(hKey, "CurrentBuild", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS) {
            current_build = buf;
            info.known = true;
        }
        sz = sizeof(buf);
        if (RegQueryValueExA(hKey, "DisplayVersion", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS) {
            info.display_version = buf;
        }
        sz = sizeof(buf);
        if (RegQueryValueExA(hKey, "ProductName", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS) {
            info.product_name = buf;
        }
        DWORD ubr = 0;
        DWORD ubr_sz = sizeof(ubr);
        if (RegQueryValueExA(hKey, "UBR", nullptr, nullptr, reinterpret_cast<LPBYTE>(&ubr), &ubr_sz) == ERROR_SUCCESS) {
            info.ubr = ubr;
        }
        RegCloseKey(hKey);
    }

    if (!current_build.empty()) {
        try {
            info.build = static_cast<uint32_t>(std::stoul(current_build));
            info.major = 10;
            info.minor = 0;
        } catch (...) {
            info.build = 0;
        }
    }

    if (info.build == 0) {
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
                info.major = os_ver.dwMajorVersion;
                info.minor = os_ver.dwMinorVersion;
                info.build = os_ver.dwBuildNumber;
                info.known = true;
            }
        }
    }

    return info;
}

std::vector<DisplayMonitorInfo> ProbeDisplays() {
    std::vector<DisplayMonitorInfo> displays;
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0) {
            DEVMODEW dm{};
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                DisplayMonitorInfo m{};
                m.device_name = WideToUtf8Local(dd.DeviceName);
                m.width = dm.dmPelsWidth;
                m.height = dm.dmPelsHeight;
                m.refresh_hz = dm.dmDisplayFrequency;
                m.primary = ((dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0);
                displays.push_back(std::move(m));
            }
        }
        dd = DISPLAY_DEVICEW{};
        dd.cb = sizeof(dd);
    }
    return displays;
}

uint64_t ComputeAdapterSignature(const std::vector<GpuProfile>& gpus) {
    // FNV-1a 64-bit hash over sorted (vendor_id, device_id, luid, connection).
    std::vector<uint64_t> tokens;
    tokens.reserve(gpus.size());
    for (const auto& g : gpus) {
        uint64_t t = (static_cast<uint64_t>(g.vendor_id) << 32) |
                     static_cast<uint64_t>(g.device_id);
        t ^= g.luid * 0x9E3779B97F4A7C15ULL;
        t ^= static_cast<uint64_t>(g.connection) << 56;
        tokens.push_back(t);
    }
    std::sort(tokens.begin(), tokens.end());
    uint64_t h = 14695981039346656037ULL;
    for (uint64_t t : tokens) {
        for (int b = 0; b < 8; ++b) {
            h ^= static_cast<uint8_t>((t >> (b * 8)) & 0xFFu);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

void DetectCpuSetsRaw(CpuTopologyProfile& cpu_topology) {
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
                if (info->Size == 0) break;
                if (info->Type == CpuSetInformation) {
                    CpuCoreSetInfo core_set{};
                    core_set.cpu_set_id = info->CpuSet.Id;
                    core_set.core_index = info->CpuSet.CoreIndex;
                    core_set.logical_processor_index = info->CpuSet.LogicalProcessorIndex;
                    core_set.group = info->CpuSet.Group;
                    core_set.last_level_cache_index = info->CpuSet.LastLevelCacheIndex;
                    core_set.efficiency_class = info->CpuSet.EfficiencyClass;
                    cpu_topology.all_cpu_sets.push_back(core_set);
                }
                ptr += info->Size;
            }
        }
    }
}

HANDLE OpenVolumeOrDeviceNoWrite(const wchar_t* device_path) {
    // Prefer dwDesiredAccess = 0 (metadata query only); fall back to GENERIC_READ only if denied.
    HANDLE h = CreateFileW(device_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        return h;
    }
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        h = CreateFileW(device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, 0, nullptr);
    }
    return h;
}

} // namespace

RawDeviceSnapshot WindowsDeviceProbe::ProbeSystem(bool include_displays) {
    RawDeviceSnapshot snap{};

    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status) && mem_status.ullTotalPhys > 0) {
        snap.total_physical_ram_bytes = mem_status.ullTotalPhys;
        snap.ram_probe_ok = true;
    }

    const auto detachable_devs = QueryDetachableDisplayDevices();
    bool dxcore_ok = ProbeGpusWithDxCore(detachable_devs, snap.gpus, snap.dxcore_available);
    SupplementOrFallbackWithDxgi(detachable_devs, snap.dxcore_available, snap.gpus);
    (void)dxcore_ok;
    snap.gpu_probe_ok = !snap.gpus.empty();
    snap.adapter_signature = ComputeAdapterSignature(snap.gpus);

    DetectCpuSetsRaw(snap.cpu_topology);
    snap.cpu_probe_ok = !snap.cpu_topology.all_cpu_sets.empty() || !snap.cpu_topology.brand_string.empty();
    QueryCpuFamilyModel(snap.cpu_family, snap.cpu_model);

    snap.power_status = ProbePowerSupply();
    snap.system_arch = ProbeSystemArchitecture();
    snap.windows_version = ProbeWindowsVersion(snap.dxcore_available);
    if (include_displays) {
        snap.displays = ProbeDisplays();
    }

    return snap;
}

GameDiskType WindowsDeviceProbe::ProbeDiskType(const std::wstring& path) {
    if (path.empty()) return GameDiskType::Unknown;

    wchar_t vol_root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), vol_root, MAX_PATH)) {
        return GameDiskType::Unknown;
    }

    std::wstring vol_dev(vol_root);
    while (!vol_dev.empty() && vol_dev.back() == L'\\') {
        vol_dev.pop_back();
    }
    if (vol_dev.empty()) return GameDiskType::Unknown;
    if (vol_dev.rfind(L"\\\\.\\", 0) != 0 && vol_dev.rfind(L"\\\\?\\", 0) != 0) {
        vol_dev = L"\\\\.\\" + vol_dev;
    }

    HANDLE hVol = OpenVolumeOrDeviceNoWrite(vol_dev.c_str());
    if (hVol == INVALID_HANDLE_VALUE) {
        return GameDiskType::Unknown;
    }

    struct {
        DWORD NumberOfDiskExtents;
        DISK_EXTENT Extents[4];
    } extents_buf{};
    DWORD bytes_ret = 0;
    const BOOL got_ext = DeviceIoControl(
        hVol,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        nullptr,
        0,
        &extents_buf,
        sizeof(extents_buf),
        &bytes_ret,
        nullptr);
    CloseHandle(hVol);

    // Multi-disk volumes (RAID / Storage Spaces) or unreadable volumes stay Unknown ("No se pudo leer").
    if (!got_ext || extents_buf.NumberOfDiskExtents != 1) {
        return GameDiskType::Unknown;
    }

    const std::wstring phys_path = L"\\\\.\\PhysicalDrive" + std::to_wstring(extents_buf.Extents[0].DiskNumber);
    HANDLE hDisk = OpenVolumeOrDeviceNoWrite(phys_path.c_str());
    if (hDisk == INVALID_HANDLE_VALUE) {
        return GameDiskType::Unknown;
    }

    // Query 1: Seek penalty (HDD vs solid-state).
    STORAGE_PROPERTY_QUERY seek_q{};
    seek_q.PropertyId = StorageDeviceSeekPenaltyProperty;
    seek_q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek_desc{};
    const BOOL got_seek = DeviceIoControl(
        hDisk,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &seek_q,
        sizeof(seek_q),
        &seek_desc,
        sizeof(seek_desc),
        &bytes_ret,
        nullptr);

    if (got_seek && seek_desc.IncursSeekPenalty) {
        CloseHandle(hDisk);
        return GameDiskType::HDD;
    }

    // Query 2: BusType (NVMe vs SATA/other SSD vs USB/virtual).
    STORAGE_PROPERTY_QUERY dev_q{};
    dev_q.PropertyId = StorageDeviceProperty;
    dev_q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> dev_buf(512, 0);
    const BOOL got_dev = DeviceIoControl(
        hDisk,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &dev_q,
        sizeof(dev_q),
        dev_buf.data(),
        static_cast<DWORD>(dev_buf.size()),
        &bytes_ret,
        nullptr);
    CloseHandle(hDisk);

    if (!got_dev || bytes_ret < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return GameDiskType::Unknown;
    }
    const auto* dev_desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(dev_buf.data());
    if (dev_desc->BusType == BusTypeUsb ||
        dev_desc->BusType == BusTypeRAID ||
        dev_desc->BusType == BusTypeSpaces ||
        dev_desc->BusType == BusTypeVirtual ||
        dev_desc->BusType == BusTypeFileBackedVirtual) {
        return GameDiskType::Unknown;
    }
    if (dev_desc->BusType == BusTypeNvme) {
        return GameDiskType::NVMe;
    }
    if (got_seek) {
        return GameDiskType::SSD;
    }
    return GameDiskType::Unknown;
}

uint8_t WindowsDeviceProbe::QueryAppGpuPreference(const std::wstring& exe_full_path) {
    if (exe_full_path.empty()) return 0;

    HKEY hPref = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences",
                      0, KEY_READ, &hPref) != ERROR_SUCCESS) {
        return 0;
    }

    wchar_t val[512]{};
    DWORD val_sz = sizeof(val);
    DWORD type = 0;
    uint8_t pref = 0;
    if (RegQueryValueExW(hPref, exe_full_path.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(val), &val_sz) == ERROR_SUCCESS &&
        type == REG_SZ) {
        std::wstring s(val);
        for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
        if (s.find(L"gpupreference=1") != std::wstring::npos) {
            pref = 1;
        } else if (s.find(L"gpupreference=2") != std::wstring::npos) {
            pref = 2;
        }
    }
    RegCloseKey(hPref);
    return pref;
}

uint64_t WindowsDeviceProbe::QueryAdapterSignature() {
    const auto detachable_devs = QueryDetachableDisplayDevices();
    std::vector<GpuProfile> gpus;
    bool dxcore_ok = false;
    ProbeGpusWithDxCore(detachable_devs, gpus, dxcore_ok);
    SupplementOrFallbackWithDxgi(detachable_devs, dxcore_ok, gpus);
    return ComputeAdapterSignature(gpus);
}

} // namespace vegaflow
