#include "session/config.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "optimize/steam/steam_engine_optimizer.hpp"
#include <filesystem>
#include "windows/text_convert.hpp"
#include "bridge/rust_bridge.h"

namespace vegaflow::app {

namespace {

// Larger than any real config.json; anything bigger is not read and not overwritten.
constexpr size_t kMaxConfigBytes = 1u << 20;
constexpr uint64_t kBytesPerMb = 1024ULL * 1024ULL;

void CollectUtf8(void* ctx, const char* utf8, size_t len) {
    static_cast<std::vector<std::string>*>(ctx)->emplace_back(utf8, len);
}

struct LoadExCollector {
    std::vector<std::string> game_exes;
    std::vector<std::string> excluded_game_exes;
    std::vector<std::pair<std::string, uint8_t>> exceptions;
};

void CollectGameExe(void* ctx, const char* utf8, size_t len) {
    static_cast<LoadExCollector*>(ctx)->game_exes.emplace_back(utf8, len);
}

void CollectExcludedExe(void* ctx, const char* utf8, size_t len) {
    static_cast<LoadExCollector*>(ctx)->excluded_game_exes.emplace_back(utf8, len);
}

void CollectException(void* ctx, const uint8_t* exe, size_t len, uint8_t action) {
    static_cast<LoadExCollector*>(ctx)->exceptions.emplace_back(
        std::string(reinterpret_cast<const char*>(exe), len), action);
}

VfConfigSettings ToBridge(const AppConfig& cfg) {
    VfConfigSettings s{};
    s.poll_interval_ms = cfg.poll_interval_ms;
    s.cooldown_seconds = cfg.memory.cooldown_seconds;
    s.restrict_game_cpu_sets = cfg.restrict_game_cpu_sets;
    s.min_free_ram_mb = cfg.memory.min_free_ram_mb;
    s.min_standby_mb = cfg.memory.min_standby_mb;
    s.max_game_disk_read_mb_s = cfg.memory.max_game_disk_read_bytes_per_sec / kBytesPerMb;
    return s;
}

VfProcessPolicyConfig PolicyToBridge(const vegaflow::ProcessPolicyConfig& policy) {
    VfProcessPolicyConfig p{};
    p.category_actions.recording_streaming = static_cast<uint8_t>(policy.category_actions.recording_streaming);
    p.category_actions.calls_voice = static_cast<uint8_t>(policy.category_actions.calls_voice);
    p.category_actions.messaging = static_cast<uint8_t>(policy.category_actions.messaging);
    p.category_actions.browsers = static_cast<uint8_t>(policy.category_actions.browsers);
    p.category_actions.stores_launchers = static_cast<uint8_t>(policy.category_actions.stores_launchers);
    p.category_actions.music = static_cast<uint8_t>(policy.category_actions.music);
    p.category_actions.cloud_sync = static_cast<uint8_t>(policy.category_actions.cloud_sync);
    p.heavy_work_as_game = policy.heavy_work_as_game;
    return p;
}

AppConfig FromBridge(const VfConfigSettings& s, const VfProcessPolicyConfig& p, const LoadExCollector& col) {
    AppConfig cfg{};
    cfg.poll_interval_ms = s.poll_interval_ms;
    cfg.restrict_game_cpu_sets = s.restrict_game_cpu_sets;
    cfg.memory.min_free_ram_mb = s.min_free_ram_mb;
    cfg.memory.min_standby_mb = s.min_standby_mb;
    cfg.memory.cooldown_seconds = s.cooldown_seconds;
    cfg.memory.max_game_disk_read_bytes_per_sec = s.max_game_disk_read_mb_s * kBytesPerMb;

    cfg.process_policy.heavy_work_as_game = p.heavy_work_as_game;
    cfg.process_policy.category_actions.recording_streaming = static_cast<vegaflow::ProcessAction>(p.category_actions.recording_streaming);
    cfg.process_policy.category_actions.calls_voice = static_cast<vegaflow::ProcessAction>(p.category_actions.calls_voice);
    cfg.process_policy.category_actions.messaging = static_cast<vegaflow::ProcessAction>(p.category_actions.messaging);
    cfg.process_policy.category_actions.browsers = static_cast<vegaflow::ProcessAction>(p.category_actions.browsers);
    cfg.process_policy.category_actions.stores_launchers = static_cast<vegaflow::ProcessAction>(p.category_actions.stores_launchers);
    cfg.process_policy.category_actions.music = static_cast<vegaflow::ProcessAction>(p.category_actions.music);
    cfg.process_policy.category_actions.cloud_sync = static_cast<vegaflow::ProcessAction>(p.category_actions.cloud_sync);

    for (const auto& exe : col.game_exes) {
        cfg.user_custom_game_exes.push_back(Utf8ToWide(exe));
    }
    cfg.custom_game_exes = cfg.user_custom_game_exes;
    cfg.process_policy.custom_game_exes = cfg.user_custom_game_exes;

    for (const auto& exe : col.excluded_game_exes) {
        cfg.process_policy.excluded_game_exes.push_back(Utf8ToWide(exe));
    }
    for (const auto& [exe, act] : col.exceptions) {
        cfg.process_policy.exceptions.push_back({Utf8ToWide(exe), static_cast<vegaflow::ProcessAction>(act)});
    }
    return cfg;
}

// The known settings and game names of a file as one comparable string ("" for an unreadable or corrupt text).
std::string KnownSettings(const std::string& text) {
    VfConfigLoad out{};
    VfProcessPolicyConfig policy{};
    LoadExCollector col{};
    if (!vf_rust_config_load_ex(text.data(), text.size(), &out, &policy, CollectGameExe, CollectExcludedExe,
                                CollectException, &col) ||
        (out.status & VF_CONFIG_CORRUPT)) {
        return "";
    }
    const VfConfigSettings& s = out.settings;
    std::string key = std::to_string(s.poll_interval_ms) + "," + std::to_string(s.cooldown_seconds) + "," +
                      std::to_string(s.restrict_game_cpu_sets) + "," + std::to_string(s.min_free_ram_mb) + "," +
                      std::to_string(s.min_standby_mb) + "," + std::to_string(s.max_game_disk_read_mb_s) + "," +
                      std::to_string(policy.heavy_work_as_game ? 1 : 0) + "," +
                      std::to_string(policy.category_actions.recording_streaming) + "," +
                      std::to_string(policy.category_actions.calls_voice) + "," +
                      std::to_string(policy.category_actions.messaging) + "," +
                      std::to_string(policy.category_actions.browsers) + "," +
                      std::to_string(policy.category_actions.stores_launchers) + "," +
                      std::to_string(policy.category_actions.music) + "," +
                      std::to_string(policy.category_actions.cloud_sync);
    for (const auto& n : col.game_exes) key += std::string(1, '\0') + "g:" + n;
    for (const auto& n : col.excluded_game_exes) key += std::string(1, '\0') + "x:" + n;
    for (const auto& [exe, act] : col.exceptions) {
        key += std::string(1, '\0') + "e:" + exe + "=" + std::to_string(act);
    }
    return key;
}

// The factory values, read the same way as a file (empty text), so they come from the Rust rules.
AppConfig FactoryConfig() {
    VfConfigLoad out{};
    VfProcessPolicyConfig policy{};
    LoadExCollector col{};
    vf_rust_config_load_ex(nullptr, 0, &out, &policy, nullptr, nullptr, nullptr, nullptr);
    return FromBridge(out.settings, policy, col);
}

std::string RejectedKeyNames(uint32_t bits) {
    static const std::pair<uint32_t, const char*> kNames[] = {
        {VF_CONFIG_KEY_VERSION, "version"},
        {VF_CONFIG_KEY_POLL_INTERVAL_MS, "poll_interval_ms"},
        {VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS, "restrict_game_cpu_sets"},
        {VF_CONFIG_KEY_MIN_FREE_RAM_MB, "memory.min_free_ram_mb"},
        {VF_CONFIG_KEY_MIN_STANDBY_MB, "memory.min_standby_mb"},
        {VF_CONFIG_KEY_COOLDOWN_SECONDS, "memory.cooldown_seconds"},
        {VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, "memory.max_game_disk_read_mb_s"},
        {VF_CONFIG_KEY_CUSTOM_GAME_EXES, "custom_game_exes"},
        {VF_CONFIG_KEY_MEMORY, "memory"},
        {VF_CONFIG_KEY_HEAVY_WORK_AS_GAME, "heavy_work_as_game"},
        {VF_CONFIG_KEY_PROCESS_CATEGORY_ACTIONS, "process_category_actions"},
        {VF_CONFIG_KEY_PROCESS_EXCEPTIONS, "process_exceptions"},
        {VF_CONFIG_KEY_EXCLUDED_GAME_EXES, "excluded_game_exes"},
    };
    std::string names;
    for (const auto& [bit, name] : kNames) {
        if (bits & bit) names += (names.empty() ? "" : ", ") + std::string(name);
    }
    return names;
}

} // namespace

bool ConfigLoadReport::CanSave() const {
    return folder_trusted && !unreadable && (status & VF_CONFIG_BLOCKS_SAVE) == 0;
}

std::string ConfigLoadReport::Warning() const {
    const std::string not_saved = " VegaFlow no lo sobrescribe hasta que lo corrijas y reinicies VegaFlow.";
    if (!folder_trusted) {
        return "config.json está en una carpeta donde pueden escribir cuentas sin permisos de administrador: se usan "
               "los valores de fábrica y no se guarda nada.";
    }
    if (unreadable) return "No se pudo leer config.json: se usan los valores de fábrica." + not_saved;
    if (status & VF_CONFIG_CORRUPT) return "config.json no es JSON válido: se usan los valores de fábrica." + not_saved;
    std::string warning;
    if (status & VF_CONFIG_KEY_REJECTED) {
        warning = "config.json tiene valores no permitidos (" + RejectedKeyNames(rejected_keys) +
                  "): esas claves usan su valor de fábrica." + not_saved;
    }
    if (status & VF_CONFIG_NEWER_VERSION) {
        warning += std::string(warning.empty() ? "" : " ") +
                   "config.json es de una versión más nueva de VegaFlow: se lee lo conocido y no se sobrescribe.";
    }
    return warning;
}

ConfigStore::ConfigStore(vegaflow::FileSystem& fs, std::wstring path, bool folder_trusted)
    : fs_(fs), path_(std::move(path)) {
    report_.folder_trusted = folder_trusted;
}

AppConfig ConfigStore::Load() {
    const bool trusted = report_.folder_trusted;
    report_ = ConfigLoadReport{};
    report_.folder_trusted = trusted;
    if (!trusted) return FactoryConfig();

    std::string text;
    if (!fs_.Read(path_, text)) {
        if (fs_.Exists(path_)) report_.unreadable = true;
        text.clear(); // a missing file reads as empty: factory values, and saving may create it
    }
    if (report_.unreadable || text.size() > kMaxConfigBytes) {
        report_.unreadable = true;
        return FactoryConfig();
    }
    VfConfigLoad out{};
    VfProcessPolicyConfig policy{};
    LoadExCollector col{};
    if (!vf_rust_config_load_ex(text.data(), text.size(), &out, &policy, CollectGameExe, CollectExcludedExe,
                                CollectException, &col)) {
        report_.status = VF_CONFIG_CORRUPT;
        return FactoryConfig();
    }
    report_.status = out.status;
    report_.rejected_keys = out.rejected_keys;
    last_known_ = KnownSettings(text);
    return FromBridge(out.settings, policy, col);
}

bool ConfigStore::Save(const AppConfig& cfg, bool dry_run, std::string* why) {
    auto refuse = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (!report_.CanSave()) return refuse(report_.Warning());
    // The current file is read again: unknown keys added by hand are kept, and a change by hand to a known setting or a
    // break since Load stops the save instead of being overwritten with the values in memory.
    std::string current;
    if (!fs_.Read(path_, current)) {
        if (fs_.Exists(path_)) return refuse("no se pudo leer config.json para guardarlo sin perder nada");
        current.clear();
    }
    if (current.size() > kMaxConfigBytes) return refuse("config.json es demasiado grande");
    if (KnownSettings(current) != last_known_) {
        return refuse("config.json cambió fuera de VegaFlow después de abrirlo; reinicia VegaFlow para leer esos cambios");
    }

    std::string names;
    for (const auto& exe : cfg.user_custom_game_exes) {
        names += WideToUtf8(exe);
        names.push_back('\0');
    }
    std::string excluded_names;
    for (const auto& exe : cfg.process_policy.excluded_game_exes) {
        excluded_names += WideToUtf8(exe);
        excluded_names.push_back('\0');
    }
    std::vector<std::string> ex_strings;
    ex_strings.reserve(cfg.process_policy.exceptions.size());
    std::vector<VfProcessExceptionEntry> ex_entries;
    ex_entries.reserve(cfg.process_policy.exceptions.size());
    for (const auto& ex : cfg.process_policy.exceptions) {
        ex_strings.push_back(WideToUtf8(ex.exe_name));
    }
    for (size_t i = 0; i < cfg.process_policy.exceptions.size(); ++i) {
        VfProcessExceptionEntry e{};
        e.exe_ptr = ex_strings[i].empty() ? nullptr : reinterpret_cast<const uint8_t*>(ex_strings[i].data());
        e.exe_len = ex_strings[i].size();
        e.action = static_cast<uint8_t>(cfg.process_policy.exceptions[i].action);
        ex_entries.push_back(e);
    }

    const VfConfigSettings settings = ToBridge(cfg);
    const VfProcessPolicyConfig policy = PolicyToBridge(cfg.process_policy);
    std::vector<std::string> text;
    const uint32_t result = vf_rust_config_save_ex(
        current.data(), current.size(), &settings, &policy,
        names.empty() ? nullptr : reinterpret_cast<const uint8_t*>(names.data()), names.size(),
        excluded_names.empty() ? nullptr : reinterpret_cast<const uint8_t*>(excluded_names.data()), excluded_names.size(),
        ex_entries.empty() ? nullptr : ex_entries.data(), ex_entries.size(),
        CollectUtf8, &text);
    if (result == VF_CONFIG_SAVE_BLOCKED) {
        return refuse("config.json cambió y ya no se puede sobrescribir sin perder lo que tiene; corrígelo a mano");
    }
    if (result != VF_CONFIG_SAVE_OK || text.size() != 1) return refuse("un valor no cumple las reglas de config.json");
    if (dry_run) return true;
    if (!fs_.Replace(path_, text[0])) return refuse("no se pudo escribir config.json");
    last_known_ = KnownSettings(text[0]);
    return true;
}

void SyncSteamGamesToCustomExes(const std::vector<vegaflow::SteamGameEntry>& steam_games, AppConfig& cfg) {
    cfg.custom_game_exes = cfg.user_custom_game_exes;
    for (const auto& sg : steam_games) {
        if (!sg.primary_exe_path.empty()) {
            std::wstring exe_name = std::filesystem::path(sg.primary_exe_path).filename().wstring();
            const std::string utf8_name = WideToUtf8(exe_name);
            // A name planted in a game folder must not turn a Windows process into "the game".
            if (vf_rust_is_allowed_game_exe(utf8_name.data(), utf8_name.size())) {
                bool exists = false;
                for (const auto& existing : cfg.custom_game_exes) {
                    if (_wcsicmp(existing.c_str(), exe_name.c_str()) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    cfg.custom_game_exes.push_back(exe_name);
                }
            }
        }
    }
    cfg.process_policy.custom_game_exes = cfg.custom_game_exes;
}

} // namespace vegaflow::app
