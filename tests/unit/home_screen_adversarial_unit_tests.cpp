// Adversarial unit tests: the "Inicio" model with hostile telemetry: RAM available above the total, totals of zero and
// of the maximum, empty equipment, impossible tiers, every flag combination, and no promise of gain in any text.

#include "session/home_screen.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vftest {

namespace {

namespace hs = vegaflow::session;

void Attack(const std::string& name, const std::function<std::string()>& body) {
    try {
        const std::string why = body();
        RecordResult(why.empty(), name, why);
    } catch (const std::exception& e) {
        RecordResult(false, name, std::string("threw: ") + e.what());
    } catch (...) {
        RecordResult(false, name, "threw an unknown exception");
    }
}

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool Promises(const std::string& text) {
    const std::string t = LowerAscii(text);
    return t.find("fps") != std::string::npos || t.find("mejora") != std::string::npos || t.find("más rápido") != std::string::npos ||
           t.find("mas rapido") != std::string::npos || t.find("rápid") != std::string::npos;
}

hs::EquipmentScreenModel Equipment() {
    hs::EquipmentScreenModel e;
    e.cpu_line = "AMD Ryzen 5 4600H · 6 núcleos, 12 hilos";
    e.gpus.push_back({"AMD Radeon(TM) Graphics", "Integrada", ""});
    e.tier_labels = {"Básica", "Intermedia", "Avanzada", "Dedicada o externa"};
    e.tier = 2;
    return e;
}

std::string AllText(const hs::HomeScreenModel& m) {
    std::string s = m.permission_text + "|" + m.memory_text + "|" + m.game_title + "|" + m.game_name + "|" + m.game_sub + "|" + m.waiting_text + "|" +
                    m.memory_title + "|" + m.memory_detail + "|" + m.guard_text + "|" + m.purge_text + "|" + m.background_text + "|" + m.background_note;
    for (const auto& f : m.game_facts) s += "|" + f;
    for (const auto& [k, v] : m.equipment) s += "|" + k + "|" + v;
    return s;
}

} // namespace

