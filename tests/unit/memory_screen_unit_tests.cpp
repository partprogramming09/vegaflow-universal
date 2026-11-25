// F4.2 unit tests: the model of the "Memoria UMA y núcleos" tab (roles of the threads by real CPU set id, honest numbers
// without data, the pressure alert) and its keyboard logic (arrow keys over the thread map, the sidebar order, the grid
// geometry at the narrowest window). No Windows calls: session/memory_screen and session/sidebar_nav are pure.

#include "session/memory_screen.hpp"
#include "session/sidebar_nav.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

namespace {

namespace ms = vegaflow::session;

// `n` logical processors in `groups` processor groups, CPU set ids starting at `first_id` (Windows starts at 256 on one group,
// but nothing may rely on it).
std::vector<ms::CpuSetRef> Sets(uint32_t n, uint32_t first_id, uint32_t groups = 1) {
    std::vector<ms::CpuSetRef> v;
    for (uint32_t i = 0; i < n; ++i) v.push_back({first_id + i, (i / 2) * 2, i * groups / n});
    return v;
}

ms::MemoryScreenInputs Base() {
    ms::MemoryScreenInputs in;
    in.total_ram_mb = 16384;
    in.free_ram_mb = 4096;
    in.standby_ram_mb = 4096;
    in.modified_ram_mb = 1024;
    in.dxgi_pressure_ratio = 0.40;
    in.dxgi_pressure_threshold = 0.85;
    in.cpu_sets = Sets(12, 256);
    return in;
}

} // namespace

