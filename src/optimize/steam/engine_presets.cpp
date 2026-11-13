#include "optimize/steam/steam_engine_optimizer.hpp"
#include "optimize/steam/steam_internal.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "bridge/rust_bridge.h"
#include "windows/change_journal.hpp"

namespace vegaflow {

using namespace steam_internal;

namespace {

constexpr const char* kEmptyBackupSentinel = "; VegaFlow Empty Backup\n";

std::string WideToUtf8Preset(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

uint64_t JournalBeginGameConfig(const std::wstring& file_path, const std::wstring& bak_path,
                                const std::string& after_desc, const std::string& reason) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal || file_path.empty() || bak_path.empty()) return 0;
    JournalEntry e;
    e.kind = JournalTargetKind::GameConfigFile;
    e.target = "GameFile:" + WideToUtf8Preset(file_path);
    e.reason = reason;
    e.before_value = "Backup en " + WideToUtf8Preset(bak_path);
    e.after_value = after_desc;
    e.manual_fix_hint = "Restaura " + WideToUtf8Preset(file_path) + " desde " + WideToUtf8Preset(bak_path);
    e.file_path = file_path;
    e.backup_path = bak_path;
    return journal->RecordPending(std::move(e));
}

void JournalEndGameConfig(uint64_t id, bool ok) {
    if (id == 0) return;
    if (ChangeJournal* journal = GetActiveChangeJournal()) {
        if (ok) journal->MarkCommitted(id);
        else journal->MarkFailed(id, "Error al escribir archivo de configuracion del juego");
    }
}

void JournalMarkGameConfigRestored(const std::wstring& file_path) {
    if (ChangeJournal* journal = GetActiveChangeJournal()) {
        journal->MarkMatchingReverted(JournalTargetKind::GameConfigFile, "GameFile:" + WideToUtf8Preset(file_path));
    }
}

bool WriteStringToFile(FileSystem& fs, const std::filesystem::path& path, const std::string& content, bool is_anticheat_protected = false) {
    return fs.Write(path.wstring(), content, is_anticheat_protected);
}

void CollectBytesCallback(void* ctx, const uint8_t* data, size_t len) {
    auto* out = static_cast<std::string*>(ctx);
    out->assign(reinterpret_cast<const char*>(data), len);
}

bool EnsureBackupBeforeModify(
    FileSystem& fs,
    const std::wstring& target_path,
    const std::wstring& bak_path,
    bool has_anticheat,
    bool& out_backup_created) {

    if (fs.Exists(bak_path)) {
        return true;
    }
    if (fs.Exists(target_path)) {
        if (!fs.Copy(target_path, bak_path, true, has_anticheat)) {
            return false;
        }
        out_backup_created = true;
        return true;
    }
    if (!fs.Write(bak_path, kEmptyBackupSentinel, has_anticheat)) {
        return false;
    }
    out_backup_created = true;
    return true;
}

bool RestoreFromBackupVerified(
    FileSystem& fs,
    const std::wstring& target_path,
    const std::wstring& bak_path,
    bool has_anticheat) {

    std::string bak_bytes;
    if (!fs.Read(bak_path, bak_bytes)) {
        return false;
    }

    if (bak_bytes.find("; VegaFlow Empty Backup") != std::string::npos) {
        if (fs.Exists(target_path)) {
            if (!fs.Remove(target_path, has_anticheat)) {
                return false;
            }
        }
        if (fs.Exists(target_path)) {
            return false;
        }
        return fs.Remove(bak_path, has_anticheat);
    }

    if (!fs.Copy(bak_path, target_path, true, has_anticheat)) {
        return false;
    }
    std::string restored_bytes;
    if (!fs.Read(target_path, restored_bytes) || restored_bytes != bak_bytes) {
        return false;
    }
    return fs.Remove(bak_path, has_anticheat);
}

} // namespace

