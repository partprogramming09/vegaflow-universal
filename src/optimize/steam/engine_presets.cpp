#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"

namespace vegaflow {

using namespace steam_internal;

namespace {

bool WriteStringToFile(FileSystem& fs, const std::filesystem::path& path, const std::string& content, bool is_anticheat_protected = false) {
    return fs.Write(path.wstring(), content, is_anticheat_protected);
}

} // namespace

std::string SteamEngineOptimizer::GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override) {
    uint32_t effective_pool_mb = 0;
    if (policy.tier == ApuTier::Tier1_EntryUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb_override, policy.safe_vram_pool_mb);
    } else if (policy.tier == ApuTier::Tier2_MidVegaUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override >= 3000) ? 4096 : vram_pool_mb_override;
    } else {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb_override;
    }

    std::string umaBlock;
    umaBlock += "\n\n; [VegaFlowUMA_Start]\n";
    umaBlock += "[SystemSettings]\n";
    umaBlock += "r.Streaming.PoolSize=" + std::to_string(effective_pool_mb) + "\n";
    umaBlock += "r.Streaming.LimitPoolSizeToVRAM=0\n";
    umaBlock += "r.Streaming.FullyLoadUsedTextures=1\n";
    umaBlock += "r.Streaming.FramesForFullUpdate=1\n";
    umaBlock += "r.Streaming.MaxNumTexturesToStreamPerFrame=120\n";
    umaBlock += "r.Streaming.AmortizeCPUToGPUCopy=0\n";
    umaBlock += "r.Streaming.Boost=1.5\n";
    umaBlock += "r.ShaderPipelineCache.Enabled=1\n";
    umaBlock += "r.ShaderPipelineCache.AsyncCompileRate=16\n";
    umaBlock += "r.OneFrameThreadLag=1\n";
    umaBlock += "r.GTSyncType=1\n";
    umaBlock += "r.SceneColorFringeQuality=0\n";
    umaBlock += "r.Tonemapper.GrainQuantization=0\n";
    umaBlock += "r.MotionBlurQuality=0\n";
    umaBlock += "r.DepthOfFieldQuality=0\n";
    umaBlock += "r.BloomQuality=1\n";
    umaBlock += "r.Shadow.FilterMethod=0\n";
    umaBlock += "r.Shadow.MaxResolution=" + std::to_string(policy.ue_max_csm_resolution) + "\n";
    umaBlock += "r.Shadow.MaxCSMResolution=" + std::to_string(policy.ue_max_csm_resolution) + "\n";
    umaBlock += "r.Shadow.CSM.MaxCascades=" + std::to_string(policy.ue_max_shadow_cascades) + "\n";
    umaBlock += "r.Shadow.RadiusThreshold=0.05\n";
    umaBlock += "r.Shadow.DistanceScale=0.6\n";
    umaBlock += "r.TranslucencyLightingVolumeDim=24\n";
    umaBlock += "r.VolumetricFog=" + std::string(policy.disable_volumetric_fog ? "0" : "1") + "\n";
    umaBlock += "r.SSR.Quality=0\n";
    umaBlock += "r.AmbientOcclusionLevels=0\n";
    umaBlock += "r.AOQuality=0\n";
    umaBlock += "r.DefaultFeature.AntiAliasing=1\n";
    umaBlock += "r.MaxAnisotropy=" + std::to_string(policy.max_anisotropy) + "\n";
    umaBlock += "r.RDG.AsyncCompute=" + std::string(policy.disable_dx12_async_compute ? "0" : "1") + "\n";
    umaBlock += "D3D12.AsyncCompute=" + std::string(policy.disable_dx12_async_compute ? "0" : "1") + "\n";
    umaBlock += "; [VegaFlowUMA_End]\n";
    return umaBlock;
}

