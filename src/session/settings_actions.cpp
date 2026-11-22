#include "session/settings_actions.hpp"

namespace vegaflow::session {

std::string SettingsActions::Said(const std::string& msg, const char* audit_prefix) {
    if (d_.audit && audit_prefix && !quiet_) d_.audit(std::string(audit_prefix) + msg);
    return msg;
}

std::string SettingsActions::ToggleMpo() {
    auto& gfx = d_.gfx;
    const bool target = !gfx.QuerySystemGraphicsTweaks().mpo_disabled;
    const bool ok = gfx.Display().SetMpoDisabled(target, false);
    return Said(ok ? (target ? "MPO Desactivado (OverlayTestMode=5, no documentado)" : "MPO Restaurado a Default (OverlayTestMode=0)")
                   : "Fallo al modificar MPO (Requiere Administrador)",
                "Ajuste DWM MPO: ");
}

std::string SettingsActions::ToggleMemCompression() {
    auto& gfx = d_.gfx;
    const bool target = !gfx.QuerySystemGraphicsTweaks().memory_compression_enabled;
    const bool ok = gfx.KernelScheduler().SetMemoryCompressionEnabled(target, false);
    return Said(ok ? (target ? "Compresión de Memoria Habilitada" : "Compresión de Memoria Deshabilitada")
                   : "Fallo al cambiar compresión (Requiere Administrador)",
                "Ajuste Windows MMAgent: ");
}

std::string SettingsActions::ToggleShaderCache() {
    auto& gfx = d_.gfx;
    if (!gfx.AmdDriver()) return "Ajuste de ShaderCache AMD no disponible en esta GPU";
    const bool target = !gfx.QuerySystemGraphicsTweaks().amd_shader_cache_forced;
    const bool ok = gfx.AmdDriver()->SetShaderCacheForced(target, d_.policy, false);
    return Said(ok ? (target ? "ShaderCache AMD 0x3200 forzada (SurfaceFormat + FlipQueue=1)" : "AMD ShaderCache restaurada al valor del driver")
                   : "Fallo al cambiar ShaderCache (Requiere Administrador)",
                "Ajuste AMD ShaderCache: ");
}

std::string SettingsActions::ToggleVramOverride() {
    auto& gfx = d_.gfx;
    if (!gfx.AmdDriver()) return "Ajuste VRAM Reporting AMD no disponible en esta GPU";
    const ApuOptimizationPolicy pol = d_.live_policy ? d_.live_policy() : d_.policy;
    const bool target = !gfx.QuerySystemGraphicsTweaks().amd_vram_override_active;
    if (target && pol.safe_vram_pool_mb == 0) return Said("No se pudo calcular un tamaño de memoria gráfica para este equipo: no se cambió nada", "Ajuste VRAM Reporting: ");
    const bool ok = target ? gfx.AmdDriver()->ApplyVramReportingOverride(pol.safe_vram_pool_mb, false)
                           : gfx.AmdDriver()->RestoreVramReporting(false);
    const std::string pool = std::to_string(pol.safe_vram_pool_mb);
    return Said(ok ? (target ? "Sobrescritura VRAM UMA (" + pool + " MB DXGI/AMD) Activada (Pool Size = " + pool + " MB)"
                             : std::string("Reporte de VRAM devuelto al valor que tenía el driver"))
                   : (target ? "Fallo al modificar reporte de VRAM (Requiere Administrador)"
                             : "VegaFlow no tiene guardado el valor original (o falta ser administrador): no cambió nada. Cámbialo a mano en el registro o reinstala el controlador"),
                "Ajuste VRAM Reporting: ");
}

std::string SettingsActions::ToggleMmcssGaming() {
    auto& gfx = d_.gfx;
    const bool target = !gfx.QuerySystemGraphicsTweaks().mmcss_gaming_optimized;
    const bool ok = gfx.Mmcss().SetGamingProfileOptimized(target, false, !target);
    if (ok) gfx.KernelScheduler().SetDynamicPrioritySeparation(target ? 0x26 : 0x02, false, !target);
    return Said(ok ? (target ? "Planificador MMCSS Gaming + Quantum CPU (0x26) Activado" : "Planificador MMCSS y Quantum CPU restaurados a Stock Windows")
                   : "Fallo al optimizar MMCSS/Quantum (Requiere Administrador)",
                "Ajuste Kernel MMCSS/Quantum: ");
}

std::string SettingsActions::ToggleApuPowerMode() {
    auto& gfx = d_.gfx;
    if (!gfx.PowerMode()) return "Modo de energía APU no disponible para la política actual";
    const bool target = !gfx.QuerySystemGraphicsTweaks().apu_power_optimized;
    const bool ok = gfx.PowerMode()->SetApuPowerMode(target ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced, d_.policy, false);
    // The text states the ceiling of the policy that was just applied (the boot one), not a newer reading.
    const std::string on = d_.policy.cpu_stapm_cap_mhz == 0
        ? "Control Térmico APU: perfil sin tope activado (Efficient-Aggressive + EPP 25)"
        : "Control Térmico APU: Modo GPU-Priority Activado (" + std::to_string(d_.policy.cpu_stapm_cap_mhz) + " MHz de techo)";
    return Said(ok ? (target ? on : std::string("Control Térmico APU: Perfil Balanceado Estándar Restaurado")) : "Fallo al alternar modo de energía APU", "");
}

void SettingsActions::ResetAll(size_t* reverted, size_t* total) {
    auto& gfx = d_.gfx;
    // MPO is not written here: if VegaFlow changed it, its RegBackup copy is put back below; otherwise it is the user's.
    if (gfx.AmdDriver()) {
        gfx.AmdDriver()->SetShaderCacheForced(false, d_.policy, false);
        gfx.AmdDriver()->RestoreVramReporting(false);
    }
    gfx.KernelScheduler().RevertMemoryCompressionChange(false); // only what the journal says VegaFlow changed
    gfx.Mmcss().SetGamingProfileOptimized(false, false);
    gfx.Display().SetGameDvrCaptureDisabled(false, false);
    gfx.Display().SetFullscreenOptimizationsDisabled(false, false);
    if (gfx.PowerMode()) gfx.PowerMode()->SetApuLaptopPowerProfile(false, d_.policy, false);
    gfx.KernelScheduler().SetDynamicPrioritySeparation(0x02, false);
    gfx.KernelScheduler().SetKernelMemoryPagingShield(false, false);
    gfx.RestoreAllRegistryBackups(reverted, total);
}

std::string SettingsActions::Set(const std::string& id, bool turn_on) {
    auto& gfx = d_.gfx;
    const auto cur = gfx.QuerySystemGraphicsTweaks();
    auto toggle_if = [&](bool is_on, std::string (SettingsActions::*fn)()) {
        if (is_on == turn_on) return std::string("Ya estaba así: sin cambios");
        quiet_ = true;
        const std::string answer = (this->*fn)();
        quiet_ = false;
        return answer;
    };
    std::string msg;
    if (id == "vram_reporting") {
        msg = toggle_if(cur.amd_vram_override_active, &SettingsActions::ToggleVramOverride);
    } else if (id == "amd_driver_profile") {
        msg = toggle_if(cur.amd_shader_cache_forced, &SettingsActions::ToggleShaderCache);
    } else if (id == "mpo_disabled") {
        msg = toggle_if(cur.mpo_disabled, &SettingsActions::ToggleMpo);
    } else if (id == "memory_compression_off") {
        msg = toggle_if(!cur.memory_compression_enabled, &SettingsActions::ToggleMemCompression); // E2.2: on = compression off
    } else if (id == "power_profile") {
        msg = toggle_if(cur.apu_power_optimized, &SettingsActions::ToggleApuPowerMode);
    } else if (id == "mmcss_gaming") {
        const bool ok = gfx.Mmcss().SetGamingProfileOptimized(turn_on, false, !turn_on);
        msg = ok ? (turn_on ? "Prioridad de multimedia para juegos activada" : "Prioridad de multimedia devuelta al valor de Windows")
                 : "No se pudo cambiar la prioridad de multimedia (hace falta ser administrador)";
    } else if (id == "priority_separation") {
        const bool ok = gfx.KernelScheduler().SetDynamicPrioritySeparation(turn_on ? 0x26 : 0x02, false, !turn_on);
        msg = ok ? (turn_on ? "Tiempo de CPU para la ventana activa: 0x26" : "Tiempo de CPU devuelto al valor de Windows (2)")
                 : "No se pudo cambiar Win32PrioritySeparation (hace falta ser administrador)";
    } else if (id == "paging_shield") {
        const bool ok = gfx.KernelScheduler().SetKernelMemoryPagingShield(turn_on, false, !turn_on);
        msg = ok ? (turn_on ? "Núcleo de Windows fijado en la RAM" : "Núcleo de Windows devuelto al comportamiento normal")
                 : "No se pudo cambiar el blindaje del núcleo (hace falta ser administrador)";
    } else if (id == "game_dvr") {
        const bool ok = gfx.Display().SetGameDvrCaptureDisabled(turn_on, false, !turn_on);
        msg = ok ? (turn_on ? "Grabación de Xbox Game Bar apagada" : "Grabación de Xbox Game Bar devuelta a Windows")
                 : "No se pudo cambiar la grabación de Game Bar";
    } else if (id == "fse_compat") {
        const bool ok = gfx.Display().SetFullscreenOptimizationsDisabled(turn_on, false);
        msg = ok ? (turn_on ? "Optimizaciones de pantalla completa apagadas (modo compatibilidad)"
                            : "Optimizaciones de pantalla completa devueltas a como estaban")
                 : "No se pudieron cambiar las optimizaciones de pantalla completa";
    } else {
        msg = "Este ajuste no tiene interruptor";
    }
    if (d_.audit) d_.audit("Ajustes: " + id + (turn_on ? " activar: " : " desactivar: ") + msg);
    return msg;
}

} // namespace vegaflow::session
