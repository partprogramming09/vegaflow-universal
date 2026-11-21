#include "session/equipment_screen.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace vegaflow::session {

namespace {

bool Contains(const std::vector<uint32_t>& v, uint32_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

int TierOf(const std::string& name) {
    if (name.rfind("Tier", 0) != 0 || name.size() < 5 || name[4] < '1' || name[4] > '4') return 0;
    return name[4] - '0';
}

GpuRow GpuRowOf(const GpuInfo& g) {
    GpuRow r;
    r.name = g.adapter_name.empty() ? "Gráfica no leída" : g.adapter_name;
    r.link = g.connection.empty() ? "No se pudo leer" : g.connection;
    if (g.connection == "Integrada") r.note = "Comparte la RAM del sistema: VegaFlow vigila la memoria y aplica el perfil para gráfica integrada.";
    else if (g.connection == "Dedicada") r.note = "Tiene memoria propia: VegaFlow no vigila la memoria compartida.";
    else if (g.connection == "Externa") r.note = "Gráfica externa (eGPU): tiene memoria propia, y el juego puede usarla o no según la preferencia de Windows.";
    else r.note = "No se pudo leer qué tipo de gráfica es, así que VegaFlow aplica solo lo que vale para cualquiera.";
    return r;
}

} // namespace

EquipmentScreenModel BuildEquipmentScreen(const SystemInfoSnapshot& s, const std::vector<CpuSetRef>& sets, const std::vector<uint32_t>& game,
                                          const std::vector<uint32_t>& background, bool restrict_game) {
    EquipmentScreenModel m;
    m.tier_labels = {"Básica", "Intermedia", "Avanzada", "Dedicada o externa"};
    m.tier = TierOf(s.apu_guard.tier_name);
    // The level comes from RAM, core count and graphics architecture, not from the graphics generation alone.
    const char* tier_notes[] = {"No se pudo determinar el nivel de tu equipo: VegaFlow usa los valores más prudentes.",
                                "Nivel 1: equipo con poca RAM o pocos núcleos. Con gráfica integrada puede activarse el guardián de memoria compartida.",
                                "Nivel 2: equipo de gama media. Con gráfica integrada puede activarse el guardián de memoria compartida.",
                                "Nivel 3: equipo con más memoria o gráfica reciente. Con gráfica integrada puede activarse el guardián de memoria compartida.",
                                "Nivel 4: gráfica dedicada, externa o que no se pudo leer. VegaFlow no vigila la memoria compartida."};
    m.tier_note = tier_notes[m.tier];

    const uint32_t logical = s.cpu.logical_processors;
    const uint32_t physical = s.cpu.physical_cores;
    m.has_data = logical > 0 && !s.cpu.brand_string.empty() && !sets.empty();
    m.gpus.push_back(GpuRowOf(s.gpu));
    if (!m.has_data) {
        m.cpu_line = "No se pudo leer el procesador.";
        m.cpu_note = "Sin datos del procesador, VegaFlow no separa núcleos.";
        return m;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, " · %u núcleos, %u hilos", physical, logical);
    m.cpu_line = s.cpu.brand_string + buf;
    if (s.ram.total_physical_gb > 0.0) {
        std::snprintf(buf, sizeof buf, " · %.0f GB", s.ram.total_physical_gb);
        m.cpu_line += buf;
    }
    std::snprintf(buf, sizeof buf, "%zu hilos", sets.size());
    m.threads_text = buf;
    std::vector<std::pair<uint32_t, uint32_t>> cores; // (group, core)
    for (const auto& r : sets) cores.push_back({r.group, r.core});
    std::sort(cores.begin(), cores.end());
    cores.erase(std::unique(cores.begin(), cores.end()), cores.end());
    for (size_t i = 0; i < sets.size(); ++i) {
        ThreadCell c;
        std::snprintf(buf, sizeof buf, "Hilo %zu", i + 1);
        c.label = buf;
        c.role = Contains(background, sets[i].id) ? ThreadRole::Background : (Contains(game, sets[i].id) ? ThreadRole::Game : ThreadRole::System);
        std::snprintf(buf, sizeof buf, "Núcleo %zu", static_cast<size_t>(std::lower_bound(cores.begin(), cores.end(), std::make_pair(sets[i].group, sets[i].core)) - cores.begin()) + 1);
        c.kind = buf;
        if (c.role == ThreadRole::Background) c.kind += " · fondo";
        m.threads.push_back(std::move(c));
    }
    if (background.empty()) m.cpu_note = "VegaFlow no ha separado núcleos para los programas de fondo en este equipo.";
    else if (restrict_game) m.cpu_note = "El juego se limita a los núcleos marcados como suyos, y los programas de fondo se envían a los núcleos marcados como de fondo.";
    else m.cpu_note = "El juego no se limita a ningún núcleo. Solo los programas de fondo se envían a los núcleos marcados, para que no le quiten tiempo.";
    return m;
}

} // namespace vegaflow::session
