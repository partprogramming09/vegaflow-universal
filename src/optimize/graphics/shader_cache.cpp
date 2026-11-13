#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool AmdDriverTweaks::SetShaderCacheForced(bool force_on, const ApuOptimizationPolicy& policy, bool dry_run) {
    if (dry_run) {
        return true;
    }

    SetDx11UmaDriverBoost(force_on, policy, dry_run);

    const std::vector<uint8_t> binary_val = { static_cast<uint8_t>(force_on ? 0x32 : 0x31), 0x00 };
    const std::string preview = force_on ? "0x3200" : "0x3100";

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path)) {
            continue;
        }

        const bool st1 = reg_.WriteValue(
            RegHive::LocalMachine, adapter_path, L"ShaderCache", RegValue::MakeBinary(binary_val),
            force_on, "AMD ShaderCache forced", preview);
        if (!force_on) {
            reg_.RestoreBackedUpValue(RegHive::LocalMachine, adapter_path, L"ShaderCache");
        }
        if (st1) {
            updated_any = true;
        }

        const std::wstring umd_path = adapter_path + L"\\UMD";
        if (reg_.KeyExists(RegHive::LocalMachine, umd_path)) {
            const bool st2 = reg_.WriteValue(
                RegHive::LocalMachine, umd_path, L"ShaderCache", RegValue::MakeBinary(binary_val),
                force_on, "AMD UMD ShaderCache forced", preview);
            if (!force_on) {
                reg_.RestoreBackedUpValue(RegHive::LocalMachine, umd_path, L"ShaderCache");
            }
            if (st2) {
                updated_any = true;
            }
        }
    }

    return updated_any;
}

std::vector<ShaderCacheReport> DisplayTweaks::InspectShaderCaches() {
    return reg_.InspectShaderCaches();
}

} // namespace vegaflow
