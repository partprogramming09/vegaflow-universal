#include "optimize/graphics/graphics_internal.hpp"

#include <cstdlib>
#include <vector>

namespace vegaflow {

using namespace graphics_internal;

namespace {

bool WriteUmdCharFlag(
    RegistryPort& reg,
    const std::wstring& umd_path,
    const wchar_t* name,
    uint8_t ascii_digit,
    bool backup_and_journal,
    const std::string& reason) {
    RegValue existing;
    const std::string preview(1, static_cast<char>(ascii_digit));
    if (reg.ReadValue(RegHive::LocalMachine, umd_path, name, existing) &&
        existing.type == RegValueType::String) {
        const std::wstring wstr(1, static_cast<wchar_t>(ascii_digit));
        return reg.WriteValue(
            RegHive::LocalMachine, umd_path, name, RegValue::MakeString(wstr),
            backup_and_journal, reason, preview);
    }
    return reg.WriteValue(
        RegHive::LocalMachine, umd_path, name, RegValue::MakeBinary({ascii_digit, 0x00}),
        backup_and_journal, reason, preview);
}

bool WriteUmdTessellation(
    RegistryPort& reg,
    const std::wstring& umd_path,
    const std::string& tess_str,
    bool backup_and_journal,
    const std::string& reason) {
    RegValue existing;
    if (reg.ReadValue(RegHive::LocalMachine, umd_path, L"Tessellation", existing) &&
        existing.type == RegValueType::String) {
        const std::wstring wstr(tess_str.begin(), tess_str.end());
        return reg.WriteValue(
            RegHive::LocalMachine, umd_path, L"Tessellation", RegValue::MakeString(wstr),
            backup_and_journal, reason, tess_str);
    }
    std::vector<uint8_t> tess_bytes(tess_str.begin(), tess_str.end());
    tess_bytes.push_back(0x00);
    return reg.WriteValue(
        RegHive::LocalMachine, umd_path, L"Tessellation", RegValue::MakeBinary(std::move(tess_bytes)),
        backup_and_journal, reason, tess_str);
}

} // namespace

uint32_t AmdDriverTweaks::QueryTessellationCap() {
    bool sc = false;
    bool uma = false;
    uint32_t tess = 0;
    QueryAdapterState(sc, uma, tess);
    return tess;
}

bool AmdDriverTweaks::SetDx11UmaDriverBoost(bool enable, const ApuOptimizationPolicy& policy, bool dry_run) {
    if (dry_run) {
        return true;
    }

    const uint32_t active_tess_cap = enable ? policy.amd_tessellation_cap : 64u;
    const std::string tess_str = std::to_string(active_tess_cap);

    const uint8_t sfr_digit = static_cast<uint8_t>(enable ? '1' : '0');
    const uint8_t fqs_digit = static_cast<uint8_t>('1');
    const uint8_t tfq_digit = static_cast<uint8_t>(enable ? '2' : '0');
    const uint8_t tess_opt_digit = static_cast<uint8_t>(enable ? '1' : '0');
    const uint8_t main3d_digit = static_cast<uint8_t>(enable ? '1' : '0');
    const uint8_t turbosync_digit = static_cast<uint8_t>('0');
    const uint8_t dfqo_digit = static_cast<uint8_t>(enable ? '0' : '1');

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path)) {
            continue;
        }

        const std::wstring umd_path = adapter_path + L"\\UMD";
        const bool ok1 = WriteUmdCharFlag(reg_, umd_path, L"SurfaceFormatReplacements", sfr_digit, enable, "AMD DX11 UMA boost");
        const bool ok2 = WriteUmdCharFlag(reg_, umd_path, L"FlipQueueSize", fqs_digit, enable, "AMD DX11 UMA boost");
        const bool ok3 = WriteUmdCharFlag(reg_, umd_path, L"TFQ", tfq_digit, enable, "AMD DX11 UMA boost");
        const bool ok4 = WriteUmdCharFlag(reg_, umd_path, L"Tessellation_OPTION", tess_opt_digit, enable, "AMD DX11 UMA boost");
        const bool ok5 = WriteUmdTessellation(reg_, umd_path, tess_str, enable, "AMD DX11 UMA boost");
        const bool ok6 = WriteUmdCharFlag(reg_, umd_path, L"Main3D_DEF", main3d_digit, enable, "AMD DX11 UMA boost");
        const bool ok7 = WriteUmdCharFlag(reg_, umd_path, L"TurboSync", turbosync_digit, enable, "AMD DX11 UMA boost");
        const bool ok8 = WriteUmdCharFlag(reg_, umd_path, L"DisableFlipQueueOptimization", dfqo_digit, enable, "AMD DX11 UMA boost");

        if (!enable) {
            for (const wchar_t* n : kUmdBoostValues) {
                reg_.RestoreBackedUpValue(RegHive::LocalMachine, umd_path, n);
            }
        }
        if (ok1 || ok2 || ok3 || ok4 || ok5 || ok6 || ok7 || ok8) {
            updated_any = true;
        }
    }

    return updated_any;
}

bool AmdDriverTweaks::SetTessellationCap(uint32_t max_tess, bool dry_run) {
    if (dry_run) {
        return true;
    }

    const bool enable_override = (max_tess > 0 && max_tess < 64);
    const uint8_t tess_opt_digit = static_cast<uint8_t>(enable_override ? '1' : '0');
    const std::string tess_str = (max_tess > 0) ? std::to_string(max_tess) : "64";

    bool updated_any = false;

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path)) {
            continue;
        }

        const std::wstring umd_path = adapter_path + L"\\UMD";
        const bool ok_opt = WriteUmdCharFlag(reg_, umd_path, L"Tessellation_OPTION", tess_opt_digit, enable_override, "AMD tessellation cap");
        const bool ok_tess = WriteUmdTessellation(reg_, umd_path, tess_str, enable_override, "AMD tessellation cap");

        if (!enable_override) {
            reg_.RestoreBackedUpValue(RegHive::LocalMachine, umd_path, L"Tessellation_OPTION");
            reg_.RestoreBackedUpValue(RegHive::LocalMachine, umd_path, L"Tessellation");
        }
        if (ok_opt || ok_tess) {
            updated_any = true;
        }
    }

    return updated_any;
}

} // namespace vegaflow
