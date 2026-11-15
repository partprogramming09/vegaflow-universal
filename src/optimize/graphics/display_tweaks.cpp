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
    if (reg_.DeleteValue(RegHive::LocalMachine, kDwmPath, L"OverlayTestMode", true)) {
        return true;
    }
    return reg_.WriteValue(
        RegHive::LocalMachine,
        kDwmPath,
        L"OverlayTestMode",
        RegValue::MakeDword(0u),
        false);
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

bool DisplayTweaks::SetGameDvrAndFseOptimized(bool optimize, bool dry_run) {
    if (dry_run) {
        return true;
    }

    static constexpr const wchar_t* kStoreValues[] = {
        L"GameDVR_Enabled",
        L"GameDVR_FSEBehaviorMode",
        L"GameDVR_HonorUserFSEBehaviorMode",
        L"GameDVR_DXGIHonorFSEWindowsCompatible",
        L"GameDVR_EFSEFeatureFlags"
    };

    const uint32_t vals[5] = {
        optimize ? 0u : 1u,
        optimize ? 2u : 0u,
        optimize ? 1u : 0u,
        optimize ? 1u : 0u,
        0u
    };

    bool success = true;
    for (size_t i = 0; i < 5; ++i) {
        if (!reg_.WriteValue(
                RegHive::CurrentUser,
                kGameConfigStorePath,
                kStoreValues[i],
                RegValue::MakeDword(vals[i]),
                optimize,
                "Desactivar GameDVR / FSE",
                std::to_string(vals[i]))) {
            success = false;
        }
    }
    if (!optimize) {
        for (const wchar_t* n : kStoreValues) {
            reg_.RestoreBackedUpValue(RegHive::CurrentUser, kGameConfigStorePath, n);
        }
    }

    const uint32_t app_capture = optimize ? 0u : 1u;
    if (!reg_.WriteValue(
            RegHive::CurrentUser,
            kGameDvrPath,
            L"AppCaptureEnabled",
            RegValue::MakeDword(app_capture),
            optimize,
            "Desactivar GameDVR (AppCapture)",
            std::to_string(app_capture))) {
        success = false;
    }
    if (!optimize) {
        reg_.RestoreBackedUpValue(RegHive::CurrentUser, kGameDvrPath, L"AppCaptureEnabled");
    }

    return success;
}

bool DisplayTweaks::QueryGameDvrDisabled() {
    RegValue val;
    if (reg_.ReadValue(RegHive::CurrentUser, kGameConfigStorePath, L"GameDVR_Enabled", val) &&
        val.type == RegValueType::Dword) {
        return val.dword == 0u;
    }
    return false;
}

} // namespace vegaflow
