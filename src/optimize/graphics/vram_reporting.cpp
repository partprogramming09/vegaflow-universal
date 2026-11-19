#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool AmdDriverTweaks::ApplyVramReportingOverride(uint32_t target_mb, bool dry_run) {
    if (dry_run) {
        return true;
    }
    // The 512 MB-style override only makes sense for the iGPU's shared pool; never on an eGPU or dGPU.
    if (!allow_vram_override_) {
        return false;
    }

    bool updated_driver = false;
    const uint64_t target_bytes = static_cast<uint64_t>(target_mb) * 1024ULL * 1024ULL;
    const uint32_t dword_bytes = (target_bytes > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : static_cast<uint32_t>(target_bytes);
    const std::string reason = "AMD VRAM reporting override (" + std::to_string(target_mb) + " MB)";
    const std::string dx_reason = "DirectX VRAM reporting override (" + std::to_string(target_mb) + " MB)";

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path, device_id_)) {
            continue;
        }
        const bool s1 = reg_.WriteValue(
            RegHive::LocalMachine, adapter_path, L"HardwareInformation.MemorySize",
            RegValue::MakeDword(dword_bytes), true, reason, std::to_string(dword_bytes));
        const bool s2 = reg_.WriteValue(
            RegHive::LocalMachine, adapter_path, L"HardwareInformation.qwMemorySize",
            RegValue::MakeQword(target_bytes), true, reason, std::to_string(target_bytes));
        if (s1 || s2) {
            updated_driver = true;
        }
    }

    bool updated_dxgi = false;
    for (const auto& sub : reg_.EnumSubkeys(RegHive::LocalMachine, kDirectXBasePath)) {
        const std::wstring dx_subkey = std::wstring(kDirectXBasePath) + L"\\" + sub;
        if (!IsAmdDirectXSubkey(reg_, dx_subkey, device_id_)) {
            continue;
        }
        if (reg_.WriteValue(
                RegHive::LocalMachine, dx_subkey, L"DedicatedVideoMemory",
                RegValue::MakeQword(target_bytes), true, dx_reason, std::to_string(target_bytes))) {
            updated_dxgi = true;
        }
    }

    return updated_driver || updated_dxgi;
}

bool AmdDriverTweaks::RestoreVramReporting(bool dry_run) {
    if (dry_run) {
        return true;
    }

    // Only what VegaFlow saved is put back. With no saved value nothing is written: a made-up size (the old 512 MB default)
    // would replace what the driver really reports on a machine where the override was never turned on.
    bool restored_driver = false;
    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path, device_id_)) {
            continue;
        }
        const bool r1 = reg_.RestoreBackedUpValue(RegHive::LocalMachine, adapter_path, L"HardwareInformation.MemorySize");
        const bool r2 = reg_.RestoreBackedUpValue(RegHive::LocalMachine, adapter_path, L"HardwareInformation.qwMemorySize");
        restored_driver = restored_driver || r1 || r2;
    }

    bool restored_dxgi = false;
    for (const auto& sub : reg_.EnumSubkeys(RegHive::LocalMachine, kDirectXBasePath)) {
        const std::wstring dx_subkey = std::wstring(kDirectXBasePath) + L"\\" + sub;
        if (!IsAmdDirectXSubkey(reg_, dx_subkey, device_id_)) {
            continue;
        }
        restored_dxgi = reg_.RestoreBackedUpValue(RegHive::LocalMachine, dx_subkey, L"DedicatedVideoMemory") || restored_dxgi;
    }

    return restored_driver || restored_dxgi;
}

bool AmdDriverTweaks::QueryVramReporting(uint32_t& out_driver_mb, uint64_t& out_dxgi_bytes) {
    out_driver_mb = 0;
    out_dxgi_bytes = 0;

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path, device_id_)) {
            continue;
        }

        RegValue qw_val;
        if (reg_.ReadValue(RegHive::LocalMachine, adapter_path, L"HardwareInformation.qwMemorySize", qw_val)) {
            uint64_t qw = 0;
            if (qw_val.type == RegValueType::Qword) {
                qw = qw_val.qword;
            } else if (qw_val.type == RegValueType::Binary && qw_val.binary.size() >= sizeof(uint64_t)) {
                for (size_t i = 0; i < sizeof(uint64_t); ++i) {
                    qw |= static_cast<uint64_t>(qw_val.binary[i]) << (i * 8);
                }
            }
            if (qw > 0) {
                out_driver_mb = static_cast<uint32_t>(qw / (1024ULL * 1024ULL));
            }
        }

        if (out_driver_mb == 0) {
            RegValue dw_val;
            if (reg_.ReadValue(RegHive::LocalMachine, adapter_path, L"HardwareInformation.MemorySize", dw_val)) {
                uint32_t dw = 0;
                bool valid = false;
                if (dw_val.type == RegValueType::Dword) {
                    dw = dw_val.dword;
                    valid = true;
                } else if (dw_val.type == RegValueType::Binary && dw_val.binary.size() >= sizeof(uint32_t)) {
                    for (size_t i = 0; i < sizeof(uint32_t); ++i) {
                        dw |= static_cast<uint32_t>(dw_val.binary[i]) << (i * 8);
                    }
                    valid = true;
                }
                if (valid) {
                    out_driver_mb = (dw == 0xFFFFFFFFUL) ? 4096u : (dw / (1024u * 1024u));
                }
            }
        }

        if (out_driver_mb > 0) {
            break;
        }
    }

    for (const auto& sub : reg_.EnumSubkeys(RegHive::LocalMachine, kDirectXBasePath)) {
        const std::wstring dx_subkey = std::wstring(kDirectXBasePath) + L"\\" + sub;
        if (!IsAmdDirectXSubkey(reg_, dx_subkey, device_id_)) {
            continue;
        }
        RegValue val;
        if (reg_.ReadValue(RegHive::LocalMachine, dx_subkey, L"DedicatedVideoMemory", val)) {
            uint64_t qw = 0;
            bool valid = false;
            if (val.type == RegValueType::Qword) {
                qw = val.qword;
                valid = true;
            } else if (val.type == RegValueType::Binary && val.binary.size() >= sizeof(uint64_t)) {
                for (size_t i = 0; i < sizeof(uint64_t); ++i) {
                    qw |= static_cast<uint64_t>(val.binary[i]) << (i * 8);
                }
                valid = true;
            }
            if (valid) {
                out_dxgi_bytes = qw;
            }
        }
        if (out_dxgi_bytes > 0) {
            break;
        }
    }

    return (out_driver_mb > 0 || out_dxgi_bytes > 0);
}

} // namespace vegaflow
