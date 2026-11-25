#include "model/engine_presets.hpp"

#include <algorithm>

namespace vegaflow {

const std::vector<std::pair<std::string, std::string>>& UnityBootConfigKeys() {
    static const std::vector<std::pair<std::string, std::string>> keys = {
        {"gfx-enable-gfx-jobs", "1"},
        {"gfx-enable-native-gfx-jobs", "1"},
        {"gfx-enable-mt-rendering", "1"},
        {"wait-for-native-debugger", "0"},
        {"hdr-display-enabled", "0"},
        {"gc-max-time-slice", "3"}
    };
    return keys;
}

std::string GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override) {
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

} // namespace vegaflow
