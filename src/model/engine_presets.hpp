#pragma once
// What the engine presets write into a game's config: pure text and key lists, so screens can show them without touching a file.
// (optimize/SteamEngineOptimizer applies them; its static members of the same names forward here.)

#include "model/hardware.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

// The boot.config keys a Unity profile writes (the one list: the writer and the Juegos screen both read it).
const std::vector<std::pair<std::string, std::string>>& UnityBootConfigKeys();

// The UMA configuration block for Engine.ini according to the hardware policy and VRAM pool.
std::string GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override = 0);

} // namespace vegaflow