EngineOptimizationReport SteamEngineOptimizer::ApplyEngineUmaPreset(
    const SteamGameEntry& game,
    const ApuOptimizationPolicy& policy,
    uint32_t vram_pool_mb,
    bool dry_run,
    const std::wstring& custom_config_dir_override) {

    EngineOptimizationReport report{};
    std::error_code ec;

    if (!game.primary_exe_path.empty()) {
        GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.primary_exe_path, dry_run);
    }

    if (game.has_anticheat) {
        report.success = true;
        report.blocked_by_anticheat = true;
        report.status_message = "Anti-Cheat detectado: Solo se aplico DXGI Flip Model seguro (archivos del motor intactos)";
        return report;
    }

    if (game.engine == GameEngineType::UnrealEngine4_5) {
        auto ini_paths = ResolveAllUnrealEngineIniPaths(game, custom_config_dir_override);
        if (ini_paths.empty()) {
            ini_paths.push_back(ResolveUnrealEngineIniPath(game, custom_config_dir_override));
        }
        report.config_modified_path = ini_paths.front();

        if (dry_run) {
            report.success = true;
            report.status_message = "Dry-run: Preset UMA Unreal Engine 4/5 validado";
            return report;
        }

        uint32_t effective_pool_mb = 0;
        if (policy.tier == ApuTier::Tier1_EntryUMA) {
            effective_pool_mb = (vram_pool_mb == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb, policy.safe_vram_pool_mb);
        } else if (policy.tier == ApuTier::Tier2_MidVegaUMA) {
            effective_pool_mb = (vram_pool_mb == 0 || vram_pool_mb >= 3000) ? 4096 : vram_pool_mb;
        } else {
            effective_pool_mb = (vram_pool_mb == 0 || vram_pool_mb == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb;
        }
        std::string umaBlock = GenerateUnrealUmaBlock(policy, vram_pool_mb);

        bool any_applied = false;
        for (const auto& engine_ini_path : ini_paths) {
            std::filesystem::path pIni(engine_ini_path);
            std::filesystem::path pBak(engine_ini_path + L".vegaflow.bak");

            if (!std::filesystem::exists(pBak, ec)) {
                if (std::filesystem::exists(pIni, ec)) {
                    report.backup_created = fs_.Copy(
                        pIni.wstring(),
                        pBak.wstring(),
                        true,
                        game.has_anticheat);
                } else {
                    WriteStringToFile(fs_, pBak, "; VegaFlow Empty Backup\n", game.has_anticheat);
                    report.backup_created = true;
                }
            }

            std::string existingContent;
            if (std::filesystem::exists(pIni, ec)) {
                existingContent = ReadFileToString(pIni);
            }

            const std::string tagStart = "; [VegaFlowUMA_Start]";
            const std::string tagEnd = "; [VegaFlowUMA_End]";
            size_t posStart = existingContent.find(tagStart);
            while (posStart != std::string::npos) {
                size_t posEnd = existingContent.find(tagEnd, posStart);
                if (posEnd != std::string::npos) {
                    size_t removeLen = (posEnd + tagEnd.size()) - posStart;
                    if (posStart + removeLen < existingContent.size() && existingContent[posStart + removeLen] == '\n') {
                        removeLen++;
                    }
                    existingContent.erase(posStart, removeLen);
                } else {
                    existingContent.erase(posStart);
                    break;
                }
                posStart = existingContent.find(tagStart);
            }

            // Remove any untagged lingering resolution-scaling lines
            std::stringstream ss(existingContent);
            std::string line;
            std::string cleanedContent;
            while (std::getline(ss, line)) {
                std::string trimmed = line;
                while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == ' ')) trimmed.pop_back();
                if (trimmed.rfind("r.ScreenPercentage=", 0) == 0 ||
                    trimmed.rfind("r.TemporalAA.Upsampling=", 0) == 0 ||
                    trimmed.rfind("r.DefaultFeature.AntiAliasing=", 0) == 0) {
                    continue;
                }
                cleanedContent += line + "\n";
            }
            while (!cleanedContent.empty() &&
                   (cleanedContent.back() == '\r' || cleanedContent.back() == '\n' || cleanedContent.back() == ' ')) {
                cleanedContent.pop_back();
            }
            existingContent = std::move(cleanedContent);

            std::string finalContent = existingContent + umaBlock;
            if (WriteStringToFile(fs_, pIni, finalContent, game.has_anticheat)) {
                any_applied = true;
            }
        }

        if (!any_applied) {
            report.success = false;
            report.status_message = "No se pudo abrir ningun Engine.ini para escritura";
            return report;
        }

        report.success = true;
        report.status_message = "Preset UMA Unreal Engine aplicado (PoolVRAM=" +
            std::to_string(effective_pool_mb) + "MB, AsyncShaders=ON, FlipModel=ON, 60FPS-VegaTuned)";
        return report;
    }

    if (game.engine == GameEngineType::Unity) {
        std::wstring boot_config_path = ResolveUnityBootConfigPath(game, custom_config_dir_override);
        if (boot_config_path.empty()) {
            report.success = false;
            report.status_message = "No se encontro carpeta *_Data ni boot.config para Unity";
            return report;
        }
        std::filesystem::path pBoot(boot_config_path);
        std::filesystem::path pBak(boot_config_path + L".vegaflow.bak");
        report.config_modified_path = boot_config_path;

        if (dry_run) {
            report.success = true;
            report.status_message = "Dry-run: Preset UMA Unity Engine validado";
            return report;
        }

        if (!std::filesystem::exists(pBak, ec)) {
            if (std::filesystem::exists(pBoot, ec)) {
                report.backup_created = fs_.Copy(
                    pBoot.wstring(),
                    pBak.wstring(),
                    true,
                    game.has_anticheat);
            } else {
                WriteStringToFile(fs_, pBak, "; VegaFlow Empty Backup\n", game.has_anticheat);
                report.backup_created = true;
            }
        }

        std::vector<std::pair<std::string, std::string>> keysToSet = {
            {"gfx-enable-gfx-jobs", "1"},
            {"gfx-enable-native-gfx-jobs", "1"},
            {"gfx-enable-mt-rendering", "1"},
            {"wait-for-native-debugger", "0"},
            {"hdr-display-enabled", "0"},
            {"gc-max-time-slice", "3"}
        };

        std::vector<std::string> lines;
        std::string existing;
        if (std::filesystem::exists(pBoot, ec) && fs_.Read(pBoot.wstring(), existing)) {
            std::istringstream in(existing);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
            }
        }

        for (const auto& [k, v] : keysToSet) {
            bool found = false;
            for (auto& l : lines) {
                if (l.rfind(k + "=", 0) == 0) {
                    l = k + "=" + v;
                    found = true;
                    break;
                }
            }
            if (!found) {
                lines.push_back(k + "=" + v);
            }
        }

        std::stringstream ss;
        for (const auto& l : lines) {
            ss << l << "\n";
        }

        if (!WriteStringToFile(fs_, pBoot, ss.str(), game.has_anticheat)) {
            report.success = false;
            report.status_message = "No se pudo abrir boot.config para escritura";
            return report;
        }

        report.success = true;
        report.status_message = "Preset UMA Unity aplicado (NativeGfxJobs=1, GfxJobs=1, GC-Slice=3ms, FlipModel=ON)";
        return report;
    }

    // For Source1_2, NativeDirectX_Vulkan or Unknown
    report.success = true;
    report.status_message = "Optimización DXGI Flip Model + Alta Prioridad GPU aplicada al ejecutable del juego";
    return report;
}

