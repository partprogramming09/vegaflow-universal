#pragma once
// Model of the "Memoria UMA y núcleos" tab: how the logical processors are shared (by real CPU set id), the memory numbers
// with honest "no data" states, the pressure alert, and the keyboard logic over the thread map (no <windows.h>).

#include <cstdint>
#include <string>
#include <vector>

#include "session/equipment_screen.hpp" // CpuSetRef

namespace vegaflow::session {

// What the screen reads from the poll; ui/ fills it from its own data, so session/ never sees the GUI structs.
struct MemoryScreenInputs {
    std::vector<CpuSetRef> cpu_sets; // every logical processor in order, with its real CPU set id
    std::vector<uint32_t> game_cpu_sets;
    std::vector<uint32_t> background_cpu_sets;
    uint64_t total_ram_mb = 0;
    uint64_t free_ram_mb = 0;
    uint64_t standby_ram_mb = 0;
    uint64_t modified_ram_mb = 0;
    double dxgi_pressure_ratio = 0.0;
    double dxgi_pressure_threshold = 0.0; // 0 = the guard has no threshold: never an alert from the ratio
    bool uma_guard_triggered_live = false;
    uint32_t noisy_bg_processes_count = 0;
    uint32_t isolated_bg_processes_count = 0;
};

enum class CoreRole { Game, Background, Windows };

struct CoreCell {
    std::string label;     // "Hilo #3"
    std::string role_text; // "JUEGO", "FONDO", "WINDOWS": the role is never colour alone
    CoreRole role = CoreRole::Windows;
};

struct BarPart {
    enum Kind { Used, Standby, Modified, Free };
};

struct BarFraction {
    BarPart::Kind kind = BarPart::Used;
    double fraction = 0.0; // 0..1, the four add up to at most 1
};

struct MemoryScreenModel {
    bool has_cpu_data = false;
    std::vector<CoreCell> cells;
    std::string cores_summary; // "12 hilos: 10 del juego, 2 de fondo, 0 de Windows" or the honest no-data line
    std::string background_line;
    bool has_memory_data = false;
    std::string ram_line;
    std::vector<BarFraction> bar; // empty without data
    bool pressure_alert = false;
    std::string pressure_badge;
};

MemoryScreenModel BuildMemoryScreen(const MemoryScreenInputs& in);

// Keys of the thread map; the grid is `columns` wide and `count` cells long. A stale `index` is clamped first.
enum class CoreKey { Left, Right, Up, Down, Home, End };
int MoveCoreSelection(int index, CoreKey key, int count, int columns);

// "Hilo #3: JUEGO" for the selected cell; empty when the index is out of range.
std::string DescribeCell(const MemoryScreenModel& m, int index);

// Grid of equal cells wrapped to `width`: always at least one column.
struct GridGeometry {
    int columns = 1;
    int rows = 0;
    int height = 0; // rows of cells with their gaps, 0 without cells
};
GridGeometry ComputeGrid(int count, int width, int cell_w, int gap_x, int cell_h, int gap_y);

// The cell under a click at (x, y) inside the map, or -1 for a gap, a slot past the last cell, or a point outside the grid.
int CoreAtPoint(int x, int y, int count, int columns, int cell_w, int gap_x, int cell_h, int gap_y);

} // namespace vegaflow::session
