#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "bridge/rust_bridge.h"

namespace vegaflow {

namespace {

constexpr const char* kSystemSettings = "SystemSettings";
constexpr const char* kEmptyBackupSentinel = "; VegaFlow Empty Backup\n";

bool EqualsInsensitive(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    return std::equal(
        a.begin(), a.end(),
        b.begin(), b.end(),
        [](char ca, char cb) {
            return std::tolower(static_cast<unsigned char>(ca)) ==
                   std::tolower(static_cast<unsigned char>(cb));
        }
    );
}

std::string FormatFloatTrimmed(float val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", val);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s.empty() ? "0" : s;
}

void CollectBytesCallback(void* ctx, const uint8_t* data, size_t len) {
    auto* out = static_cast<std::string*>(ctx);
    out->assign(reinterpret_cast<const char*>(data), len);
}

struct QueriedKv {
    std::vector<std::pair<std::string, std::string>> pairs;
};

void CollectKvCallback(void* ctx, const char* key, size_t key_len, const char* val, size_t val_len) {
    auto* out = static_cast<QueriedKv*>(ctx);
    out->pairs.emplace_back(std::string(key, key_len), std::string(val, val_len));
}

} // namespace

bool SteamEngineOptimizer::ApplyApuShadowConfig(
    const std::filesystem::path& iniPath,
    const ApuShadowConfig& config,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    if (!fs_.CanModifyGameDir(targetDir)) {
        return false;
    }
    if (fs_.HasAntiCheatSignature(iniPath.wstring())) {
        return false;
    }
    if (!fs_.CanWrite(iniPath.wstring())) {
        return false;
    }

    if (config.max_cascades > 16 || config.distance_scale < 0.0f ||
        config.radius_threshold < 0.0f || config.volumetric_fog > 1) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    const std::wstring iniW = iniPath.wstring();
    const std::wstring bakW = iniW + L".vegaflow.bak";
    if (!fs_.Exists(bakW)) {
        if (fs_.Exists(iniW)) {
            if (!fs_.Copy(iniW, bakW, true, false)) {
                return false;
            }
        } else {
            if (!fs_.Write(bakW, kEmptyBackupSentinel, false)) {
                return false;
            }
        }
    }

    std::string existingContent;
    if (fs_.Exists(iniW)) {
        if (!fs_.Read(iniW, existingContent)) {
            return false;
        }
    }

    const std::pair<std::string, std::string> entries[] = {
        {"r.Shadow.CSM.MaxCascades", std::to_string(config.max_cascades)},
        {"r.Shadow.DistanceScale", FormatFloatTrimmed(config.distance_scale)},
        {"r.Shadow.RadiusThreshold", FormatFloatTrimmed(config.radius_threshold)},
        {"r.AOHalfRes", config.half_res_ao ? "1" : "0"},
        {"r.VolumetricFog", std::to_string(config.volumetric_fog)},
    };
    std::vector<VfIniKv> kv;
    kv.reserve(5);
    for (const auto& [k, v] : entries) {
        kv.push_back(VfIniKv{k.data(), k.size(), v.data(), v.size()});
    }

    std::string outBytes;
    const uint32_t rc = vf_rust_ini_apply_section(
        reinterpret_cast<const uint8_t*>(existingContent.data()),
        existingContent.size(),
        kSystemSettings,
        std::char_traits<char>::length(kSystemSettings),
        kv.data(),
        kv.size(),
        CollectBytesCallback,
        &outBytes);
    if (rc != VF_INI_OK) {
        return false;
    }

    return fs_.Write(iniW, outBytes, false);
}

bool SteamEngineOptimizer::RestoreApuShadowConfig(
    const std::filesystem::path& iniPath,
    bool dryRun) {

    if (iniPath.empty()) {
        return false;
    }

    std::filesystem::path targetDir = iniPath.parent_path();
    if (targetDir.empty()) {
        std::error_code ec;
        targetDir = std::filesystem::current_path(ec);
    }

    if (!fs_.CanModifyGameDir(targetDir)) {
        return false;
    }
    if (fs_.HasAntiCheatSignature(iniPath.wstring())) {
        return false;
    }
    if (!fs_.CanWrite(iniPath.wstring())) {
        return false;
    }

    const std::wstring iniW = iniPath.wstring();
    const std::wstring vbakW = iniW + L".vegaflow.bak";
    const std::wstring legacyBakW = iniW + L".bak";
    const bool hasVbak = fs_.Exists(vbakW);
    const bool hasLegacyBak = !hasVbak && fs_.Exists(legacyBakW);
    const std::wstring activeBakW = hasVbak ? vbakW : legacyBakW;
    const bool bakExists = hasVbak || hasLegacyBak;
    const bool iniExists = fs_.Exists(iniW);

    if (!bakExists && !iniExists) {
        return false;
    }

    if (dryRun) {
        return true;
    }

    if (bakExists) {
        std::string bakContent;
        if (!fs_.Read(activeBakW, bakContent)) {
            return false;
        }
        if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
            if (iniExists && !fs_.Remove(iniW, false)) {
                return false;
            }
            if (fs_.Exists(iniW)) {
                return false;
            }
            return fs_.Remove(activeBakW, false);
        }

        if (!fs_.Copy(activeBakW, iniW, true, false)) {
            return false;
        }
        std::string restoredContent;
        if (!fs_.Read(iniW, restoredContent) || restoredContent != bakContent) {
            return false;
        }
        return fs_.Remove(activeBakW, false);
    }

