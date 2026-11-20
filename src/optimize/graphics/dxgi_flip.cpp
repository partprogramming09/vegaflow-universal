#include "optimize/graphics/graphics_internal.hpp"

#include <sstream>

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

// Appends HIGHDPIAWARE to the user's Layers flags; unchanged when a DPI mode is already set (the modes exclude each other).
std::wstring WithHighDpiAware(const std::wstring& layers) {
    if (layers.empty()) {
        return L"~ HIGHDPIAWARE";
    }
    std::wistringstream tokens(ToLower(layers));
    std::wstring tok;
    while (tokens >> tok) {
        if (tok == L"highdpiaware" || tok == L"dpiunaware" || tok == L"gdidpiscaling") {
            return layers;
        }
    }
    return layers + L" HIGHDPIAWARE";
}

std::wstring ReadPerExeString(RegistryPort& reg, const wchar_t* subkey, const std::wstring& exe) {
    RegValue v;
    return (reg.ReadValue(RegHive::CurrentUser, subkey, exe, v) && v.type == RegValueType::String) ? v.str : std::wstring{};
}

} // namespace

bool DxgiFlipTweaks::ApplyFlipModelUpgrade(
    const std::wstring& exe_full_path,
    bool dry_run,
    std::wstring* out_effective_preference) {
    if (exe_full_path.empty()) {
        return false;
    }

    // Only the windowed-game Flip switch: GpuPreference, AutoHDREnable and anything else the user set stay as they are.
    const std::wstring old_prefs = ReadPerExeString(reg_, kUserGpuPrefsPath, exe_full_path);
    std::wstring final_value = old_prefs;
    UpdateOrInsertParam(final_value, L"SwapEffectUpgradeEnable", L"1");
    const std::wstring old_layers = ReadPerExeString(reg_, kAppCompatLayersPath, exe_full_path);
    const std::wstring final_layers = WithHighDpiAware(old_layers);

    if (out_effective_preference) {
        *out_effective_preference = final_value;
    }

    // Already applied: no write and no second journal entry, which would record VegaFlow's own value as the original.
    if (dry_run || (final_value == old_prefs && final_layers == old_layers)) {
        return true;
    }

    const uint64_t j_id = reg_.BeginDxgiPreferenceChange(exe_full_path, final_value, "DXGI Flip Model upgrade");
    bool ok = final_value == old_prefs ||
              reg_.WriteValue(RegHive::CurrentUser, kUserGpuPrefsPath, exe_full_path, RegValue::MakeString(final_value), false);
    if (ok && final_layers != old_layers) {
        ok = reg_.WriteValue(RegHive::CurrentUser, kAppCompatLayersPath, exe_full_path, RegValue::MakeString(final_layers), false);
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

bool DxgiFlipTweaks::HasFlipModelUpgrade(const std::wstring& exe_full_path) {
    std::wstring val;
    if (!QueryUserGpuPreferences(exe_full_path, val)) {
        return false;
    }
    const std::wstring lower = ToLower(val);
    return (lower.find(L"swapeffectupgradeenable=1") != std::wstring::npos);
}

} // namespace vegaflow
