#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

constexpr wchar_t kDwmPath[] = L"SOFTWARE\\Microsoft\\Windows\\Dwm";
constexpr wchar_t kGameConfigStorePath[] = L"System\\GameConfigStore";
constexpr wchar_t kGameDvrPath[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\GameDVR";

} // namespace

bool DisplayTweaks::SetMpoDisabled(bool disable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    if (disable) {
        return reg_.WriteValue(
            RegHive::LocalMachine,
            kDwmPath,
            L"OverlayTestMode",
            RegValue::MakeDword(5u),
            true,
            "Desactivar MPO (DWM)",
            "5");
    }

    if (reg_.RestoreBackedUpValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode")) {
        return true;
    }
    // No copy: save the current value first (DeleteValue never copies), so nothing is lost.
    RegValue current;
    if (!reg_.ReadValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode", current)) {
        return true; // already absent
    }
    if (!reg_.WriteValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode", current, true,
                         "Devolver MPO (DWM)", "")) {
        return false;
    }
    if (reg_.DeleteValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode", true)) {
        return true;
    }
    return reg_.WriteValue(
        RegHive::LocalMachine,
        kDwmPath,
        L"OverlayTestMode",
        RegValue::MakeDword(0u),
        true);
}

void DisplayTweaks::QueryMpoState(bool& out_disabled, uint32_t& out_overlay_test_mode) {
    out_disabled = false;
    out_overlay_test_mode = 0;

    RegValue val;
    if (reg_.ReadValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode", val) &&
        val.type == RegValueType::Dword) {
        out_overlay_test_mode = val.dword;
        out_disabled = (val.dword == 5u);
    }
}

bool DisplayTweaks::SetGameDvrCaptureDisabled(bool disable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    // Giving it back is never a write: with VegaFlow's copy the user's value returns, without one it was never touched.
    if (!disable) return RestoreGameDvrCapture(false);

    struct Item { const wchar_t* path; const wchar_t* name; const char* reason; };
    static constexpr Item kItems[] = {
        {kGameConfigStorePath, L"GameDVR_Enabled", "Desactivar GameDVR"},
        {kGameDvrPath, L"AppCaptureEnabled", "Desactivar GameDVR (AppCapture)"},
    };
    bool success = true;
    for (const Item& it : kItems) {
        if (!reg_.WriteValue(RegHive::CurrentUser, it.path, it.name, RegValue::MakeDword(0u), true, it.reason, "0")) {
            success = false;
        }
    }
    return success;
}

bool DisplayTweaks::RestoreGameDvrCapture(bool dry_run) {
    if (dry_run) return true;
    // No copy means VegaFlow never turned it off: the user's value stays as it is.
    reg_.RestoreBackedUpValue(RegHive::CurrentUser, kGameConfigStorePath, L"GameDVR_Enabled");
    reg_.RestoreBackedUpValue(RegHive::CurrentUser, kGameDvrPath, L"AppCaptureEnabled");
    return true;
}

bool DisplayTweaks::QueryGameDvrDisabled() {
    RegValue val;
    if (reg_.ReadValue(RegHive::CurrentUser, kGameConfigStorePath, L"GameDVR_Enabled", val) &&
        val.type == RegValueType::Dword) {
        return val.dword == 0u;
    }
    return false;
}

bool DisplayTweaks::SetFullscreenOptimizationsDisabled(bool disable, bool dry_run) {
    static constexpr const wchar_t* kNames[] = {
        L"GameDVR_FSEBehaviorMode",
        L"GameDVR_HonorUserFSEBehaviorMode",
        L"GameDVR_DXGIHonorFSEWindowsCompatible",
        L"GameDVR_EFSEFeatureFlags",
    };
    static constexpr uint32_t kDisabled[] = {2u, 1u, 1u, 0u};
    if (dry_run) {
        return true;
    }

    bool success = true;
    for (size_t i = 0; i < 4; ++i) {
        if (disable) {
            success = reg_.WriteValue(RegHive::CurrentUser, kGameConfigStorePath, kNames[i], RegValue::MakeDword(kDisabled[i]),
                                      true, "Compatibilidad: desactivar optimizaciones de pantalla completa",
                                      std::to_string(kDisabled[i])) && success;
        } else {
            // Only the saved original goes back; without a backup the user's value is left alone.
            reg_.RestoreBackedUpValue(RegHive::CurrentUser, kGameConfigStorePath, kNames[i]);
        }
    }
    return success;
}

bool DisplayTweaks::QueryFullscreenOptimizationsDisabled() {
    RegValue val;
    return reg_.ReadValue(RegHive::CurrentUser, kGameConfigStorePath, L"GameDVR_FSEBehaviorMode", val) &&
           val.type == RegValueType::Dword && val.dword == 2u;
}

} // namespace vegaflow
