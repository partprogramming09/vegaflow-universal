#include "session/config.hpp"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "optimize/steam/steam_engine_optimizer.hpp"
#include <filesystem>
#include "session/text_convert.hpp"

namespace vegaflow::app {

// Robust, lightweight JSON-like parser for config.json (Zero external dependencies)
AppConfig LoadConfig(const std::string& path) {
    AppConfig cfg{};
    std::ifstream file(path);
    if (!file.is_open()) {
        return cfg;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto find_uint = [&](const std::string& key, uint64_t default_val) -> uint64_t {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return default_val;
        size_t start = content.find_first_of("0123456789", pos);
        if (start == std::string::npos) return default_val;
        size_t end = content.find_first_not_of("0123456789", start);
        std::string num_str = content.substr(start, end - start);
        try {
            return std::stoull(num_str);
        } catch (...) {
            return default_val;
        }
    };

    auto find_bool = [&](const std::string& key, bool default_val) -> bool {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return default_val;
        size_t t_pos = content.find("true", pos);
        size_t f_pos = content.find("false", pos);
        size_t next_comma = content.find_first_of(",}\n\r", pos);
        if (t_pos != std::string::npos && (next_comma == std::string::npos || t_pos < next_comma)) return true;
        if (f_pos != std::string::npos && (next_comma == std::string::npos || f_pos < next_comma)) return false;
        return default_val;
    };

    cfg.poll_interval_ms = static_cast<uint32_t>(find_uint("poll_interval_ms", 1500));
    cfg.timer_resolution_ns100 = static_cast<ULONG>(find_uint("timer_resolution_ns100", 5000));
    cfg.timer_forced_manual = find_bool("timer_forced_manual", false);
    cfg.restrict_game_cpu_sets = find_bool("restrict_game_cpu_sets", false);
    cfg.memory.min_free_ram_mb = find_uint("min_free_ram_mb", 1200);
    cfg.memory.min_standby_mb = find_uint("min_standby_mb", 1500);
    cfg.memory.cooldown_seconds = static_cast<uint32_t>(find_uint("cooldown_seconds", 90));
    uint64_t max_disk_mb_s = find_uint("max_game_disk_read_mb_s", 15);
    cfg.memory.max_game_disk_read_bytes_per_sec = max_disk_mb_s * 1024ULL * 1024ULL;

    size_t custom_pos = content.find("\"custom_game_exes\"");
    if (custom_pos != std::string::npos) {
        size_t arr_start = content.find('[', custom_pos);
        size_t arr_end = content.find(']', arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            std::string sub = content.substr(arr_start + 1, arr_end - arr_start - 1);
            size_t curr = 0;
            while ((curr = sub.find('\"', curr)) != std::string::npos) {
                size_t next_q = sub.find('\"', curr + 1);
                if (next_q == std::string::npos) break;
                std::string exe = sub.substr(curr + 1, next_q - curr - 1);
                if (!exe.empty()) {
                    std::wstring wexe = Utf8ToWide(exe);
                    cfg.custom_game_exes.push_back(wexe);
                    cfg.user_custom_game_exes.push_back(wexe);
                }
                curr = next_q + 1;
            }
        }
    }

    return cfg;
}

bool SaveConfig(const std::string& path, const AppConfig& cfg) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"poll_interval_ms\": " << cfg.poll_interval_ms << ",\n";
    file << "  \"timer_resolution_ns100\": " << cfg.timer_resolution_ns100 << ",\n";
    file << "  \"timer_forced_manual\": " << (cfg.timer_forced_manual ? "true" : "false") << ",\n";
    file << "  \"restrict_game_cpu_sets\": " << (cfg.restrict_game_cpu_sets ? "true" : "false") << ",\n";
    file << "  \"memory\": {\n";
    file << "    \"min_free_ram_mb\": " << cfg.memory.min_free_ram_mb << ",\n";
    file << "    \"min_standby_mb\": " << cfg.memory.min_standby_mb << ",\n";
    file << "    \"cooldown_seconds\": " << cfg.memory.cooldown_seconds << ",\n";
    uint64_t max_disk_mb_s = cfg.memory.max_game_disk_read_bytes_per_sec / (1024ULL * 1024ULL);
    file << "    \"max_game_disk_read_mb_s\": " << (max_disk_mb_s == 0 ? 15 : max_disk_mb_s) << "\n";
    file << "  },\n";
    file << "  \"custom_game_exes\": [";
    for (size_t i = 0; i < cfg.user_custom_game_exes.size(); ++i) {
        file << "\"" << WideToUtf8(cfg.user_custom_game_exes[i]) << "\"";
        if (i + 1 < cfg.user_custom_game_exes.size()) {
            file << ", ";
        }
    }
    file << "]\n";
    file << "}\n";
    return true;
}

void SyncSteamGamesToCustomExes(const std::vector<vegaflow::SteamGameEntry>& steam_games, AppConfig& cfg) {
    cfg.custom_game_exes = cfg.user_custom_game_exes;
    for (const auto& sg : steam_games) {
        if (!sg.primary_exe_path.empty()) {
            std::wstring exe_name = std::filesystem::path(sg.primary_exe_path).filename().wstring();
            if (!exe_name.empty()) {
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