std::string SteamEngineOptimizer::GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override) {
    std::string umaBlock;
    umaBlock += "\n\n; [VegaFlowUMA_Start]\n";
    umaBlock += "[SystemSettings]\n";
    umaBlock += "r.ShaderPipelineCache.Enabled=1\n";

    if (!policy.apply_visual_cuts) {
        if (policy.apply_egpu_streaming_tweaks) {
            umaBlock += "r.Streaming.AmortizeCPUToGPUCopy=1\n";
            umaBlock += "r.Streaming.MaxNumTexturesToStreamPerFrame=40\n";
        }
        umaBlock += "; [VegaFlowUMA_End]\n";
        return umaBlock;
    }

    uint32_t effective_pool_mb = 0;
    if (policy.tier == ApuTier::Tier1_EntryUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb_override, policy.safe_vram_pool_mb);
    } else if (policy.tier == ApuTier::Tier2_MidVegaUMA) {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override >= 3000) ? 4096 : vram_pool_mb_override;
    } else {
        effective_pool_mb = (vram_pool_mb_override == 0 || vram_pool_mb_override == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb_override;
    }

    umaBlock += "r.Streaming.PoolSize=" + std::to_string(effective_pool_mb) + "\n";
    umaBlock += "r.Streaming.LimitPoolSizeToVRAM=0\n";
    umaBlock += "r.Streaming.FullyLoadUsedTextures=1\n";
    umaBlock += "r.Streaming.FramesForFullUpdate=1\n";
    umaBlock += "r.Streaming.MaxNumTexturesToStreamPerFrame=120\n";
    umaBlock += "r.Streaming.AmortizeCPUToGPUCopy=0\n";
    umaBlock += "r.Streaming.Boost=1.5\n";
    umaBlock += "r.OneFrameThreadLag=1\n";
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
            report.status_message = policy.apply_visual_cuts
                ? "Dry-run: Preset UMA Unreal Engine 4/5 validado"
                : "Dry-run: Preset Unreal Engine (solo cache PSO, sin recortes visuales) validado";
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
            std::wstring pIni = engine_ini_path;
            std::wstring pBak = engine_ini_path + L".vegaflow.bak";

            if (!EnsureBackupBeforeModify(fs_, pIni, pBak, game.has_anticheat, report.backup_created)) {
                report.success = false;
                report.status_message = "No se pudo crear la copia de seguridad .vegaflow.bak; juego intacto";
                return report;
            }

            std::string existingContent;
            if (fs_.Exists(pIni)) {
                if (!fs_.Read(pIni, existingContent)) {
                    report.success = false;
                    report.status_message = "No se pudo leer Engine.ini existente; juego intacto";
                    return report;
                }
            }

            std::string finalContent;
            uint32_t merge_rc = vf_rust_ini_merge_unreal(
                reinterpret_cast<const uint8_t*>(existingContent.data()),
                existingContent.size(),
                umaBlock.data(),
                umaBlock.size(),
                CollectBytesCallback,
                &finalContent);
            if (merge_rc != VF_INI_OK) {
                report.success = false;
                report.status_message = (merge_rc == VF_INI_ERR_MISSING_END_MARKER || merge_rc == VF_INI_ERR_UNMATCHED_END_MARKER)
                    ? "Marcador de bloque VegaFlow incompleto en Engine.ini; archivo intacto"
                    : "Codificacion de Engine.ini no valida; archivo intacto";
                return report;
            }

            const std::string desc = policy.apply_visual_cuts
                ? ("Preset UMA Unreal Engine (PoolVRAM=" + std::to_string(effective_pool_mb) + "MB)")
                : (policy.apply_egpu_streaming_tweaks
                    ? std::string("Preset eGPU Unreal Engine (ShaderCache=ON + Streaming amortizado)")
                    : std::string("Preset Unreal Engine sin recortes (ShaderCache=ON)"));
            const uint64_t j_ini = JournalBeginGameConfig(pIni, pBak, desc, "Preset Unreal Engine");
            if (WriteStringToFile(fs_, pIni, finalContent, game.has_anticheat)) {
                JournalEndGameConfig(j_ini, true);
                any_applied = true;
            } else {
                JournalEndGameConfig(j_ini, false);
            }
        }

        if (!any_applied) {
            report.success = false;
            report.status_message = "No se pudo abrir ningun Engine.ini para escritura";
            return report;
        }

        report.success = true;
        if (!policy.apply_visual_cuts) {
            report.status_message = policy.apply_egpu_streaming_tweaks
                ? "Preset eGPU Unreal aplicado (ShaderCache=ON, Streaming amortizado, sin recortes visuales)"
                : "Preset Unreal aplicado sin recortes visuales (ShaderCache=ON, FlipModel=ON)";
        } else {
            report.status_message = "Preset UMA Unreal Engine aplicado (PoolVRAM=" +
                std::to_string(effective_pool_mb) + "MB, AsyncShaders=ON, FlipModel=ON, 60FPS-VegaTuned)";
        }
        return report;
    }

    if (game.engine == GameEngineType::Unity) {
        std::wstring boot_config_path = ResolveUnityBootConfigPath(game, custom_config_dir_override);
        if (boot_config_path.empty()) {
            report.success = false;
            report.status_message = "No se encontro carpeta *_Data ni boot.config para Unity";
            return report;
        }
        std::wstring pBoot = boot_config_path;
        std::wstring pBak = boot_config_path + L".vegaflow.bak";
        report.config_modified_path = boot_config_path;

        if (dry_run) {
            report.success = true;
            report.status_message = "Dry-run: Preset UMA Unity Engine validado";
            return report;
        }

        if (!EnsureBackupBeforeModify(fs_, pBoot, pBak, game.has_anticheat, report.backup_created)) {
            report.success = false;
            report.status_message = "No se pudo crear la copia de seguridad .vegaflow.bak de Unity; juego intacto";
            return report;
        }

        std::string existing;
        if (fs_.Exists(pBoot)) {
            if (!fs_.Read(pBoot, existing)) {
                report.success = false;
                report.status_message = "No se pudo leer boot.config existente; juego intacto";
                return report;
            }
        }

        const std::pair<std::string, std::string> keysToSet[] = {
            {"gfx-enable-gfx-jobs", "1"},
            {"gfx-enable-native-gfx-jobs", "1"},
            {"gfx-enable-mt-rendering", "1"},
            {"wait-for-native-debugger", "0"},
            {"hdr-display-enabled", "0"},
            {"gc-max-time-slice", "3"}
        };
        std::vector<VfIniKv> kv;
        kv.reserve(6);
        for (const auto& [k, v] : keysToSet) {
            kv.push_back(VfIniKv{k.data(), k.size(), v.data(), v.size()});
        }

        std::string merged;
        uint32_t merge_rc = vf_rust_ini_merge_unity(
            reinterpret_cast<const uint8_t*>(existing.data()),
            existing.size(),
            kv.data(),
            kv.size(),
            CollectBytesCallback,
            &merged);
        if (merge_rc != VF_INI_OK) {
            report.success = false;
            report.status_message = "Codificacion de boot.config no valida; archivo intacto";
            return report;
        }

        const uint64_t j_boot = JournalBeginGameConfig(pBoot, pBak, "Preset UMA Unity", "Preset UMA Unity");
        if (!WriteStringToFile(fs_, pBoot, merged, game.has_anticheat)) {
            JournalEndGameConfig(j_boot, false);
            report.success = false;
            report.status_message = "No se pudo abrir boot.config para escritura";
            return report;
        }
        JournalEndGameConfig(j_boot, true);

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

    if (game.engine == GameEngineType::UnrealEngine4_5) {
        auto ini_paths = ResolveAllUnrealEngineIniPaths(game, custom_config_dir_override);
        if (ini_paths.empty()) {
            ini_paths.push_back(ResolveUnrealEngineIniPath(game, custom_config_dir_override));
        }
        report.config_modified_path = ini_paths.front();

        bool any_restored = false;
        for (const auto& engine_ini_path : ini_paths) {
            std::wstring pIni = engine_ini_path;
            std::wstring pBak = engine_ini_path + L".vegaflow.bak";

            if (fs_.Exists(pBak)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }
                if (!RestoreFromBackupVerified(fs_, pIni, pBak, game.has_anticheat)) {
                    report.success = false;
                    report.status_message = "Fallo al restaurar o verificar Engine.ini; se conserva .vegaflow.bak";
                    return report;
                }
                JournalMarkGameConfigRestored(pIni);
                any_restored = true;
                continue;
            }

            if (fs_.Exists(pIni)) {
                if (dry_run) {
                    any_restored = true;
                    continue;
                }

                std::string content;
                if (!fs_.Read(pIni, content)) {
                    report.success = false;
                    report.status_message = "No se pudo leer Engine.ini para retirar el bloque";
                    return report;
                }

                bool changed = false;
                std::string out_bytes;
                uint32_t rc = vf_rust_ini_remove_unreal(
                    reinterpret_cast<const uint8_t*>(content.data()),
                    content.size(),
                    &changed,
                    CollectBytesCallback,
                    &out_bytes);
                if (rc != VF_INI_OK) {
                    report.success = false;
                    report.status_message = "Marcador incompleto en Engine.ini; no se modifico el archivo";
                    return report;
                }
                if (changed) {
                    if (!WriteStringToFile(fs_, pIni, out_bytes, game.has_anticheat)) {
                        report.success = false;
                        report.status_message = "No se pudo escribir Engine.ini al restaurar";
                        return report;
                    }
                    JournalMarkGameConfigRestored(pIni);
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
        std::wstring pBoot = boot_config_path;
        std::wstring pBak = boot_config_path + L".vegaflow.bak";
        report.config_modified_path = boot_config_path;

        if (fs_.Exists(pBak)) {
            if (dry_run) {
                report.success = true;
                report.status_message = "Dry-run: Restauracion de boot.config desde backup validada";
                return report;
            }
            if (!RestoreFromBackupVerified(fs_, pBoot, pBak, game.has_anticheat)) {
                report.success = false;
                report.status_message = "Fallo al restaurar o verificar boot.config; se conserva .vegaflow.bak";
                return report;
            }
            JournalMarkGameConfigRestored(pBoot);

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
