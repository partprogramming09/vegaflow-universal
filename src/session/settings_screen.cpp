#include "session/settings_screen.hpp"

namespace vegaflow {

bool SettingIsOn(const SettingSpec& spec, const SettingsScreenInputs& in) {
    const std::string id = spec.id;
    const SystemGraphicsTweaksState& t = in.tweaks;
    if (id == "memory_guard") return in.memory_guard_active;
    if (id == "vram_reporting") return t.amd_vram_override_active;
    if (id == "amd_driver_profile") return t.amd_shader_cache_forced;
    if (id == "priority_separation") return t.win32_priority_separation == 0x26 || t.win32_priority_separation == 0x28; // only the values VegaFlow writes; a value of the user is not "on"
    if (id == "paging_shield") return t.kernel_paging_shield_active;
    if (id == "mpo_disabled") return t.mpo_disabled;
    if (id == "mmcss_gaming") return t.mmcss_gaming_optimized;
    if (id == "game_dvr") return t.gamedvr_disabled;
    if (id == "fse_compat") return t.fse_disabled;
    if (id == "memory_compression_off") return !t.memory_compression_enabled; // on = VegaFlow (or the user) turned it off
    if (id == "power_profile") return t.apu_power_optimized;
    if (id == "engine_presets") return in.auto_engine_presets;
    return false; // a catalog row without a case here is shown as off, never as applied
}

std::string EvidenceLabel(const SettingSpec& spec) {
    switch (spec.evidence) {
    case Evidence::Gain: return "Recomendado";
    case Evidence::Loss: return "No recomendado";
    case Evidence::NoEffect: return "Sin efecto medido";
    case Evidence::NotMeasured: break;
    }
    return "Sin medir";
}

std::string SettingPreview(const SettingSpec& spec, bool on_now, bool elevated) {
    std::string t = std::string("Ahora: ") + (on_now ? "encendido" : "apagado") + ".\n";
    if (!spec.user_switch) return t + "VegaFlow lo decide solo: no hay interruptor que pulsar.";
    if (spec.config_key) { // nothing is written to the system now, so there is no "original value" to give back
        return t + (on_now ? "Si lo apagas: los juegos que abras después no se tocan. Lo ya escrito se quita desde Juegos."
                           : "Si lo enciendes: al detectar un juego con Unreal Engine, VegaFlow escribe un bloque en su Engine.ini (con copia .vegaflow.bak). Ahora no cambia nada.");
    }
    t += on_now ? "Si lo apagas: VegaFlow devuelve el valor que tenía antes."
                : std::string("Si lo enciendes: VegaFlow escribe ") + spec.change + " en " + spec.target + ".";
    t += std::string("\nSe puede deshacer: ") + (spec.undoable ? "sí, desde Historial." : "no: no deja un valor original guardado.");
    if (!elevated) t += "\nVegaFlow no corre como administrador: el cambio puede no aplicarse y te lo dirá.";
    return t;
}

SettingsScreenModel BuildSettingsScreen(const SettingsScreenInputs& in) {
    SettingsScreenModel model;
    model.experimental_notice = in.show_experimental;
    for (const FamilyView& view : VisibleFamilies(in.device, in.show_experimental)) {
        SettingsFamilyModel fam;
        fam.family = view.family;
        fam.title = view.title;
        fam.scope = view.scope;
        for (const SettingSpec* spec : view.rows) {
            const bool on = SettingIsOn(*spec, in);
            SettingRow row{spec, on, EvidenceLabel(*spec), ChangeDescription(*spec), SettingPreview(*spec, on, in.elevated)};
            if (std::string(spec->id) == "memory_compression_off") {
                row.discouraged = in.compression_advice == CompressionAdvice::Discouraged;
                row.needs_confirm = in.compression_advice != CompressionAdvice::Allowed;
                if (row.discouraged) row.preview += "\nDesaconsejado: con 16 GB de RAM o menos la compresión ayuda; te pedirá confirmar.";
                if (row.needs_confirm && !row.discouraged) row.preview += "\nNo se pudo leer la RAM del equipo: te pedirá confirmar.";
            }
            fam.rows.push_back(std::move(row));
        }
        model.families.push_back(std::move(fam));
    }
    return model;
}

} // namespace vegaflow
