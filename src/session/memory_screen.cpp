#include "session/memory_screen.hpp"

#include <algorithm>
#include <cstdio>

namespace vegaflow::session {

namespace {

bool Contains(const std::vector<uint32_t>& v, uint32_t id) { return std::find(v.begin(), v.end(), id) != v.end(); }

std::string Fmt(const char* f, unsigned long long a) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, a);
    return buf;
}

} // namespace

MemoryScreenModel BuildMemoryScreen(const MemoryScreenInputs& in) {
    MemoryScreenModel m;
    m.has_cpu_data = !in.cpu_sets.empty();
    size_t game = 0, bg = 0, win = 0;
    for (size_t i = 0; i < in.cpu_sets.size(); ++i) {
        CoreCell c;
        c.label = "Hilo " + std::to_string(i + 1); // numbered from 1, as on Tu equipo
        const uint32_t id = in.cpu_sets[i].id;
        // Only the real CPU set id counts; a thread in both lists is background, as on Tu equipo.
        if (Contains(in.background_cpu_sets, id)) { c.role = CoreRole::Background; c.role_text = "FONDO"; ++bg; }
        else if (Contains(in.game_cpu_sets, id)) { c.role = CoreRole::Game; c.role_text = "JUEGO"; ++game; }
        else { c.role = CoreRole::Windows; c.role_text = "WINDOWS"; ++win; }
        m.cells.push_back(std::move(c));
    }
    if (!m.has_cpu_data) {
        m.cores_summary = "No se pudo leer la lista de hilos del procesador: VegaFlow no separa núcleos.";
    } else {
        m.cores_summary = std::to_string(m.cells.size()) + " hilos: " + std::to_string(game) + " del juego, " + std::to_string(bg) + " de fondo, " + std::to_string(win) + " de Windows";
    }
    m.background_line = "Procesos en segundo plano detectados: " + std::to_string(in.noisy_bg_processes_count) + "   |   Procesos aislados en núcleos secundarios: " +
                        std::to_string(in.isolated_bg_processes_count);

    m.has_memory_data = in.total_ram_mb > 0;
    if (m.has_memory_data) {
        const double total = static_cast<double>(in.total_ram_mb);
        // Parts are clipped one after the other so the four never add up to more than the whole.
        double left = 1.0;
        auto take = [&](uint64_t mb) {
            const double f = std::clamp(static_cast<double>(mb) / total, 0.0, left);
            left -= f;
            return f;
        };
        const double free_f = take(in.free_ram_mb);
        const double standby_f = take(in.standby_ram_mb);
        const double modified_f = take(in.modified_ram_mb);
        m.bar = {{BarPart::Used, std::max(0.0, left)}, {BarPart::Standby, standby_f}, {BarPart::Modified, modified_f}, {BarPart::Free, free_f}};
        m.ram_line = "Total: " + Fmt("%llu", in.total_ram_mb) + " MB   |   Libre: " + Fmt("%llu", in.free_ram_mb) + " MB   |   Standby: " + Fmt("%llu", in.standby_ram_mb) + " MB";
    } else {
        m.ram_line = "Sin dato de la memoria del equipo.";
    }

    const int pct = static_cast<int>(in.dxgi_pressure_threshold * 100.0 + 0.5);
    const bool over = in.dxgi_pressure_threshold > 0.0 && in.dxgi_pressure_ratio >= in.dxgi_pressure_threshold;
    m.pressure_alert = over || in.uma_guard_triggered_live;
    char buf[96];
    if (m.pressure_alert) std::snprintf(buf, sizeof buf, "ALERTA UMA: %.0f%% (>%d%%)", in.dxgi_pressure_ratio * 100.0, pct);
    else std::snprintf(buf, sizeof buf, "PRESIÓN DXGI: %.0f%% [OK]", in.dxgi_pressure_ratio * 100.0);
    m.pressure_badge = buf;
    return m;
}

int MoveCoreSelection(int index, CoreKey key, int count, int columns) {
    if (count <= 0) return 0;
    const int cols = std::max(columns, 1);
    int i = std::clamp(index, 0, count - 1);
    switch (key) {
    case CoreKey::Left: i = std::max(i - 1, 0); break;
    case CoreKey::Right: i = std::min(i + 1, count - 1); break;
    case CoreKey::Up: i = i - cols >= 0 ? i - cols : i; break;
    case CoreKey::Down: i = i + cols < count ? i + cols : (i / cols < (count - 1) / cols ? count - 1 : i); break; // short last row: land on its last cell
    case CoreKey::Home: i = 0; break;
    case CoreKey::End: i = count - 1; break;
    }
    return i;
}

std::string DescribeCell(const MemoryScreenModel& m, int index) {
    if (index < 0 || static_cast<size_t>(index) >= m.cells.size()) return {};
    const CoreCell& c = m.cells[static_cast<size_t>(index)];
    return c.label + ": " + c.role_text;
}

GridGeometry ComputeGrid(int count, int width, int cell_w, int gap_x, int cell_h, int gap_y) {
    GridGeometry g;
    g.columns = std::max(1, (width + gap_x) / std::max(cell_w + gap_x, 1));
    if (count <= 0) { g.rows = 0; g.height = 0; return g; }
    g.rows = (count + g.columns - 1) / g.columns;
    g.height = g.rows * cell_h + (g.rows - 1) * gap_y;
    return g;
}

int CoreAtPoint(int x, int y, int count, int columns, int cell_w, int gap_x, int cell_h, int gap_y) {
    const int cols = std::max(columns, 1);
    if (x < 0 || y < 0 || count <= 0 || cell_w <= 0 || cell_h <= 0) return -1;
    const int col = x / (cell_w + gap_x);
    const int row = y / (cell_h + gap_y);
    if (col >= cols || x % (cell_w + gap_x) >= cell_w || y % (cell_h + gap_y) >= cell_h) return -1;
    const int idx = row * cols + col;
    return idx < count ? idx : -1;
}

} // namespace vegaflow::session