EngineOptimizationReport SteamEngineOptimizer::RestoreEngineOriginalConfig(
    const SteamGameEntry& game,
    bool dry_run,
    const std::wstring& custom_config_dir_override) {

    EngineOptimizationReport report{};
    std::error_code ec;

    if (game.engine == GameEngineType::UnrealEngine4_5) {
        auto ini_paths = ResolveAllUnrealEngineIniPaths(game, custom_config_dir_override);
        if (ini_paths.empty()) {
            ini_paths.push_back(ResolveUnrealEngineIniPath(game, custom_config_dir_override));
        }
        report.config_modified_path = ini_paths.front();

        bool any_restored = false;
        for (const auto& engine_ini_path : ini_paths) {
            std::filesystem::path pIni(engine_ini_path);
            std::filesystem::path pBak(engine_ini_path + L".vegaflow.bak");

            if (std::filesystem::exists(pBak, ec)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }

                std::string bakContent = ReadFileToString(pBak);
                if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
                    fs_.Remove(pIni.wstring(), game.has_anticheat);
                } else {
                    fs_.Copy(
                        pBak.wstring(),
                        pIni.wstring(),
                        true,
                        game.has_anticheat);
                }
                fs_.Remove(pBak.wstring(), game.has_anticheat);
                any_restored = true;
                continue;
            }

            if (std::filesystem::exists(pIni, ec)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }

                std::string content = ReadFileToString(pIni);

                const std::string tagStart = "; [VegaFlowUMA_Start]";
                const std::string tagEnd = "; [VegaFlowUMA_End]";
                size_t posStart = content.find(tagStart);
                if (posStart != std::string::npos) {
                    while (posStart != std::string::npos) {
                        size_t posEnd = content.find(tagEnd, posStart);
                        if (posEnd != std::string::npos) {
                            size_t removeLen = (posEnd + tagEnd.size()) - posStart;
                            if (posStart + removeLen < content.size() && content[posStart + removeLen] == '\n') {
                                removeLen++;
                            }
                            content.erase(posStart, removeLen);
                        } else {
                            content.erase(posStart);
                            break;
                        }
                        posStart = content.find(tagStart);
                    }
                    while (!content.empty() &&
                           (content.back() == '\r' || content.back() == '\n' || content.back() == ' ')) {
                        content.pop_back();
                    }
                    if (!content.empty()) content += "\n";
                    WriteStringToFile(fs_, pIni, content, game.has_anticheat);
                    any_restored = true;
                }
            }
        }

        report.success = true;
        if (dry_run) {
            report.status_message = "Dry-run: Restauracion de Engine.ini desde backup validada";
        } else if (any_restored) {
            report.status_message = "Configuracion original de Unreal Engine restaurada desde .vegaflow.bak";
        } else {
            report.status_message = "No habia configuracion modificada de Unreal Engine que restaurar";
        }
        return report;
    }

    if (game.engine == GameEngineType::Unity) {
        std::wstring boot_config_path = ResolveUnityBootConfigPath(game, custom_config_dir_override);
        if (boot_config_path.empty()) {
            report.success = true;
            report.status_message = "No se encontro ruta de configuracion para Unity";
            return report;
        }
        std::filesystem::path pBoot(boot_config_path);
        std::filesystem::path pBak(boot_config_path + L".vegaflow.bak");
        report.config_modified_path = boot_config_path;

        if (std::filesystem::exists(pBak, ec)) {
            if (dry_run) {
                report.success = true;
                report.status_message = "Dry-run: Restauracion de boot.config desde backup validada";
                return report;
            }

            std::string bakContent = ReadFileToString(pBak);
            if (bakContent.find("; VegaFlow Empty Backup") != std::string::npos) {
                fs_.Remove(pBoot.wstring(), game.has_anticheat);
            } else {
                fs_.Copy(
                    pBak.wstring(),
                    pBoot.wstring(),
                    true,
                    game.has_anticheat);
            }
            fs_.Remove(pBak.wstring(), game.has_anticheat);

            report.success = true;
            report.status_message = "Configuracion original de Unity restaurada desde .vegaflow.bak";
            return report;
        }

        report.success = true;
        report.status_message = "No habia copia de seguridad de Unity para restaurar";
        return report;
    }

    report.success = true;
    report.status_message = "No hay configuracion de archivos de motor que restaurar para este tipo de juego";
    return report;
}

} // namespace vegaflow
