#pragma once
// The real GameFiles port: forwards to the Steam engine optimizer and the DXVK manager the daemon already owns.

#include "optimize/dxvk_manager.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "ports/game_files.hpp"

namespace vegaflow {

class OptimizeGameFiles final : public GameFiles {
public:
    OptimizeGameFiles(SteamEngineOptimizer& steam, DxvkManager& dxvk) : steam_(steam), dxvk_(dxvk) {}

    EngineOptimizationReport ApplyEngineUmaPreset(const SteamGameEntry& game, const ApuOptimizationPolicy& policy,
                                                  uint32_t vram_pool_mb, bool dry_run) override {
        return steam_.ApplyEngineUmaPreset(game, policy, vram_pool_mb, dry_run);
    }
    EngineOptimizationReport RestoreEngineOriginalConfig(const SteamGameEntry& game, bool dry_run) override {
        return steam_.RestoreEngineOriginalConfig(game, dry_run);
    }
    bool RemoveDxvk(const std::wstring& game_exe_path, bool dry_run, std::string* out_message) override {
        return dxvk_.RemoveDxvk(game_exe_path, dry_run, out_message);
    }

private:
    SteamEngineOptimizer& steam_;
    DxvkManager& dxvk_;
};

} // namespace vegaflow
