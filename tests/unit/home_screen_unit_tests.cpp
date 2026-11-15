// Unit tests: the model of the "Inicio" screen (phase G): waiting vs game open, the numbers it states, and no promise of gain.

#include "session/home_screen.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <string>

namespace vftest {

namespace {

namespace hs = vegaflow::session;

hs::EquipmentScreenModel Equipment() {
    hs::EquipmentScreenModel e;
    e.cpu_line = "AMD Ryzen 5 4600H · 6 núcleos, 12 hilos";
    e.gpus.push_back({"AMD Radeon(TM) Graphics", "Integrada", ""});
    e.tier_labels = {"Básica", "Intermedia", "Avanzada", "Dedicada o externa"};
    e.tier = 2;
    return e;
}

bool HasFact(const hs::HomeScreenModel& m, const std::string& needle) {
    return std::any_of(m.game_facts.begin(), m.game_facts.end(), [&](const std::string& f) { return f.find(needle) != std::string::npos; });
}

} // namespace

void Test70() {
    vegaflow::app::AppConfig cfg;
    hs::SetCategoryAction(cfg, "recording_streaming", vegaflow::ProcessAction::DoNotTouch);
    hs::SetCategoryAction(cfg, "calls_voice", vegaflow::ProcessAction::DoNotTouch);
    hs::SetCategoryAction(cfg, "browsers", vegaflow::ProcessAction::Throttle);
    const auto programs = hs::BuildProgramsScreen(cfg);

    // 1. Waiting: says so, lists no game facts and promises nothing about the system beyond "no frena programas".
    hs::HomeInputs idle;
    idle.elevated = true;
    idle.daemon_ws_kb = 3112;
    const auto w = hs::BuildHomeScreen(idle, Equipment(), programs);
    RecordResult(!w.game_active && w.game_title == "En espera" && w.game_facts.empty() && !w.waiting_text.empty() && w.permission_ok &&
                     w.background_text.find("Ninguno") == 0,
                 "Test 70.1: with no game open the screen says it is waiting, lists no facts and no background programs");

    // 2. Memory of VegaFlow itself: KB to MB with one decimal and a decimal comma; nothing when unknown.
    hs::HomeInputs big = idle;
    big.daemon_ws_kb = 9626; // 9,40 MB
    hs::HomeInputs none = idle;
    none.daemon_ws_kb = 0;
    RecordResult(w.memory_text == "VegaFlow usa 3,0 MB de memoria" && hs::BuildHomeScreen(big, Equipment(), programs).memory_text == "VegaFlow usa 9,4 MB de memoria" &&
                     hs::BuildHomeScreen(none, Equipment(), programs).memory_text.empty(),
                 "Test 70.2: VegaFlow's own memory is shown in MB with one decimal, and omitted when unknown");

    // 3. A game is open: name, engine and API, which plane, and the three telemetry facts as the numbers say.
    hs::HomeInputs g = idle;
    g.game_active = true;
    g.game_name = "Octopath Traveler II";
    g.game_engine = "Unreal Engine 4/5";
    g.game_api = "DirectX 11";
    g.game_foreground = true;
    g.background_saving = 7;
    g.io_priority_high = true;
    g.direct_flip = false;
    const auto a = hs::BuildHomeScreen(g, Equipment(), programs);
    RecordResult(a.game_active && a.game_title == "Juego detectado" && a.game_name == "Octopath Traveler II" &&
                     a.game_sub == "Unreal Engine 4/5 · DirectX 11 · en primer plano" && HasFact(a, "modo ahorro: 7") && HasFact(a, "disco alta pedida a Windows para el juego: sí") &&
                     HasFact(a, "DirectFlip pedido a Windows para este juego: no") && a.background_text == "7 programas en modo ahorro",
                 "Test 70.3: an open game shows its name, engine, API, plane and the telemetry facts as measured");

    // 4. Missing engine/API leave no stray separators; an unnamed game gets a plain name.
    hs::HomeInputs u = g;
    u.game_engine.clear();
    u.game_api.clear();
    u.game_name.clear();
    u.game_foreground = false;
    u.background_saving = 1;
    const auto b = hs::BuildHomeScreen(u, Equipment(), programs);
    RecordResult(b.game_sub == "en segundo plano" && b.game_name == "Juego sin nombre" && b.background_text == "1 programa en modo ahorro",
                 "Test 70.4: unknown engine and API leave no separators, an unnamed game gets a plain name, and singular is respected");

    // 5. System memory: percent of RAM in use, no total means unknown (never a division), purge text tells both numbers.
    hs::HomeInputs v = idle;
    v.ram_total_mb = 16384;
    v.ram_available_mb = 4096; // 75 % in use
    v.guard_interventions = 2;
    v.purge_before_free_mb = 900;
    v.purge_after_free_mb = 2100;
    const auto m = hs::BuildHomeScreen(v, Equipment(), programs);
    const auto nb = hs::BuildHomeScreen(idle, Equipment(), programs); // total 0
    RecordResult(m.memory_percent == 75 && m.memory_detail == "4096 MB disponibles de 16384 MB" && m.guard_text.find(": 2") != std::string::npos &&
                     m.purge_text == "Última liberación: de 900 MB libres a 2100 MB" && nb.memory_percent == -1 && !nb.memory_detail.empty() &&
                     nb.purge_text.find("Aún no liberó") == 0,
                 "Test 70.5: the RAM share in use is computed from total and free, an unknown total is unknown, and the last purge states both numbers");

    // 5b. A dedicated graphics card has its own memory: the title says so and the shared-memory guardian is not claimed.
    auto dedicated = Equipment();
    dedicated.gpus[0].link = "Dedicada";
    hs::HomeInputs v_off = v;
    v_off.guard_active = false; // policy off, or safe mode: the daemon decides, the screen only states it
    const auto dm = hs::BuildHomeScreen(v_off, dedicated, programs);
    RecordResult(m.memory_title == "Memoria del equipo, compartida con la gráfica" && dm.memory_title == "Memoria del equipo" && dm.guard_text.find("apagado") != std::string::npos &&
                     dm.guard_text.find("0") == std::string::npos && m.guard_text.find(": 2") != std::string::npos,
                 "Test 70.5b: a dedicated card is not described as shared memory, and an off guardian shows no counter"); 

    // 5c. Safe mode (and --observe): nothing is detected or changed by VegaFlow itself, so the waiting text must not promise it.
    hs::HomeInputs safe;
    safe.automatic = false;
    const auto sm = hs::BuildHomeScreen(safe, Equipment(), programs);
    RecordResult(!sm.game_active && sm.game_name.find("Modo seguro") == 0 && sm.game_name.find("solo") == std::string::npos && sm.waiting_text.find("Modo seguro") == 0,
                 "Test 70.8: in safe mode the waiting card says VegaFlow changes nothing by itself and does not promise detection");

    // 6. The machine in three lines; the untouched kinds of program come from the user's choices.
    const bool eq_ok = w.equipment.size() == 3 && w.equipment[0].second.find("Ryzen") != std::string::npos && w.equipment[1].second == "AMD Radeon(TM) Graphics · Integrada" &&
                       w.equipment[2].second == "Intermedia";
    RecordResult(eq_ok && w.background_note == "No se tocan: Grabación y transmisión, Llamadas y voz.",
                 "Test 70.6: the machine shows processor, graphics and level, and the note lists the kinds the user set to No tocar");

    // 7. No text promises a gain.
    std::string all = w.waiting_text + a.game_sub + w.background_note + m.purge_text + m.guard_text + w.permission_text;
    for (const auto& f : a.game_facts) all += f;
    RecordResult(all.find("mejora") == std::string::npos && all.find("FPS") == std::string::npos && all.find("más rápido") == std::string::npos && all.find("Optimizando") == std::string::npos,
                 "Test 70.7: the Inicio texts promise no gain and do not claim 'optimizing'");
}

} // namespace vftest
