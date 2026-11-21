// Unit tests: the model of the "Tu equipo" screen (phase G): how the threads are shared while a game runs, the graphics
// adapter and the memory-protection level, all read from what the probe found. No gain is promised anywhere.

#include "session/equipment_screen.hpp"
#include "../test_support.hpp"

#include <string>
#include <vector>

namespace vftest {

namespace {

namespace es = vegaflow::session;

vegaflow::SystemInfoSnapshot Ryzen() {
    vegaflow::SystemInfoSnapshot s;
    s.cpu.brand_string = "AMD Ryzen 5 4600H with Radeon Graphics";
    s.cpu.physical_cores = 6;
    s.cpu.logical_processors = 12;
    s.gpu.adapter_name = "AMD Radeon(TM) Graphics";
    s.gpu.connection = "Integrada";
    s.ram.total_physical_gb = 16.0;
    s.apu_guard.tier_name = "Tier2_MidVegaUMA";
    return s;
}

// Windows numbers CPU sets from 256, not from 0, and gives the core as the index of its first thread (0,0,2,2,4,4...).
std::vector<es::CpuSetRef> Sets(uint32_t n, bool smt) {
    std::vector<es::CpuSetRef> v;
    for (uint32_t i = 0; i < n; ++i) v.push_back({256 + i, smt ? (i / 2) * 2 : i, 0});
    return v;
}

} // namespace

void Test67() {
    const std::vector<uint32_t> game = {256, 257, 258, 259, 260, 261, 262, 263, 264, 265};
    const std::vector<uint32_t> background = {266, 267};
    const auto sets = Sets(12, true);

    // 1. The thread map: one cell per logical processor, named by core (two threads per core), roles from the probe.
    const auto m = es::BuildEquipmentScreen(Ryzen(), sets, game, background, false);
    bool roles_ok = m.threads.size() == 12;
    for (size_t i = 0; roles_ok && i < 12; ++i) roles_ok = m.threads[i].role == (i >= 10 ? es::ThreadRole::Background : es::ThreadRole::Game);
    RecordResult(m.has_data && roles_ok && m.threads_text == "12 hilos" && m.threads[0].kind == "Núcleo 1" && m.threads[11].kind == "Núcleo 6 · fondo" &&
                     m.threads[3].label == "Hilo 4" && m.cpu_line.find("Ryzen 5 4600H") != std::string::npos && m.cpu_line.find("6 núcleos, 12 hilos") != std::string::npos &&
                     m.cpu_line.find("16 GB") != std::string::npos,
                 "Test 67.1: a 6-core 12-thread CPU shows 12 cells named by core, the last core marked as background, and the CPU line");

    // 2. Threads in neither list belong to Windows; a CPU without hyper-threading counts cores one to one.
    vegaflow::SystemInfoSnapshot plain = Ryzen();
    plain.cpu.physical_cores = 4;
    plain.cpu.logical_processors = 4;
    const auto pm = es::BuildEquipmentScreen(plain, Sets(4, false), {256, 257}, {259}, false);
    RecordResult(pm.threads.size() == 4 && pm.threads[2].role == es::ThreadRole::System && pm.threads[3].role == es::ThreadRole::Background &&
                     pm.threads[3].kind == "Núcleo 4 · fondo" && pm.threads_text == "4 hilos",
                 "Test 67.2: a thread in no list is left to Windows, and without hyper-threading each core has one thread");

    // 3. No data from the probe: no cells and an honest text, never invented cores.
    const auto empty = es::BuildEquipmentScreen(vegaflow::SystemInfoSnapshot{}, {}, {}, {}, false);
    RecordResult(!empty.has_data && empty.threads.empty() && !empty.cpu_note.empty() && empty.tier == 0 && !empty.tier_note.empty(),
                 "Test 67.3: without probe data there are no thread cells, the notes say so and the tier is 0");

    // 4. The protection level comes from the probe's tier name.
    auto tier_of = [&](const char* name) {
        vegaflow::SystemInfoSnapshot s = Ryzen();
        s.apu_guard.tier_name = name;
        return es::BuildEquipmentScreen(s, sets, game, background, false).tier;
    };
    const auto labels = es::BuildEquipmentScreen(Ryzen(), sets, game, background, false).tier_labels;
    RecordResult(tier_of("Tier1_EntryUMA") == 1 && tier_of("Tier2_MidVegaUMA") == 2 && tier_of("Tier3_AdvancedUMA") == 3 &&
                     tier_of("Tier4_DiscreteOrCustom") == 4 && tier_of("Unknown") == 0 && tier_of("") == 0 && labels.size() == 4,
                 "Test 67.4: the protection level is read from the tier name; unknown or empty gives 0; four labels are always listed");

    // 5. The graphics row follows the connection, and says what VegaFlow does with it.
    auto gpu_note = [&](const char* connection) {
        vegaflow::SystemInfoSnapshot s = Ryzen();
        s.gpu.connection = connection;
        const auto r = es::BuildEquipmentScreen(s, sets, game, background, false);
        return r.gpus.size() == 1 ? r.gpus[0].note : std::string();
    };
    const std::string integrated = gpu_note("Integrada");
    const std::string dedicated = gpu_note("Dedicada");
    const std::string unreadable = gpu_note("No se pudo leer");
    RecordResult(integrated.find("RAM del sistema") != std::string::npos && dedicated.find("memoria propia") != std::string::npos &&
                     !unreadable.empty() && unreadable != integrated && unreadable != dedicated,
                 "Test 67.5: integrated, dedicated and unreadable graphics each get their own note");

    // 6. The CPU note follows the setting that limits the game's cores.
    const auto free_game = es::BuildEquipmentScreen(Ryzen(), sets, game, background, false);
    const auto limited = es::BuildEquipmentScreen(Ryzen(), sets, game, background, true);
    RecordResult(free_game.cpu_note != limited.cpu_note && free_game.cpu_note.find("no se limita") != std::string::npos &&
                     limited.cpu_note.find("se limita") != std::string::npos,
                 "Test 67.6: the CPU note says whether the game is limited to some cores, following the Configuración setting");

    // 7. Cores repeat their index in each processor group: two groups of four threads are four cores, not two, and a hybrid
    //    CPU with single-thread cores numbers one thread per core.
    std::vector<es::CpuSetRef> two_groups;
    for (uint32_t g = 0; g < 2; ++g) {
        for (uint32_t i = 0; i < 4; ++i) two_groups.push_back({1000 + g * 100 + i, (i / 2) * 2, g});
    }
    vegaflow::SystemInfoSnapshot big = Ryzen();
    big.cpu.logical_processors = 8;
    const auto gm = es::BuildEquipmentScreen(big, two_groups, {}, {}, false);
    RecordResult(gm.threads.size() == 8 && gm.threads[0].kind == "Núcleo 1" && gm.threads[2].kind == "Núcleo 2" && gm.threads[4].kind == "Núcleo 3" &&
                     gm.threads[7].kind == "Núcleo 4",
                 "Test 67.7: the same core index in two processor groups gives different core numbers");
    vegaflow::SystemInfoSnapshot hybrid = Ryzen();
    hybrid.cpu.logical_processors = 6;
    const auto hm = es::BuildEquipmentScreen(hybrid, {{1, 0, 0}, {2, 2, 0}, {3, 4, 0}, {4, 5, 0}, {5, 6, 0}, {6, 7, 0}}, {1}, {5, 6}, false);
    RecordResult(hm.threads.size() == 6 && hm.threads[1].kind == "Núcleo 2" && hm.threads[2].kind == "Núcleo 3" && hm.threads[3].kind == "Núcleo 4" &&
                     hm.threads[4].role == es::ThreadRole::Background && hm.threads[0].role == es::ThreadRole::Game && hm.threads[3].role == es::ThreadRole::System,
                 "Test 67.8: a hybrid layout numbers one thread per core, and an id in no list is left to Windows");

    // 8. An id in both lists counts as background; ids that are in no set are ignored.
    const auto both = es::BuildEquipmentScreen(Ryzen(), sets, {256, 999}, {256, 888}, false);
    RecordResult(both.threads.size() == 12 && both.threads[0].role == es::ThreadRole::Background && both.threads[1].role == es::ThreadRole::System,
                 "Test 67.9: an id in both lists is background, and ids that match no processor change nothing");

    // 9. Every level and every graphics kind has its own note, and an unreadable graphics adapter never says "dedicated".
    std::string seen;
    bool tiers_distinct = true;
    for (const char* t : {"Unknown", "Tier1_EntryUMA", "Tier2_MidVegaUMA", "Tier3_AdvancedUMA", "Tier4_DiscreteOrCustom"}) {
        vegaflow::SystemInfoSnapshot s = Ryzen();
        s.apu_guard.tier_name = t;
        const std::string note = es::BuildEquipmentScreen(s, sets, game, background, false).tier_note;
        tiers_distinct = tiers_distinct && !note.empty() && seen.find("|" + note + "|") == std::string::npos;
        seen += "|" + note + "|";
    }
    const std::string external = gpu_note("Externa");
    RecordResult(tiers_distinct && external.find("eGPU") != std::string::npos && external != dedicated && labels[3] == "Dedicada o externa",
                 "Test 67.10: each level and the external graphics kind have their own text, and level 4 is labelled dedicated or external");

    // 10. No text on the screen promises a gain.
    std::string all = free_game.cpu_line + free_game.cpu_note + free_game.tier_note + integrated + dedicated + unreadable;
    for (const auto& l : free_game.tier_labels) all += l;
    RecordResult(all.find("mejora") == std::string::npos && all.find("FPS") == std::string::npos && all.find("más rápido") == std::string::npos,
                 "Test 67.11: the Tu equipo texts promise no gain (no FPS, no 'mejora', no 'más rápido')");
}

} // namespace vftest
