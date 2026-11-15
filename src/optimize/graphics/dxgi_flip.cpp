#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

namespace {

constexpr wchar_t kUserGpuPrefsPath[] = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
constexpr wchar_t kAppCompatLayersPath[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";

void UpdateOrInsertParam(std::wstring& str, const std::wstring& key, const std::wstring& val) {
    const std::wstring lower = ToLower(str);
    const std::wstring target_key = ToLower(key) + L"=";
    const auto pos = lower.find(target_key);
    const std::wstring insertion = key + L"=" + val + L";";
    if (pos != std::wstring::npos) {
        const auto semi = str.find(L';', pos);
        if (semi != std::wstring::npos) {
            str.replace(pos, semi - pos + 1, insertion);
        } else {
            str.replace(pos, str.size() - pos, insertion);
        }
    } else {
        if (!str.empty() && str.back() != L';') {
            str.push_back(L';');
        }
        str += insertion;
    }
}

void RemoveParam(std::wstring& str, const std::wstring& key) {
    std::wstring lower = ToLower(str);
    const std::wstring target_key = ToLower(key) + L"=";
    auto pos = lower.find(target_key);
    while (pos != std::wstring::npos) {
        const auto semi = str.find(L';', pos);
        if (semi != std::wstring::npos) {
            str.erase(pos, semi - pos + 1);
        } else {
            str.erase(pos);
        }
        lower = ToLower(str);
        pos = lower.find(target_key);
    }
}

} // namespace

bool DxgiFlipTweaks::ApplyFlipModelUpgrade(
    const std::wstring& exe_full_path,
    bool dry_run,
    std::wstring* out_effective_preference) {
    if (exe_full_path.empty()) {
        return false;
    }

    std::wstring final_value = L"SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;";

    RegValue existing;
    if (reg_.ReadValue(RegHive::CurrentUser, kUserGpuPrefsPath, exe_full_path, existing) &&
        existing.type == RegValueType::String && !existing.str.empty()) {
        std::wstring updated = existing.str;
        UpdateOrInsertParam(updated, L"SwapEffectUpgradeEnable", L"1");
        UpdateOrInsertParam(updated, L"GpuPreference", L"2");
        UpdateOrInsertParam(updated, L"AutoHDREnable", L"0");
        RemoveParam(updated, L"AutoFlipModel");
        RemoveParam(updated, L"DirectFlip");
        final_value = std::move(updated);
    }

    if (out_effective_preference) {
        *out_effective_preference = final_value;
    }

    if (dry_run) {
        return true;
    }

    const uint64_t j_id = reg_.BeginDxgiPreferenceChange(exe_full_path, final_value, "DXGI Flip Model upgrade");
    const bool ok = reg_.WriteValue(
        RegHive::CurrentUser,
        kUserGpuPrefsPath,
        exe_full_path,
        RegValue::MakeString(final_value),
        false);

    if (ok) {
        reg_.WriteValue(
            RegHive::CurrentUser,
            kAppCompatLayersPath,
            exe_full_path,
            RegValue::MakeString(L"~ HIGHDPIAWARE"),
            false);
    }

    reg_.EndDxgiPreferenceChange(j_id, ok);
    return ok;
}

bool DxgiFlipTweaks::ApplyDirectFlipUpgrade(
    const std::wstring& exe_full_path,
    bool dry_run,
    std::wstring* out_effective_preference) {
    if (exe_full_path.empty()) {
        return false;
    }

    std::wstring final_value = L"SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;AutoFlipModel=1;DirectFlip=1;";

    RegValue existing;
    if (reg_.ReadValue(RegHive::CurrentUser, kUserGpuPrefsPath, exe_full_path, existing) &&
        existing.type == RegValueType::String && !existing.str.empty()) {
        std::wstring updated = existing.str;
        UpdateOrInsertParam(updated, L"SwapEffectUpgradeEnable", L"1");
        UpdateOrInsertParam(updated, L"GpuPreference", L"2");
        UpdateOrInsertParam(updated, L"AutoHDREnable", L"0");
        UpdateOrInsertParam(updated, L"AutoFlipModel", L"1");
        UpdateOrInsertParam(updated, L"DirectFlip", L"1");
        final_value = std::move(updated);
    }

    if (out_effective_preference) {
        *out_effective_preference = final_value;
    }

    if (dry_run) {
        return true;
    }

    const uint64_t j_id = reg_.BeginDxgiPreferenceChange(exe_full_path, final_value, "DXGI DirectFlip upgrade");
    const bool ok = reg_.WriteValue(
        RegHive::CurrentUser,
        kUserGpuPrefsPath,
        exe_full_path,
        RegValue::MakeString(final_value),
        false);

    if (ok) {
        reg_.WriteValue(
            RegHive::CurrentUser,
            kAppCompatLayersPath,
            exe_full_path,
            RegValue::MakeString(L"~ HIGHDPIAWARE"),
            false);
    }

    reg_.EndDxgiPreferenceChange(j_id, ok);
    return ok;
}

bool DxgiFlipTweaks::QueryUserGpuPreferences(const std::wstring& exe_full_path, std::wstring& out_prefs) {
    out_prefs.clear();
    if (exe_full_path.empty()) {
        return false;
    }

    RegValue val;
    if (!reg_.ReadValue(RegHive::CurrentUser, kUserGpuPrefsPath, exe_full_path, val) ||
        val.type != RegValueType::String || val.str.empty()) {
        return false;
    }

    out_prefs = std::move(val.str);
    return true;
}

bool DxgiFlipTweaks::RemoveFlipModelUpgrade(const std::wstring& exe_full_path, bool dry_run) {
    if (exe_full_path.empty()) {
        return false;
    }
    if (dry_run) {
        return true;
    }

    reg_.DeleteValue(RegHive::CurrentUser, kUserGpuPrefsPath, exe_full_path, false);
    reg_.DeleteValue(RegHive::CurrentUser, kAppCompatLayersPath, exe_full_path, false);
    reg_.MarkDxgiPreferenceReverted(exe_full_path);
    return true;
}

bool DxgiFlipTweaks::HasDirectFlipUpgrade(const std::wstring& exe_full_path) {
    std::wstring val;
    if (!QueryUserGpuPreferences(exe_full_path, val)) {
        return false;
    }
    const std::wstring lower = ToLower(val);
    return (lower.find(L"autoflipmodel=1") != std::wstring::npos &&
            lower.find(L"directflip=1") != std::wstring::npos);
}

bool DxgiFlipTweaks::HasFlipModelUpgrade(const std::wstring& exe_full_path) {
    std::wstring val;
    if (!QueryUserGpuPreferences(exe_full_path, val)) {
        return false;
    }
    const std::wstring lower = ToLower(val);
    return (lower.find(L"swapeffectupgradeenable=1") != std::wstring::npos);
}

} // namespace vegaflow
