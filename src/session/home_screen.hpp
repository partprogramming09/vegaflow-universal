#pragma once
// Model of the "Inicio" screen: is a game open and what VegaFlow reports about it, the machine in three lines, the shared
// graphics memory, and the background programs. It only states what the telemetry says; it decides nothing.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "session/equipment_screen.hpp"
#include "session/programs_screen.hpp"

namespace vegaflow::session {

struct HomeInputs {
    bool elevated = false;
    uint64_t daemon_ws_kb = 0; // working set of the VegaFlow process
    bool game_active = false;
    std::string game_name;
    std::string game_engine; // "" when unknown
    std::string game_api;    // "" when unknown
    bool game_foreground = false;
    uint32_t background_saving = 0; // background programs in efficiency mode right now
    bool io_priority_high = false; // asked of Windows when the game opens; the result is not read back
    bool direct_flip = false;     // asked of Windows for this game (registry); Windows may still not use it
    bool automatic = true;        // false in safe mode and --observe: VegaFlow detects and changes nothing by itself
    bool guard_active = true;     // the shared-memory guardian runs: policy on and not safe mode
    // System RAM, not the graphics counter: DXGI's CurrentUsage is the usage of this process alone (Microsoft Learn,
    // DXGI_QUERY_VIDEO_MEMORY_INFO), so it says nothing about what a game uses.
    uint64_t ram_total_mb = 0;
    uint64_t ram_available_mb = 0; // free plus standby
    uint32_t guard_interventions = 0;
    uint64_t purge_before_free_mb = 0;
    uint64_t purge_after_free_mb = 0;
};

struct HomeScreenModel {
    std::string permission_text;
    bool permission_ok = false;
    std::string memory_text; // "VegaFlow usa 3,0 MB de memoria"
    bool game_active = false;
    std::string game_title; // "Juego detectado" or "En espera"
    std::string game_name;
    std::string game_sub;
    std::vector<std::string> game_facts;
    std::string waiting_text;
    std::vector<std::pair<std::string, std::string>> equipment; // label, value
    std::string memory_title;
    int memory_percent = -1;                                    // share of RAM in use; -1 when the total is unknown
    std::string memory_detail;
    std::string guard_text;
    std::string purge_text;
    std::string background_text;
    std::string background_note; // which kinds of program are never touched, or ""
};

HomeScreenModel BuildHomeScreen(const HomeInputs& in, const EquipmentScreenModel& equipment, const ProgramsScreenModel& programs);

} // namespace vegaflow::session
