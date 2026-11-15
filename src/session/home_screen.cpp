#include "session/home_screen.hpp"

#include <cstdio>

namespace vegaflow::session {

namespace {

std::string Tenths(uint64_t kb) { // KB as MB with one decimal and a decimal comma
    uint64_t whole = kb / 1024;
    uint64_t tenth = ((kb % 1024) * 10 + 512) / 1024; // rounded to nearest, with no kb * 10 to overflow
    if (tenth == 10) {
        ++whole;
        tenth = 0;
    }
    return std::to_string(whole) + "," + std::to_string(tenth);
}

} // namespace

HomeScreenModel BuildHomeScreen(const HomeInputs& in, const EquipmentScreenModel& eq, const ProgramsScreenModel& programs) {
    HomeScreenModel m;
    m.permission_ok = in.elevated;
    m.permission_text = in.elevated ? "Permisos: administrador" : "Permisos: sin administrador (parte de los ajustes no se puede aplicar)";
    m.memory_text = in.daemon_ws_kb == 0 ? "" : "VegaFlow usa " + Tenths(in.daemon_ws_kb) + " MB de memoria";

    m.game_active = in.game_active;
    if (in.game_active) {
        m.game_title = "Juego detectado";
        m.game_name = in.game_name.empty() ? "Juego sin nombre" : in.game_name;
        std::string sub;
        for (const std::string* part : {&in.game_engine, &in.game_api}) {
            if (part->empty()) continue;
            sub += (sub.empty() ? "" : " · ") + *part;
        }
        sub += std::string(sub.empty() ? "" : " · ") + (in.game_foreground ? "en primer plano" : "en segundo plano");
        m.game_sub = sub;
        m.can_pause = in.automatic && in.game_app_id != 0;
        m.pause_app_id = m.can_pause ? in.game_app_id : 0;
        m.game_facts.push_back("Programas de fondo en modo ahorro: " + std::to_string(in.background_saving));
        m.game_facts.push_back(std::string("Prioridad de disco alta pedida a Windows para el juego: ") + (in.io_priority_high ? "sí" : "no"));
        m.game_facts.push_back(std::string("DirectFlip pedido a Windows para este juego: ") + (in.direct_flip ? "sí" : "no"));
    } else {
        m.game_title = "En espera";
        m.game_name = in.automatic ? "Abre un juego y VegaFlow lo detecta solo" : "Modo seguro: VegaFlow no detecta juegos";
        m.waiting_text = in.automatic ? "VegaFlow no frena programas por su cuenta mientras no haya un juego abierto."
                                      : "Modo seguro: VegaFlow no cambia nada por su cuenta.";
    }

    if (!eq.cpu_line.empty()) m.equipment.push_back({"Procesador", eq.cpu_line});
    if (!eq.gpus.empty()) {
        const std::string gpu = eq.gpus[0].name.empty() ? eq.gpus[0].link : eq.gpus[0].link.empty() ? eq.gpus[0].name : eq.gpus[0].name + " · " + eq.gpus[0].link;
        if (!gpu.empty()) m.equipment.push_back({"Gráfica", gpu});
    }
    if (eq.tier >= 1 && eq.tier <= 4 && eq.tier_labels.size() == 4) m.equipment.push_back({"Nivel de tu equipo", eq.tier_labels[static_cast<size_t>(eq.tier - 1)]});

    if (in.ram_total_mb == 0) {
        m.memory_detail = "No se pudo leer la memoria del equipo.";
    } else {
        const uint64_t free_mb = in.ram_available_mb > in.ram_total_mb ? in.ram_total_mb : in.ram_available_mb;
        // 128-bit: a corrupt total near 2^63 must not overflow used*100 (MinGW-only project, so __int128 is available).
        const unsigned __int128 used = in.ram_total_mb - free_mb;
        m.memory_percent = static_cast<int>((used * 100 + in.ram_total_mb / 2) / in.ram_total_mb);
        m.memory_detail = std::to_string(free_mb) + " MB disponibles de " + std::to_string(in.ram_total_mb) + " MB";
    }
    const bool integrated = !eq.gpus.empty() && eq.gpus[0].link == "Integrada";
    m.memory_title = integrated ? "Memoria del equipo, compartida con la gráfica" : "Memoria del equipo";
    m.guard_text = in.guard_active ? "Veces que el guardián de memoria tuvo que actuar: " + std::to_string(in.guard_interventions)
                                   : "El guardián de memoria compartida está apagado (no aplica a este equipo o es modo seguro).";
    m.purge_text = (in.purge_before_free_mb == 0 && in.purge_after_free_mb == 0)
                       ? "Aún no liberó memoria desde que se abrió VegaFlow."
                       : "Última liberación: de " + std::to_string(in.purge_before_free_mb) + " MB libres a " + std::to_string(in.purge_after_free_mb) + " MB";

    m.background_text = in.game_active ? std::to_string(in.background_saving) + (in.background_saving == 1 ? " programa en modo ahorro" : " programas en modo ahorro")
                                       : "Ninguno por su cuenta: se frenan solo con un juego abierto.";
    std::string untouched;
    for (const auto& c : programs.categories) {
        if (c.action != ProcessAction::DoNotTouch) continue;
        untouched += (untouched.empty() ? "" : ", ") + c.title;
    }
    if (!untouched.empty()) m.background_note = "No se tocan: " + untouched + ".";
    return m;
}

} // namespace vegaflow::session
