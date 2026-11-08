#pragma once
// config.json loading/saving and the merge of the Steam library into the custom game list.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "optimize/memory_manager.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"

namespace vegaflow::app {

struct AppConfig {
    uint32_t poll_interval_ms = 1500;
    ULONG timer_resolution_ns100 = 5000;
    float fsr_sharpness = 0.20f;
    int fsr_mode = 0; // 0 = FSR 1.0 Ultra-Clean, 1 = NIS / Bicubic 16-Tap, 2 = Anime4K (JRPG/Cel)
    bool timer_forced_manual = false;
    bool restrict_game_cpu_sets = false; // false: the game uses all threads and only the background is confined
    vegaflow::MemoryPurgeConfig memory{};
    std::vector<std::wstring> custom_game_exes;
    std::vector<std::wstring> user_custom_game_exes; // Original exes from config.json
};


AppConfig LoadConfig(const std::string& path);
bool SaveConfig(const std::string& path, const AppConfig& cfg);
void SyncSteamGamesToCustomExes(const std::vector<vegaflow::SteamGameEntry>& steam_games, AppConfig& cfg);

} // namespace vegaflow::app
