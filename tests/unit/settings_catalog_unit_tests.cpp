// Unit tests: the settings catalog behind the "Ajustes del sistema" screen (phase G): which families a machine sees,
// the experimental gate, the evidence gate for "Recomendado", and the exact-change text generated from the table.

#include "session/settings_catalog.hpp"
#include "../test_support.hpp"

#include <set>
#include <string>

namespace vftest {

namespace {

using vegaflow::DeviceFacts;
using vegaflow::GpuConnection;
using vegaflow::GpuVendor;
using vegaflow::SettingFamily;

bool HasFamily(const std::vector<vegaflow::FamilyView>& views, SettingFamily family) {
    for (const auto& v : views) {
        if (v.family == family) return true;
    }
    return false;
}

size_t RowCount(const std::vector<vegaflow::FamilyView>& views) {
    size_t n = 0;
    for (const auto& v : views) n += v.rows.size();
    return n;
}

} // namespace

void Test61() {
    const DeviceFacts amd_igpu{GpuVendor::AMD, GpuConnection::Integrated, true};
    const DeviceFacts amd_dgpu{GpuVendor::AMD, GpuConnection::Discrete, true};
    const DeviceFacts amd_blocked{GpuVendor::AMD, GpuConnection::Unknown, false};
    const DeviceFacts intel_igpu{GpuVendor::Intel, GpuConnection::Integrated, true};
    const DeviceFacts intel_dgpu{GpuVendor::Intel, GpuConnection::Discrete, true};
    const DeviceFacts nvidia_dgpu{GpuVendor::NVIDIA, GpuConnection::Discrete, true};
    const DeviceFacts unknown{GpuVendor::Unknown, GpuConnection::Unknown, false};

    // 1. Experimental settings stay hidden by default and appear when the user turns them on.
    const auto amd_default = vegaflow::VisibleFamilies(amd_igpu, false);
    const auto amd_all = vegaflow::VisibleFamilies(amd_igpu, true);
    bool default_has_experimental = false;
    for (const auto& v : amd_default) {
        for (const auto* r : v.rows) default_has_experimental = default_has_experimental || r->experimental;
    }
    RecordResult(!default_has_experimental && RowCount(amd_default) >= 1 && RowCount(amd_all) > RowCount(amd_default),
                 "Test 61.1: experimental settings are hidden until the user turns them on, then they show");

    // 2. Families follow the hardware, in screen order: shared memory, AMD driver, Windows.
    const auto intel_all = vegaflow::VisibleFamilies(intel_igpu, true);
    const auto nvidia_all = vegaflow::VisibleFamilies(nvidia_dgpu, true);
    const bool amd_ok = amd_all.size() == 3 && amd_all[0].family == SettingFamily::SharedMemory &&
                        amd_all[1].family == SettingFamily::AmdDriver && amd_all[2].family == SettingFamily::Windows;
    const bool intel_ok = !HasFamily(intel_all, SettingFamily::AmdDriver) && HasFamily(intel_all, SettingFamily::SharedMemory);
    const bool nvidia_ok = !HasFamily(nvidia_all, SettingFamily::AmdDriver) && !HasFamily(nvidia_all, SettingFamily::SharedMemory) &&
                           HasFamily(nvidia_all, SettingFamily::Windows);
    RecordResult(amd_ok && intel_ok && nvidia_ok, "Test 61.2: families follow vendor and connection type, in screen order");

    // 3. Settings that cannot be applied are not offered: an unknown GPU, or an AMD one whose driver tweaks the
    //    policy blocks (the optimizer returns no AmdDriver() then), gets only what applies everywhere.
    bool safe_only = true;
    for (const DeviceFacts& d : {unknown, amd_blocked}) {
        const auto v = vegaflow::VisibleFamilies(d, true);
        safe_only = safe_only && !HasFamily(v, SettingFamily::AmdDriver) && !HasFamily(v, SettingFamily::SharedMemory) &&
                    HasFamily(v, SettingFamily::Windows);
    }
    RecordResult(safe_only, "Test 61.3: unknown graphics and blocked AMD driver tweaks get only the settings that apply everywhere");

    // 4. The catalog is well formed: unique ids and every field the (i) text needs.
    std::set<std::string> ids;
    bool unique = true;
    bool described = true;
    for (const auto& s : vegaflow::SettingsCatalog()) {
        unique = unique && ids.insert(s.id).second;
        described = described && *s.name && *s.why && *s.target && *s.change;
    }
    RecordResult(unique && described && !vegaflow::SettingsCatalog().empty() && vegaflow::FindSetting("no_such_setting") == nullptr &&
                     vegaflow::FindSetting("paging_shield") != nullptr,
                 "Test 61.4: ids are unique, every row has name, reason, target and change, FindSetting returns null for unknown ids");

    // 5. The labels follow the evidence: only measured gain is Recommended, only measured loss is Not recommended.
    vegaflow::SettingSpec gain{};
    gain.evidence = vegaflow::Evidence::Gain;
    vegaflow::SettingSpec loss{};
    loss.evidence = vegaflow::Evidence::Loss;
    vegaflow::SettingSpec none{};
    none.evidence = vegaflow::Evidence::NoEffect;
    vegaflow::SettingSpec unmeasured{};
    unmeasured.evidence = vegaflow::Evidence::NotMeasured;
    bool recommended_in_catalog = false;
    for (const auto& s : vegaflow::SettingsCatalog()) recommended_in_catalog = recommended_in_catalog || vegaflow::IsRecommended(s);
    RecordResult(vegaflow::IsRecommended(gain) && !vegaflow::IsRecommended(loss) && !vegaflow::IsRecommended(none) &&
                     !vegaflow::IsRecommended(unmeasured) && vegaflow::IsNotRecommended(loss) && !vegaflow::IsNotRecommended(gain) &&
                     !recommended_in_catalog,
                 "Test 61.5: only measured gain is Recommended and only measured loss is Not recommended; nothing is Recommended yet");

    // 6. Measurements recorded in docs/mediciones/estado-de-verificacion.md are carried: no effect for
    //    Win32PrioritySeparation and GameDVR. The power profile bundles a frequency cap nobody measured, so it is not NoEffect.
    const auto* prio = vegaflow::FindSetting("priority_separation");
    const auto* dvr = vegaflow::FindSetting("game_dvr");
    const auto* power = vegaflow::FindSetting("power_profile");
    RecordResult(prio && dvr && power && prio->evidence == vegaflow::Evidence::NoEffect && dvr->evidence == vegaflow::Evidence::NoEffect &&
                     power->evidence == vegaflow::Evidence::NotMeasured,
                 "Test 61.6: measured no-effect settings are recorded as such; a bundle with an unmeasured write is not");

    // 7. The (i) text only claims undo where there is something to undo: the memory guard trims working sets and
    //    saves no original value.
    bool tip_ok = true;
    for (const auto& s : vegaflow::SettingsCatalog()) {
        const std::string tip = vegaflow::ChangeDescription(s);
        const bool says_undo = tip.find("se puede deshacer") != std::string::npos;
        const bool says_nothing_to_undo = tip.find("no hay nada que deshacer") != std::string::npos;
        tip_ok = tip_ok && tip.find(s.target) != std::string::npos && tip.find(s.change) != std::string::npos &&
                 (s.undoable ? (says_undo && !says_nothing_to_undo) : (says_nothing_to_undo && !says_undo));
    }
    const auto* guard = vegaflow::FindSetting("memory_guard");
    RecordResult(tip_ok && guard && !guard->undoable,
                 "Test 61.7: the (i) text names the target and value, and claims undo only for undoable rows");

    // 8. Nothing in the catalog promises a gain.
    bool no_promises = true;
    for (const auto& s : vegaflow::SettingsCatalog()) {
        const std::string text = std::string(s.name) + " " + s.why + " " + vegaflow::ChangeDescription(s);
        no_promises = no_promises && text.find("mejora") == std::string::npos && text.find("FPS") == std::string::npos &&
                      text.find("más rápido") == std::string::npos;
    }
    RecordResult(no_promises, "Test 61.8: catalog texts promise no gain (no FPS, no 'mejora', no 'más rápido')");

    // 9. Hardware gating of single rows, and no family is ever shown empty.
    const auto amd_dgpu_all = vegaflow::VisibleFamilies(amd_dgpu, true);
    bool amd_dgpu_rows = false;
    bool dgpu_has_vram_row = false;
    for (const auto& v : amd_dgpu_all) {
        for (const auto* r : v.rows) {
            amd_dgpu_rows = amd_dgpu_rows || std::string(r->id) == "amd_driver_profile";
            dgpu_has_vram_row = dgpu_has_vram_row || std::string(r->id) == "vram_reporting";
        }
    }
    bool none_empty = true;
    for (const DeviceFacts& d : {amd_igpu, amd_dgpu, amd_blocked, intel_igpu, intel_dgpu, nvidia_dgpu, unknown}) {
        for (const auto& v : vegaflow::VisibleFamilies(d, true)) none_empty = none_empty && !v.rows.empty();
    }
    RecordResult(amd_dgpu_rows && !dgpu_has_vram_row && none_empty,
                 "Test 61.9: a dedicated AMD GPU gets the driver profile but not the shared-memory VRAM row; no empty families");

    // 10. The settings the daemon applies without being asked are pinned here, because "unmeasured stays off by default"
    //     is the project rule and these break it today (pending decision in docs/plan/maestro/14-fase-g.md). Changing
    //     this list must be a deliberate edit of this test.
    std::set<std::string> by_default;
    for (const auto& s : vegaflow::SettingsCatalog()) {
        if (s.applied_by_default) by_default.insert(s.id);
    }
    const std::set<std::string> expected = {"vram_reporting", "priority_separation", "game_dvr", "power_profile"};
    RecordResult(by_default == expected, "Test 61.10: the settings applied by default are exactly the known four");

    // 11. Only the memory guard has no switch: VegaFlow decides it by itself, so the screen shows it as automatic.
    std::set<std::string> no_switch;
    for (const auto& s : vegaflow::SettingsCatalog()) {
        if (!s.user_switch) no_switch.insert(s.id);
    }
    RecordResult(no_switch == std::set<std::string>{"memory_guard"}, "Test 61.11: only the memory guard has no user switch");
}

} // namespace vftest
