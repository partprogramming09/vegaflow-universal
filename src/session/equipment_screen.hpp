#pragma once
// Model of the "Tu equipo" screen: what the probe found about the CPU, the graphics adapter and the memory-protection
// level, and how the threads are shared while a game runs. Read-only: it only describes, it changes nothing.

#include <cstdint>
#include <string>
#include <vector>

#include "session/system_info.hpp"

namespace vegaflow::session {

// One logical processor as Windows lists it: its CPU set id (not 0..n-1) and the physical core it belongs to. Windows gives that as an index shared by the threads of one core (0,0,2,2,4,4 on a 6-core CPU with two threads each), not as 0..n-1;
// the model numbers the cores by the order of their distinct values.
struct CpuSetRef {
    uint32_t id = 0;
    uint32_t core = 0;
    uint32_t group = 0; // processor group: core indexes repeat from one group to the next
};

enum class ThreadRole { Game, Background, System };

struct ThreadCell {
    std::string label; // "Hilo 4"
    std::string kind;  // "Núcleo 2" or "Núcleo 6 · fondo"
    ThreadRole role = ThreadRole::System;
};

struct GpuRow {
    std::string name;
    std::string link; // "Integrada", "Dedicada"...
    std::string note;
};

struct EquipmentScreenModel {
    bool has_data = false;
    std::string cpu_line;
    std::string threads_text;
    std::vector<ThreadCell> threads;
    std::string cpu_note;
    std::vector<GpuRow> gpus;
    std::vector<std::string> tier_labels; // always four
    int tier = 0;                         // 1..4, 0 when unknown
    std::string tier_note;
};

// `sets` lists every logical processor in order; `game` and `background` are the CPU set ids the probe chose; `restrict_game` is the Configuración setting that limits the game to its own cores.
EquipmentScreenModel BuildEquipmentScreen(const SystemInfoSnapshot& snapshot, const std::vector<CpuSetRef>& sets, const std::vector<uint32_t>& game,
                                          const std::vector<uint32_t>& background, bool restrict_game);

} // namespace vegaflow::session
