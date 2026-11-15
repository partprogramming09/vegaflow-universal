// Unit tests: the model of the "Ajustes del sistema" screen (phase G): real on/off state per setting, evidence
// labels and the experimental banner.

#include "session/settings_screen.hpp"
#include "../test_support.hpp"

#include <string>

namespace vftest {

namespace {

const vegaflow::SettingRow* FindRow(const vegaflow::SettingsScreenModel& m, const std::string& id) {
    for (const auto& f : m.families) {
        for (const auto& r : f.rows) {
            if (id == r.spec->id) return &r;
        }
    }
    return nullptr;
}

} // namespace

void Test63() {
    using namespace vegaflow;
    SettingsScreenInputs in;
    in.device = {GpuVendor::AMD, GpuConnection::Integrated, true};
    in.show_experimental = true;

    // 1. A factory machine shows every setting off and the banner on (experimental features are visible).
    const auto factory = BuildSettingsScreen(in);
    bool all_off = true;
    size_t rows = 0;
    for (const auto& f : factory.families) {
        for (const auto& r : f.rows) {
            ++rows;
            all_off = all_off && !r.on;
        }
    }
    RecordResult(all_off && rows > 0 && factory.experimental_notice,
                 "Test 63.1: a factory machine shows every setting off and the experimental banner");

    // 2. Each state field flips exactly its own row.
    struct Case {
        const char* id;
        void (*apply)(SettingsScreenInputs&);
    };
    const Case cases[] = {
        {"vram_reporting", [](SettingsScreenInputs& i) { i.tweaks.amd_vram_override_active = true; }},
        {"amd_driver_profile", [](SettingsScreenInputs& i) { i.tweaks.amd_shader_cache_forced = true; }},
        {"priority_separation", [](SettingsScreenInputs& i) { i.tweaks.win32_priority_separation = 0x28; }},
        {"paging_shield", [](SettingsScreenInputs& i) { i.tweaks.kernel_paging_shield_active = true; }},
        {"mpo_disabled", [](SettingsScreenInputs& i) { i.tweaks.mpo_disabled = true; }},
        {"mmcss_gaming", [](SettingsScreenInputs& i) { i.tweaks.mmcss_gaming_optimized = true; }},
        {"game_dvr", [](SettingsScreenInputs& i) { i.tweaks.gamedvr_disabled = true; }},
        {"memory_compression", [](SettingsScreenInputs& i) { i.tweaks.memory_compression_enabled = true; }},
        {"power_profile", [](SettingsScreenInputs& i) { i.tweaks.apu_power_optimized = true; }},
        {"memory_guard", [](SettingsScreenInputs& i) { i.memory_guard_active = true; }},
    };
    bool isolated = true;
    std::string bad;
    for (const Case& c : cases) {
        SettingsScreenInputs changed = in;
        c.apply(changed);
        const auto m = BuildSettingsScreen(changed);
        for (const auto& f : m.families) {
            for (const auto& r : f.rows) {
                const bool want = std::string(r.spec->id) == c.id;
                if (r.on != want) {
                    isolated = false;
                    bad += std::string(c.id) + "/" + r.spec->id + " ";
                }
            }
        }
    }
    RecordResult(isolated, "Test 63.2: each state field turns on only its own row", bad);

    // 2b. A Win32PrioritySeparation the user set (not the Windows 2, not a value VegaFlow writes) is not shown as ours.
    SettingsScreenInputs foreign = in;
    foreign.tweaks.win32_priority_separation = 0x18;
    bool foreign_off = true;
    for (const auto& f : BuildSettingsScreen(foreign).families) {
        for (const auto& r : f.rows) foreign_off = foreign_off && !(std::string(r.spec->id) == "priority_separation" && r.on);
    }
    RecordResult(foreign_off, "Test 63.2b: a priority separation value VegaFlow did not write is not shown as on");

    // 3. Every catalog row has a state case (none can silently stay off for a missing mapping).
    bool covered = true;
    for (const auto& s : SettingsCatalog()) {
        bool found = false;
        for (const Case& c : cases) found = found || std::string(c.id) == s.id;
        covered = covered && found;
    }
    RecordResult(covered, "Test 63.3: every catalog row has a test case for its on/off state");

    // 4. Evidence labels and the (i) text come from the row; the banner follows the experimental switch.
    SettingSpec s;
    s.evidence = Evidence::Gain;
    const bool l1 = EvidenceLabel(s) == "Recomendado";
    s.evidence = Evidence::Loss;
    const bool l2 = EvidenceLabel(s) == "No recomendado";
    s.evidence = Evidence::NoEffect;
    const bool l3 = EvidenceLabel(s) == "Sin efecto medido";
    s.evidence = Evidence::NotMeasured;
    const bool l4 = EvidenceLabel(s) == "Sin medir";
    SettingsScreenInputs hidden = in;
    hidden.show_experimental = false;
    const auto quiet = BuildSettingsScreen(hidden);
    const auto* guard = FindRow(quiet, "memory_guard");
    RecordResult(l1 && l2 && l3 && l4 && !quiet.experimental_notice && guard && guard->evidence_label == "Sin medir" &&
                     guard->tip == ChangeDescription(*guard->spec) && FindRow(quiet, "mpo_disabled") == nullptr,
                 "Test 63.4: labels follow the evidence, the (i) text is the row text, and the banner follows the switch");
}

} // namespace vftest
