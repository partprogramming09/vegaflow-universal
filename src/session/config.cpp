#include "session/config.hpp"

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "optimize/steam/steam_engine_optimizer.hpp"
#include <filesystem>
#include "session/text_convert.hpp"
#include "bridge/rust_bridge.h"

namespace vegaflow::app {

namespace {

// Larger than any real config.json; anything bigger is not read and not overwritten.
constexpr size_t kMaxConfigBytes = 1u << 20;
constexpr uint64_t kBytesPerMb = 1024ULL * 1024ULL;

void CollectUtf8(void* ctx, const char* utf8, size_t len) {
    static_cast<std::vector<std::string>*>(ctx)->emplace_back(utf8, len);
}

VfConfigSettings ToBridge(const AppConfig& cfg) {
    VfConfigSettings s{};
    s.poll_interval_ms = cfg.poll_interval_ms;
    s.timer_resolution_ns100 = cfg.timer_resolution_ns100;
    s.cooldown_seconds = cfg.memory.cooldown_seconds;
    s.timer_forced_manual = cfg.timer_forced_manual;
    s.restrict_game_cpu_sets = cfg.restrict_game_cpu_sets;
    s.min_free_ram_mb = cfg.memory.min_free_ram_mb;
    s.min_standby_mb = cfg.memory.min_standby_mb;
    s.max_game_disk_read_mb_s = cfg.memory.max_game_disk_read_bytes_per_sec / kBytesPerMb;
    return s;
}

AppConfig FromBridge(const VfConfigSettings& s, const std::vector<std::string>& game_exes) {
    AppConfig cfg{};
    cfg.poll_interval_ms = s.poll_interval_ms;
    cfg.timer_resolution_ns100 = s.timer_resolution_ns100;
    cfg.timer_forced_manual = s.timer_forced_manual;
    cfg.restrict_game_cpu_sets = s.restrict_game_cpu_sets;
    cfg.memory.min_free_ram_mb = s.min_free_ram_mb;
    cfg.memory.min_standby_mb = s.min_standby_mb;
    cfg.memory.cooldown_seconds = s.cooldown_seconds;
    cfg.memory.max_game_disk_read_bytes_per_sec = s.max_game_disk_read_mb_s * kBytesPerMb;
    for (const auto& exe : game_exes) cfg.user_custom_game_exes.push_back(Utf8ToWide(exe));
    cfg.custom_game_exes = cfg.user_custom_game_exes;
    return cfg;
}

// The known settings and game names of a file as one comparable string ("" for an unreadable or corrupt text).
std::string KnownSettings(const std::string& text) {
    VfConfigLoad out{};
    std::vector<std::string> names;
    if (!vf_rust_config_load(text.data(), text.size(), &out, CollectUtf8, &names) || (out.status & VF_CONFIG_CORRUPT)) {
        return "";
    }
    const VfConfigSettings& s = out.settings;
    std::string key = std::to_string(s.poll_interval_ms) + "," + std::to_string(s.timer_resolution_ns100) + "," +
                      std::to_string(s.cooldown_seconds) + "," + std::to_string(s.timer_forced_manual) + "," +
                      std::to_string(s.restrict_game_cpu_sets) + "," + std::to_string(s.min_free_ram_mb) + "," +
                      std::to_string(s.min_standby_mb) + "," + std::to_string(s.max_game_disk_read_mb_s);
    for (const auto& n : names) key += std::string(1, '\0') + n;
    return key;
}

// The factory values, read the same way as a file (empty text), so they come from the Rust rules.
AppConfig FactoryConfig() {
    VfConfigLoad out{};
    vf_rust_config_load(nullptr, 0, &out, nullptr, nullptr);
    return FromBridge(out.settings, {});
}

std::string RejectedKeyNames(uint32_t bits) {
    static const std::pair<uint32_t, const char*> kNames[] = {
        {VF_CONFIG_KEY_VERSION, "version"},
        {VF_CONFIG_KEY_POLL_INTERVAL_MS, "poll_interval_ms"},
        {VF_CONFIG_KEY_TIMER_RESOLUTION_NS100, "timer_resolution_ns100"},
        {VF_CONFIG_KEY_TIMER_FORCED_MANUAL, "timer_forced_manual"},
        {VF_CONFIG_KEY_RESTRICT_GAME_CPU_SETS, "restrict_game_cpu_sets"},
        {VF_CONFIG_KEY_MIN_FREE_RAM_MB, "memory.min_free_ram_mb"},
        {VF_CONFIG_KEY_MIN_STANDBY_MB, "memory.min_standby_mb"},
        {VF_CONFIG_KEY_COOLDOWN_SECONDS, "memory.cooldown_seconds"},
        {VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S, "memory.max_game_disk_read_mb_s"},
        {VF_CONFIG_KEY_CUSTOM_GAME_EXES, "custom_game_exes"},
        {VF_CONFIG_KEY_MEMORY, "memory"},
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
    std::vector<std::string> game_exes;
    if (!vf_rust_config_load(text.data(), text.size(), &out, CollectUtf8, &game_exes)) {
        report_.status = VF_CONFIG_CORRUPT;
        return FactoryConfig();
    }
    report_.status = out.status;
    report_.rejected_keys = out.rejected_keys;
    last_known_ = KnownSettings(text);
    return FromBridge(out.settings, game_exes);
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
    const VfConfigSettings settings = ToBridge(cfg);
    std::vector<std::string> text;
    const uint32_t result = vf_rust_config_save(current.data(), current.size(), &settings, names.data(), names.size(),
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
}

} // namespace vegaflow::app
