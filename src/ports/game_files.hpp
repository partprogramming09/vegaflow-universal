#pragma once
// Port for what the Juegos buttons change in a game's folder: the engine preset file and, for an old install, DXVK.
// session/game_actions only asks; optimize/game_files_adapter.hpp is the real one (SteamEngineOptimizer + DxvkManager).
// Every call that writes keeps its dry_run flag.

#include "model/hardware.hpp"
#include "model/steam.hpp"

#include <cstdint>
#include <string>

namespace vegaflow {

class GameFiles {
public:
    virtual ~GameFiles() = default;

    virtual EngineOptimizationReport ApplyEngineUmaPreset(const SteamGameEntry& game, const ApuOptimizationPolicy& policy,
                                                          uint32_t vram_pool_mb, bool dry_run) = 0;
    virtual EngineOptimizationReport RestoreEngineOriginalConfig(const SteamGameEntry& game, bool dry_run) = 0;
    virtual bool RemoveDxvk(const std::wstring& game_exe_path, bool dry_run, std::string* out_message) = 0;
};

} // namespace vegaflow
