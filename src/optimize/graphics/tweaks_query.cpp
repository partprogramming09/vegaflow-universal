#include "optimize/graphics/graphics_internal.hpp"
#include "windows/change_journal.hpp"

#include <cstring>

namespace vegaflow {

using namespace graphics_internal;

namespace {

uint32_t ParseTessellationValue(const RegValue& val) {
    if (val.type == RegValueType::Dword) {
        return val.dword;
    }
    if (val.type == RegValueType::String) {
        try {
            return static_cast<uint32_t>(std::stoul(val.str));
        } catch (...) {
            return 0;
        }
    }
    if (val.type == RegValueType::Binary && !val.binary.empty()) {
        const size_t sz = val.binary.size();
        if (sz >= 4 && val.binary[1] == 0x00 && (sz % 2 == 0)) {
            std::wstring ws;
            ws.reserve(sz / 2);
            for (size_t i = 0; i + 1 < sz; i += 2) {
                const wchar_t ch = static_cast<wchar_t>(
                    static_cast<uint16_t>(val.binary[i]) | (static_cast<uint16_t>(val.binary[i + 1]) << 8));
                if (ch == L'\0') break;
                ws.push_back(ch);
            }
            try {
                return static_cast<uint32_t>(std::stoul(ws));
            } catch (...) {}
        }
        std::string s(reinterpret_cast<const char*>(val.binary.data()), sz);
        while (!s.empty() && s.back() == '\0') s.pop_back();
        try {
            return static_cast<uint32_t>(std::stoul(s));
        } catch (...) {}
    }
    return 0;
}

bool CheckShaderCacheVal(const RegValue& val) {
    if (val.type == RegValueType::Binary && val.binary.size() >= 2) {
        return (val.binary[0] == 0x32 && val.binary[1] == 0x00);
    }
    if (val.type == RegValueType::String && !val.str.empty()) {
        return (val.str[0] == L'2');
    }
    if (val.type == RegValueType::Dword) {
        return (val.dword == 2u || val.dword == 0x32u);
    }
    return false;
}

bool CheckBoostDigit(const RegValue& val, bool allow_three = false) {
    if (val.type == RegValueType::Binary && val.binary.size() >= 2) {
        return (val.binary[0] == 0x31 && val.binary[1] == 0x00) ||
               (allow_three && val.binary[0] == 0x33 && val.binary[1] == 0x00);
    }
    if (val.type == RegValueType::String && !val.str.empty()) {
        return (val.str[0] == L'1') || (allow_three && val.str[0] == L'3');
    }
    if (val.type == RegValueType::Dword) {
        return (val.dword == 1u || val.dword == 0x31u) ||
               (allow_three && (val.dword == 3u || val.dword == 0x33u));
    }
    return false;
}

} // namespace

void AmdDriverTweaks::QueryAdapterState(
    bool& out_shader_cache_forced,
    bool& out_dx11_uma_boost,
    uint32_t& out_tess_max) {
    out_shader_cache_forced = false;
    out_dx11_uma_boost = false;
    out_tess_max = 0;

    for (const auto* sub : kAdapterSubkeys) {
        const std::wstring adapter_path = std::wstring(kDisplayClassBase) + L"\\" + sub;
        if (!IsAmdAdapterKey(reg_, adapter_path, device_id_)) {
            continue;
        }

        RegValue val;
        if (reg_.ReadValue(RegHive::LocalMachine, adapter_path, L"ShaderCache", val) && CheckShaderCacheVal(val)) {
            out_shader_cache_forced = true;
        }

        const std::wstring umd_path = adapter_path + L"\\UMD";
        if (reg_.KeyExists(RegHive::LocalMachine, umd_path)) {
            if (reg_.ReadValue(RegHive::LocalMachine, umd_path, L"ShaderCache", val) && CheckShaderCacheVal(val)) {
                out_shader_cache_forced = true;
            }
            if (reg_.ReadValue(RegHive::LocalMachine, umd_path, L"SurfaceFormatReplacements", val) && CheckBoostDigit(val, false)) {
                out_dx11_uma_boost = true;
            }
            if (reg_.ReadValue(RegHive::LocalMachine, umd_path, L"FlipQueueSize", val) && CheckBoostDigit(val, true)) {
                out_dx11_uma_boost = true;
            }
            if (reg_.ReadValue(RegHive::LocalMachine, umd_path, L"Tessellation", val)) {
                out_tess_max = ParseTessellationValue(val);
            }
        }

        if (out_shader_cache_forced && out_dx11_uma_boost && out_tess_max > 0) {
            break;
        }
    }
}

GraphicsOptimizer::GraphicsOptimizer(
    const SystemHardwareProfile& hw,
    const ApuOptimizationPolicy& policy,
    RegistryPort& registry,
    PowerPort* power)
    : registry_(registry),
      power_(power),
      gpu_(hw.primary_gpu),
      policy_(policy),
      kernel_(registry),
      mmcss_(registry),
      display_(registry),
      dxgi_(registry) {
    // Single centralized GPU vendor decision: AMD driver tweaks exist ONLY on AMD GPUs when allowed by policy.
    if (hw.primary_gpu.vendor == GpuVendor::AMD && !policy.block_driver_tweaks) {
        amd_driver_.emplace(registry_, hw.primary_gpu.device_id, hw.primary_gpu.connection == GpuConnection::Integrated);
    }
    if (power != nullptr && policy.force_high_perf_power_plan) {
        power_mode_.emplace(*power);
    }
}

GraphicsOptimizer::RebindResult GraphicsOptimizer::Rebind(
    const SystemHardwareProfile& hw, const ApuOptimizationPolicy& policy, bool dry_run) {
    RebindResult result{};
    const GpuProfile& g = hw.primary_gpu;
    const bool same_gpu = g.vendor == gpu_.vendor && g.device_id == gpu_.device_id && g.luid == gpu_.luid &&
                          g.connection == gpu_.connection;
    const bool same_policy = policy.block_driver_tweaks == policy_.block_driver_tweaks &&
                             policy.force_high_perf_power_plan == policy_.force_high_perf_power_plan;
    result.rebuilt = !(same_gpu && same_policy);
    if (!result.rebuilt || dry_run) {
        return result;
    }

    // The VRAM override belongs to the old iGPU: put it back before the tweaks point elsewhere.
    if (amd_driver_ && gpu_.connection == GpuConnection::Integrated) {
        amd_driver_->RestoreVramReporting(false);
        uint32_t driver_mb = 0;
        uint64_t dxgi_bytes = 0;
        amd_driver_->QueryVramReporting(driver_mb, dxgi_bytes);
        // shortcut: "still overridden" = still at the pool size; misses an original that equals the pool size.
        const uint64_t pool_bytes = static_cast<uint64_t>(policy_.safe_vram_pool_mb) * 1024ULL * 1024ULL;
        result.uma_restore_failed = driver_mb == policy_.safe_vram_pool_mb || dxgi_bytes == pool_bytes;
    }

    amd_driver_.reset();
    if (g.vendor == GpuVendor::AMD && !policy.block_driver_tweaks) {
        amd_driver_.emplace(registry_, g.device_id, g.connection == GpuConnection::Integrated);
    }
    // PowerModeTweaks keeps the saved plan: only drop it (after restoring) when the new policy no longer wants it.
    if (power_mode_ && !policy.force_high_perf_power_plan) {
        if (power_mode_->IsApuPowerModeApplied()) {
            power_mode_->SetApuLaptopPowerProfile(false, policy_, false);
        }
        power_mode_.reset();
    } else if (!power_mode_ && power_ != nullptr && policy.force_high_perf_power_plan) {
        power_mode_.emplace(*power_);
    }

    if (ChangeJournal* j = GetActiveChangeJournal()) {
        j->RecordAuditEvent("GPU", "Cambio de GPU principal: ajustes reorientados",
                            result.uma_restore_failed ? "No se pudo devolver la VRAM de la iGPU anterior; usa --restore-registry." : "");
    }
    gpu_ = g;
    policy_ = policy;
    return result;
}

SystemGraphicsTweaksState GraphicsOptimizer::QuerySystemGraphicsTweaks() {
    SystemGraphicsTweaksState state{};

    display_.QueryMpoState(state.mpo_disabled, state.dwm_overlay_test_mode);
    state.memory_compression_enabled = kernel_.IsMemoryCompressionEnabled();
    state.win32_priority_separation = kernel_.QueryDynamicPrioritySeparation();
    state.mmcss_gaming_optimized = mmcss_.QueryGamingProfileOptimized();
    state.gamedvr_disabled = display_.QueryGameDvrDisabled();
    state.fse_disabled = display_.QueryFullscreenOptimizationsDisabled();
    state.kernel_paging_shield_active = kernel_.QueryKernelMemoryPagingShield();
    mmcss_.QueryTaskTweaks(state.mmcss_scheduling_category, state.mmcss_sfio_priority, state.mmcss_clock_rate);

    if (power_mode_) {
        state.apu_power_optimized = power_mode_->QueryApuPowerOptimized();
    }

    if (amd_driver_) {
        amd_driver_->QueryAdapterState(
            state.amd_shader_cache_forced,
            state.amd_dx11_uma_boost,
            state.amd_tessellation_max);

        uint64_t dxgi_vram_bytes = 0;
        amd_driver_->QueryVramReporting(state.amd_vram_reported_mb, dxgi_vram_bytes);
        state.amd_vram_override_active = (state.amd_vram_reported_mb >= 2048 || dxgi_vram_bytes >= 2147483648ULL);
        if (state.amd_vram_reported_mb == 0 && dxgi_vram_bytes > 0) {
            state.amd_vram_reported_mb = static_cast<uint32_t>(dxgi_vram_bytes / (1024ULL * 1024ULL));
        }
    }

    return state;
}

bool GraphicsOptimizer::RestoreAllRegistryBackups(size_t* out_reverted, size_t* out_total) {
    return registry_.RestoreAllBackups(out_reverted, out_total);
}

} // namespace vegaflow