void Test144() {
    // 1. Empty list: no cells, no invented 12 threads, an honest line, and the selection has nowhere to go.
    {
        ms::MemoryScreenInputs in = Base();
        in.cpu_sets.clear();
        const auto m = ms::BuildMemoryScreen(in);
        RecordResult(!m.has_cpu_data && m.cells.empty() && m.cores_summary.find("No se pudo leer") != std::string::npos &&
                         ms::MoveCoreSelection(0, ms::CoreKey::Right, 0, 4) == 0,
                     "Test 144.1: an empty CPU set list gives no cells and an honest text, never invented threads");
    }
    // 2. Roles come from the real ids: ids that do not start at 256 (and the old "id == position" shortcut) must not mislabel.
    {
        ms::MemoryScreenInputs in = Base();
        in.cpu_sets = Sets(6, 1000); // ids 1000..1005
        in.game_cpu_sets = {1000, 1001, 1002};
        in.background_cpu_sets = {1004, 1005};
        in.noisy_bg_processes_count = 7;
        in.isolated_bg_processes_count = 3;
        const auto m = ms::BuildMemoryScreen(in);
        bool ok = m.cells.size() == 6 && m.cells[0].role == ms::CoreRole::Game && m.cells[2].role == ms::CoreRole::Game && m.cells[3].role == ms::CoreRole::Windows &&
                  m.cells[4].role == ms::CoreRole::Background && m.cells[5].role == ms::CoreRole::Background;
        RecordResult(ok && m.has_cpu_data && m.cells[0].label == "Hilo #0" && m.cells[4].role_text == "FONDO" && m.cells[3].role_text == "WINDOWS" &&
                         m.cores_summary.find("6 hilos") != std::string::npos && m.cores_summary.find("3 del juego") != std::string::npos &&
                         m.cores_summary.find("2 de fondo") != std::string::npos && m.cores_summary.find("1 de Windows") != std::string::npos,
                     "Test 144.2: roles by real CPU set id (ids from 1000), and the summary counts each role");
        // Small numbers 0..n-1 in the lists must not be taken for positions when the real ids are 1000+.
        ms::MemoryScreenInputs lie = in;
        lie.game_cpu_sets = {0, 1};
        const auto lm = ms::BuildMemoryScreen(lie);
        RecordResult(lm.cells[0].role == ms::CoreRole::Windows && lm.cells[1].role == ms::CoreRole::Windows,
                     "Test 144.3: an id equal to a thread position does not mark that thread when real ids differ");
    }
    // 3. A thread in both lists is background (what the probe isolates is what matters), as on Tu equipo.
    {
        ms::MemoryScreenInputs in = Base();
        in.game_cpu_sets = {256, 257};
        in.background_cpu_sets = {257};
        const auto m = ms::BuildMemoryScreen(in);
        RecordResult(m.cells[0].role == ms::CoreRole::Game && m.cells[1].role == ms::CoreRole::Background, "Test 144.4: a thread in both lists counts as background");
    }
    // 4. Many threads across processor groups: one cell per set, grid columns bounded, selection reaches the last cell.
    {
        ms::MemoryScreenInputs in = Base();
        in.cpu_sets = Sets(128, 5000, 2);
        in.game_cpu_sets = {5000, 5127};
        const auto m = ms::BuildMemoryScreen(in);
        RecordResult(m.cells.size() == 128 && m.cells[127].role == ms::CoreRole::Game && m.cells[127].label == "Hilo #127",
                     "Test 144.5: 128 threads in two groups give 128 cells with the last one marked");
        const ms::GridGeometry g = ms::ComputeGrid(128, 960 - 250 - 32 - 32, 86, 10, 46, 8);
        const int right = (g.columns - 1) * (86 + 10) + 86;
        RecordResult(g.columns >= 1 && right <= 960 - 250 - 64 - 32 && g.rows * g.columns >= 128 && (g.rows - 1) * g.columns < 128,
                     "Test 144.6: at the narrowest window 128 cells wrap inside the width (no cell past the right edge)",
                     std::to_string(g.columns) + " columns x " + std::to_string(g.rows) + " rows");
        const ms::GridGeometry z = ms::ComputeGrid(0, 600, 86, 10, 46, 8);
        const ms::GridGeometry tiny = ms::ComputeGrid(5, 10, 86, 10, 46, 8); // narrower than one cell: still one column
        RecordResult(z.columns >= 1 && z.rows == 0 && z.height == 0 && tiny.columns == 1 && tiny.rows == 5,
                     "Test 144.7: zero cells give height 0; a width narrower than one cell still gives one column");
    }
    // 5. Memory: total 0 means no data (no division by zero, no invented 16 GB); fractions never exceed 1 together.
    {
        ms::MemoryScreenInputs in = Base();
        in.total_ram_mb = 0;
        auto m = ms::BuildMemoryScreen(in);
        RecordResult(!m.has_memory_data && m.bar.empty() && m.ram_line.find("Sin dato") != std::string::npos, "Test 144.8: total RAM 0 gives no bar and an honest line");
        in = Base();
        in.free_ram_mb = 20000;
        in.standby_ram_mb = 20000;
        in.modified_ram_mb = 20000; // nonsense: more than the total
        m = ms::BuildMemoryScreen(in);
        double sum = 0.0;
        bool in_range = true;
        for (const auto& s : m.bar) {
            sum += s.fraction;
            in_range = in_range && s.fraction >= 0.0 && s.fraction <= 1.0;
        }
        RecordResult(m.has_memory_data && in_range && sum <= 1.0 + 1e-9, "Test 144.9: parts larger than the total are clipped so the bar never overflows");
        in = Base();
        const auto ok = ms::BuildMemoryScreen(in);
        double s2 = 0.0;
        for (const auto& s : ok.bar) s2 += s.fraction;
        RecordResult(ok.bar.size() == 4 && s2 > 0.999 && s2 < 1.001 && ok.bar[0].kind == ms::BarPart::Used && ok.bar[3].kind == ms::BarPart::Free,
                     "Test 144.10: normal numbers fill the bar exactly (used, standby, modified, free)");
    }
    // 6. Pressure alert: threshold 0 never alerts; at or above the threshold it does; the live guard flag alerts alone.
    {
        ms::MemoryScreenInputs in = Base();
        in.dxgi_pressure_threshold = 0.0;
        in.dxgi_pressure_ratio = 0.99;
        const bool none = !ms::BuildMemoryScreen(in).pressure_alert;
        in = Base();
        in.dxgi_pressure_ratio = 0.85;
        const auto at = ms::BuildMemoryScreen(in);
        in.dxgi_pressure_ratio = 0.84;
        const bool below = !ms::BuildMemoryScreen(in).pressure_alert;
        in.uma_guard_triggered_live = true;
        const bool live = ms::BuildMemoryScreen(in).pressure_alert;
        RecordResult(none && at.pressure_alert && below && live && at.pressure_badge.find("ALERTA") != std::string::npos &&
                         at.pressure_badge.find("85%") != std::string::npos,
                     "Test 144.11: threshold 0 never alerts, the threshold itself alerts, below it does not, the live guard flag alerts alone");
    }
    // 7. Arrow keys over the thread map: clamp at the ends, move by rows, Home/End, no wrap into nowhere.
    {
        const int n = 12, cols = 5;
        const bool a = ms::MoveCoreSelection(0, ms::CoreKey::Left, n, cols) == 0 && ms::MoveCoreSelection(0, ms::CoreKey::Up, n, cols) == 0;
        const bool b = ms::MoveCoreSelection(0, ms::CoreKey::Right, n, cols) == 1 && ms::MoveCoreSelection(0, ms::CoreKey::Down, n, cols) == 5;
        const bool c = ms::MoveCoreSelection(11, ms::CoreKey::Right, n, cols) == 11 && ms::MoveCoreSelection(8, ms::CoreKey::Down, n, cols) == 11; // short last row
        const bool d = ms::MoveCoreSelection(7, ms::CoreKey::Home, n, cols) == 0 && ms::MoveCoreSelection(3, ms::CoreKey::End, n, cols) == 11;
        const bool e = ms::MoveCoreSelection(99, ms::CoreKey::Left, n, cols) == 10; // a stale index is clamped first
        const bool f = ms::MoveCoreSelection(0, ms::CoreKey::Down, 1, 5) == 0 && ms::MoveCoreSelection(3, ms::CoreKey::Right, 12, 0) == 4; // zero columns count as one
        RecordResult(a && b && c && d && e && f, "Test 144.12: arrows move by cell and row, clamp at the ends, Home/End jump, a stale index is clamped");
    }
    // 8. The selected cell is announced in words (role in text, not colour) and an out-of-range one says nothing.
    {
        ms::MemoryScreenInputs in = Base();
        in.game_cpu_sets = {256};
        const auto m = ms::BuildMemoryScreen(in);
        const std::string s = ms::DescribeCell(m, 0);
        RecordResult(s.find("Hilo #0") != std::string::npos && s.find("JUEGO") != std::string::npos && ms::DescribeCell(m, 12).empty() && ms::DescribeCell(m, -1).empty(),
                     "Test 144.13: the selected thread is described with its role as text; an index out of range gives nothing");
    }
    // 9. Sidebar: the active entry follows the tab (Requisitos belongs to Configuración), arrows wrap, digits map to tabs.
    {
        using ms::SidebarKey;
        bool ok = ms::SidebarEntryOfTab(8) == 5 && ms::SidebarEntryOfTab(2) == 2 && ms::SidebarEntryOfTab(-1) == 0 && ms::SidebarEntryOfTab(99) == 0;
        ok = ok && ms::MoveSidebarFocus(0, SidebarKey::Up) == 7 && ms::MoveSidebarFocus(7, SidebarKey::Down) == 0 && ms::MoveSidebarFocus(3, SidebarKey::Down) == 4 &&
             ms::MoveSidebarFocus(3, SidebarKey::Home) == 0 && ms::MoveSidebarFocus(3, SidebarKey::End) == 7 && ms::MoveSidebarFocus(40, SidebarKey::Down) == 0;
        ok = ok && ms::TabOfDigit('1') == 0 && ms::TabOfDigit('8') == 7 && ms::TabOfDigit('9') == -1 && ms::TabOfDigit('0') == -1 && ms::kSidebarEntries == 8;
        RecordResult(ok, "Test 144.14: sidebar active entry, wrapping arrows, Home/End and Ctrl+digit mapping");
    }
}

} // namespace vftest