void Test77() {
    // Test 77: attacks on the Inicio model
    vegaflow::app::AppConfig cfg;
    const auto programs = hs::BuildProgramsScreen(cfg);

    Attack("Test 77.1: RAM available above the total is clamped: 0 % in use, never more free than the total", [&]() -> std::string {
        hs::HomeInputs in;
        in.ram_total_mb = 8000;
        in.ram_available_mb = 9000;
        const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
        if (m.memory_percent != 0) return "percent " + std::to_string(m.memory_percent);
        if (m.memory_detail != "8000 MB disponibles de 8000 MB") return "detail '" + m.memory_detail + "'";
        in.ram_available_mb = UINT64_MAX;
        const auto big = hs::BuildHomeScreen(in, Equipment(), programs);
        if (big.memory_percent != 0 || big.memory_detail != "8000 MB disponibles de 8000 MB") return "UINT64_MAX available: " + big.memory_detail;
        return "";
    });

    Attack("Test 77.2: a total of 0 gives percent -1 and a sentence, whatever the available value says", [&]() -> std::string {
        for (const uint64_t avail : {uint64_t{0}, uint64_t{1}, uint64_t{4096}, UINT64_MAX}) {
            hs::HomeInputs in;
            in.ram_total_mb = 0;
            in.ram_available_mb = avail;
            const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
            if (m.memory_percent != -1) return "percent " + std::to_string(m.memory_percent) + " for available " + std::to_string(avail);
            if (m.memory_detail.empty()) return "empty detail";
            if (m.memory_detail.find("MB disponibles") != std::string::npos) return "claims numbers with a total of 0: " + m.memory_detail;
        }
        return "";
    });

    Attack("Test 77.3: percent is exact at the borders (nothing free = 100, all free = 0, total 1) and rounds to nearest", [&]() -> std::string {
        const auto pct = [&](uint64_t total, uint64_t avail) {
            hs::HomeInputs in;
            in.ram_total_mb = total;
            in.ram_available_mb = avail;
            return hs::BuildHomeScreen(in, Equipment(), programs).memory_percent;
        };
        if (pct(1, 0) != 100) return "1/0";
        if (pct(1, 1) != 0) return "1/1";
        if (pct(8000, 0) != 100) return "8000/0";
        if (pct(8000, 8000) != 0) return "8000/8000";
        if (pct(3, 1) != 67) return "total 3, free 1 -> " + std::to_string(pct(3, 1));
        if (pct(200, 1) != 100 && pct(200, 1) != 99) return "200/1";
        if (pct(2, 1) != 50) return "2/1";
        return "";
    });

    Attack("Test 77.4: property: for realistic totals (up to 16 TB) the percent is always inside 0..100 and matches rounding", [&]() -> std::string {
        uint64_t x = 88172645463325252ULL; // fixed seed, xorshift
        const auto next = [&]() {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            return x;
        };
        for (int i = 0; i < 50000; ++i) {
            const uint64_t total = 1 + next() % 16'000'000ULL;
            const uint64_t avail = (i % 7 == 0) ? total + next() % 1000 : next() % (total + 1);
            hs::HomeInputs in;
            in.ram_total_mb = total;
            in.ram_available_mb = avail;
            const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
            const uint64_t free_mb = std::min(avail, total);
            const unsigned __int128 exact = ((static_cast<unsigned __int128>(total - free_mb) * 100) + total / 2) / total;
            if (m.memory_percent < 0 || m.memory_percent > 100 || static_cast<unsigned __int128>(m.memory_percent) != exact) {
                return "total=" + std::to_string(total) + " available=" + std::to_string(avail) + " percent=" + std::to_string(m.memory_percent);
            }
        }
        return "";
    });

    Attack("Test 77.5: a total so large that used*100 overflows 64 bits still gives a percent inside 0..100", [&]() -> std::string {
        hs::HomeInputs in;
        in.ram_total_mb = UINT64_MAX / 50; // 3.7e17 MB: not a real machine, only a hostile value
        in.ram_available_mb = 0;
        const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
        if (m.memory_percent < 0 || m.memory_percent > 100) return "total=" + std::to_string(in.ram_total_mb) + " available=0 -> percent " + std::to_string(m.memory_percent);
        if (m.memory_percent != 100) return "total=" + std::to_string(in.ram_total_mb) + " available=0 -> percent " + std::to_string(m.memory_percent) + " (expected 100)";
        return "";
    });

    Attack("Test 77.6: the daemon's own memory is exact: KB to MB with one decimal, comma, rounded to nearest, for realistic values", [&]() -> std::string {
        const std::vector<std::pair<uint64_t, std::string>> cases = {
            {1, "0,0"}, {51, "0,0"}, {52, "0,1"}, {512, "0,5"}, {1023, "1,0"}, {1024, "1,0"}, {1536, "1,5"}, {3112, "3,0"}, {15 * 1024, "15,0"},
            {15 * 1024 + 1, "15,0"}, {10240000, "10000,0"}};
        for (const auto& [kb, text] : cases) {
            hs::HomeInputs in;
            in.daemon_ws_kb = kb;
            const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
            if (m.memory_text != "VegaFlow usa " + text + " MB de memoria") return std::to_string(kb) + " KB -> '" + m.memory_text + "'";
        }
        return "";
    });

    Attack("Test 77.7: a working set so large that kb*10 overflows 64 bits does not print a smaller number than a smaller one", [&]() -> std::string {
        hs::HomeInputs small;
        small.daemon_ws_kb = 1ULL << 62;
        hs::HomeInputs huge;
        huge.daemon_ws_kb = UINT64_MAX;
        const std::string a = hs::BuildHomeScreen(small, Equipment(), programs).memory_text;
        const std::string b = hs::BuildHomeScreen(huge, Equipment(), programs).memory_text;
        const unsigned __int128 want = (static_cast<unsigned __int128>(UINT64_MAX) * 10 + 512) / 1024;
        const std::string expect_b = "VegaFlow usa " + std::to_string(static_cast<uint64_t>(want / 10)) + "," + std::to_string(static_cast<unsigned>(want % 10)) + " MB de memoria";
        if (b != expect_b) return "ws_kb=UINT64_MAX -> '" + b + "', expected '" + expect_b + "' (a=" + a + ")";
        return "";
    });

    Attack("Test 77.8: empty and impossible equipment (no GPU, tier 0, -3, 5, 99, labels not four) never throws and omits instead of guessing", [&]() -> std::string {
        for (const int tier : {0, -3, 5, 99, 1, 4}) {
            for (const size_t labels : {size_t{0}, size_t{3}, size_t{4}, size_t{5}}) {
                hs::EquipmentScreenModel e;
                e.cpu_line = "CPU";
                e.tier = tier;
                e.tier_labels.assign(labels, "etiqueta");
                const auto m = hs::BuildHomeScreen({}, e, programs);
                const bool has_tier = std::any_of(m.equipment.begin(), m.equipment.end(), [](const auto& kv) { return kv.first == "Nivel de tu equipo"; });
                const bool should = tier >= 1 && tier <= 4 && labels == 4;
                if (has_tier != should) return "tier " + std::to_string(tier) + ", labels " + std::to_string(labels) + ": tier row present=" + (has_tier ? "yes" : "no");
                if (m.memory_title.empty()) return "empty memory_title with no GPU";
            }
        }
        return "";
    });

    Attack("Test 77.9: a GPU with a discrete link is not called shared memory; an integrated one is", [&]() -> std::string {
        hs::EquipmentScreenModel e = Equipment();
        e.gpus[0].link = "Dedicada";
        if (hs::BuildHomeScreen({}, e, programs).memory_title.find("compartida") != std::string::npos) return "dedicated GPU called shared";
        e.gpus.clear();
        if (hs::BuildHomeScreen({}, e, programs).memory_title.find("compartida") != std::string::npos) return "no GPU called shared";
        if (hs::BuildHomeScreen({}, Equipment(), programs).memory_title.find("compartida") == std::string::npos) return "integrated GPU not called shared";
        return "";
    });

    Attack("Test 77.10: every text the screen needs is non-empty in all 2^8 flag combinations, and none promises a gain", [&]() -> std::string {
        for (int bits = 0; bits < 256; ++bits) {
            hs::HomeInputs in;
            in.elevated = bits & 1;
            in.game_active = bits & 2;
            in.game_foreground = bits & 4;
            in.io_priority_high = bits & 8;
            in.direct_flip = bits & 16;
            in.automatic = bits & 32;
            in.guard_active = bits & 64;
            if (bits & 128) {
                in.game_name = "Juego";
                in.game_engine = "Unity";
                in.game_api = "DirectX 11";
            }
            const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
            const std::string tag = "bits=" + std::to_string(bits);
            if (m.permission_text.empty()) return "permission_text empty " + tag;
            if (m.game_title.empty() || m.game_name.empty()) return "game title/name empty " + tag;
            if (m.game_active && (m.game_sub.empty() || m.game_facts.empty())) return "active game without sub or facts " + tag;
            if (!m.game_active && m.waiting_text.empty()) return "waiting without text " + tag;
            if (!m.game_active && !m.game_facts.empty()) return "facts while waiting " + tag;
            if (m.memory_title.empty() || m.memory_detail.empty() || m.guard_text.empty() || m.purge_text.empty() || m.background_text.empty()) return "empty text " + tag;
            if (m.game_sub.rfind(" · ", 0) == 0 || (m.game_sub.size() >= 3 && m.game_sub.compare(m.game_sub.size() - 3, 3, " · ") == 0) || m.game_sub.find(" ·  · ") != std::string::npos) {
                return "dangling separator in '" + m.game_sub + "' " + tag;
            }
            if (Promises(AllText(m))) return "promise in text " + tag + ": " + AllText(m);
        }
        return "";
    });

    Attack("Test 77.11: engine and API combinations make a clean subtitle (no leading, trailing or doubled separator)", [&]() -> std::string {
        for (const std::string& eng : {std::string(""), std::string("Unreal Engine 4/5")}) {
            for (const std::string& api : {std::string(""), std::string("Vulkan")}) {
                for (const bool fg : {true, false}) {
                    hs::HomeInputs in;
                    in.game_active = true;
                    in.game_engine = eng;
                    in.game_api = api;
                    in.game_foreground = fg;
                    const std::string sub = hs::BuildHomeScreen(in, Equipment(), programs).game_sub;
                    std::string want;
                    if (!eng.empty()) want += eng + " · ";
                    if (!api.empty()) want += api + " · ";
                    want += fg ? "en primer plano" : "en segundo plano";
                    if (sub != want) return "got '" + sub + "', wanted '" + want + "'";
                }
            }
        }
        return "";
    });

    Attack("Test 77.12: a nameless detected game still gets a visible name", [&]() -> std::string {
        hs::HomeInputs in;
        in.game_active = true;
        in.game_name = "";
        const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
        if (m.game_name.empty()) return "empty game_name";
        return "";
    });

    Attack("Test 77.13: the equipment rows never carry an empty value (a blank line on screen)", [&]() -> std::string {
        hs::EquipmentScreenModel e = Equipment();
        e.cpu_line = ""; // the probe found nothing
        const auto m = hs::BuildHomeScreen({}, e, programs);
        for (const auto& [label, value] : m.equipment) {
            if (value.empty()) return "row '" + label + "' has an empty value";
        }
        return "";
    });

    Attack("Test 77.17: a GPU with no name does not give a row that starts with a separator", [&]() -> std::string {
        hs::EquipmentScreenModel e = Equipment();
        e.gpus[0].name = "";
        const auto m = hs::BuildHomeScreen({}, e, programs);
        for (const auto& [label, value] : m.equipment) {
            if (value.rfind(" · ", 0) == 0) return "row '" + label + "' starts with a separator: '" + value + "'";
        }
        return "";
    });

    Attack("Test 77.14: counters at their maximum print as numbers; singular only for exactly 1", [&]() -> std::string {
        hs::HomeInputs in;
        in.game_active = true;
        in.background_saving = UINT32_MAX;
        in.guard_interventions = UINT32_MAX;
        in.purge_before_free_mb = UINT64_MAX;
        in.purge_after_free_mb = UINT64_MAX;
        const auto m = hs::BuildHomeScreen(in, Equipment(), programs);
        if (m.background_text.find("4294967295 programas") == std::string::npos) return m.background_text;
        if (m.guard_text.find("4294967295") == std::string::npos) return m.guard_text;
        if (m.purge_text.find("18446744073709551615") == std::string::npos) return m.purge_text;
        in.background_saving = 1;
        if (hs::BuildHomeScreen(in, Equipment(), programs).background_text != "1 programa en modo ahorro") return "singular";
        in.background_saving = 0;
        if (hs::BuildHomeScreen(in, Equipment(), programs).background_text != "0 programas en modo ahorro") return "zero";
        return "";
    });

    Attack("Test 77.15: the 'not touched' note lists exactly the categories set to DoNotTouch, and is empty when there are none", [&]() -> std::string {
        vegaflow::app::AppConfig c;
        for (const char* id : {"recording_streaming", "calls_voice", "messaging", "browsers", "stores_launchers", "music", "cloud_sync"}) {
            hs::SetCategoryAction(c, id, vegaflow::ProcessAction::Throttle);
        }
        if (!hs::BuildHomeScreen({}, Equipment(), hs::BuildProgramsScreen(c)).background_note.empty()) return "note not empty with every category throttled";
        for (const char* id : {"recording_streaming", "calls_voice", "messaging", "browsers", "stores_launchers", "music", "cloud_sync"}) {
            hs::SetCategoryAction(c, id, vegaflow::ProcessAction::DoNotTouch);
        }
        const auto progs = hs::BuildProgramsScreen(c);
        const std::string note = hs::BuildHomeScreen({}, Equipment(), progs).background_note;
        for (const auto& cat : progs.categories) {
            if (note.find(cat.title) == std::string::npos) return "missing '" + cat.title + "' in '" + note + "'";
        }
        if (hs::BuildHomeScreen({}, Equipment(), hs::ProgramsScreenModel{}).background_note != "") return "note with no categories";
        return "";
    });

    Attack("Test 77.16: the model is pure: the same inputs twice give the same texts", [&]() -> std::string {
        hs::HomeInputs in;
        in.game_active = true;
        in.game_name = "X";
        in.ram_total_mb = 16000;
        in.ram_available_mb = 3000;
        in.daemon_ws_kb = 9999;
        if (AllText(hs::BuildHomeScreen(in, Equipment(), programs)) != AllText(hs::BuildHomeScreen(in, Equipment(), programs))) return "differs";
        return "";
    });
}

} // namespace vftest
