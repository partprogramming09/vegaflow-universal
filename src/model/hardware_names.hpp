#pragma once
// Plain names of the hardware enums (what reports and the GUI print). Pure: no Windows, no Rust.

#include "model/hardware.hpp"

namespace vegaflow {

inline const char* VendorToString(GpuVendor v) {
    switch (v) {
        case GpuVendor::AMD:    return "AMD";
        case GpuVendor::Intel:  return "Intel";
        case GpuVendor::NVIDIA: return "NVIDIA";
        case GpuVendor::Unknown:
        default:                return "Unknown";
    }
}

inline const char* MemArchToString(MemoryArchitecture m) {
    switch (m) {
        case MemoryArchitecture::UMA_Integrated: return "UMA_Integrated";
        case MemoryArchitecture::Dedicated_VRAM: return "Dedicated_VRAM";
        case MemoryArchitecture::Unknown:
        default:                                 return "Unknown";
    }
}

inline const char* ConnectionToString(GpuConnection c) {
    switch (c) {
        case GpuConnection::Integrated: return "Integrada";
        case GpuConnection::Discrete:   return "Dedicada";
        case GpuConnection::External:   return "Externa";
        case GpuConnection::Unknown:
        default:                        return "No se pudo leer";
    }
}

inline const char* DiskTypeToString(GameDiskType d) {
    switch (d) {
        case GameDiskType::HDD:     return "HDD";
        case GameDiskType::SSD:     return "SSD";
        case GameDiskType::NVMe:    return "NVMe";
        case GameDiskType::Unknown:
        default:                    return "No se pudo leer";
    }
}

inline const char* SystemArchToString(SystemArch a) {
    switch (a) {
        case SystemArch::X64:            return "x64";
        case SystemArch::ARM64_Emulated: return "ARM64 (x64 emulado)";
        case SystemArch::ARM64_Native:   return "ARM64";
        case SystemArch::X86:            return "x86";
        case SystemArch::Unknown:
        default:                         return "No se pudo leer";
    }
}

inline const char* ApuTierToString(ApuTier tier) {
    switch (tier) {
        case ApuTier::Tier1_EntryUMA:        return "Tier1_EntryUMA";
        case ApuTier::Tier2_MidVegaUMA:      return "Tier2_MidVegaUMA";
        case ApuTier::Tier3_AdvancedUMA:     return "Tier3_AdvancedUMA";
        case ApuTier::Tier4_DiscreteOrCustom:return "Tier4_DiscreteOrCustom";
        default:                             return "Unknown";
    }
}

inline const char* ArchGenToString(GpuArchitectureGen gen) {
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

} // namespace vegaflow