    std::string content;
    if (!fs_.Read(iniW, content)) {
        return false;
    }

    const char* targetKeys[] = {
        "r.Shadow.CSM.MaxCascades",
        "r.Shadow.DistanceScale",
        "r.Shadow.RadiusThreshold",
        "r.AOHalfRes",
        "r.VolumetricFog",
    };
    std::vector<VfIniKv> kv;
    for (const char* k : targetKeys) {
        kv.push_back(VfIniKv{k, std::char_traits<char>::length(k), "", 0});
    }

    bool changed = false;
    std::string outBytes;
    const uint32_t rc = vf_rust_ini_remove_section_keys(
        reinterpret_cast<const uint8_t*>(content.data()),
        content.size(),
        kSystemSettings,
        std::char_traits<char>::length(kSystemSettings),
        kv.data(),
        kv.size(),
        &changed,
        CollectBytesCallback,
        &outBytes);
    if (rc != VF_INI_OK) {
        return false;
    }
    if (!changed) {
        return true;
    }
    return fs_.Write(iniW, outBytes, false);
}

bool SteamEngineOptimizer::QueryApuShadowConfig(
    const std::filesystem::path& iniPath,
    ApuShadowConfig& currentConfig) {

    if (iniPath.empty()) {
        return false;
    }

    const std::wstring iniW = iniPath.wstring();
    if (!fs_.Exists(iniW)) {
        return false;
    }

    std::string content;
    if (!fs_.Read(iniW, content)) {
        return false;
    }

    QueriedKv queried;
    const uint32_t rc = vf_rust_ini_query_section(
        reinterpret_cast<const uint8_t*>(content.data()),
        content.size(),
        kSystemSettings,
        std::char_traits<char>::length(kSystemSettings),
        CollectKvCallback,
        &queried);
    if (rc != VF_INI_OK) {
        return false;
    }

    uint32_t keysFound = 0;
    for (const auto& [keyPart, valStr] : queried.pairs) {
        if (EqualsInsensitive(keyPart, "r.Shadow.CSM.MaxCascades")) {
            try {
                currentConfig.max_cascades = static_cast<uint32_t>(std::stoul(valStr));
                keysFound++;
            } catch (...) {}
        } else if (EqualsInsensitive(keyPart, "r.Shadow.DistanceScale")) {
            try {
                currentConfig.distance_scale = std::stof(valStr);
                keysFound++;
            } catch (...) {}
        } else if (EqualsInsensitive(keyPart, "r.Shadow.RadiusThreshold")) {
            try {
                currentConfig.radius_threshold = std::stof(valStr);
                keysFound++;
            } catch (...) {}
        } else if (EqualsInsensitive(keyPart, "r.AOHalfRes")) {
            currentConfig.half_res_ao = (valStr == "1" || EqualsInsensitive(valStr, "true"));
            keysFound++;
        } else if (EqualsInsensitive(keyPart, "r.VolumetricFog")) {
            try {
                currentConfig.volumetric_fog = static_cast<uint32_t>(std::stoul(valStr));
                keysFound++;
            } catch (...) {}
        }
    }

    return (keysFound > 0);
}

} // namespace vegaflow
