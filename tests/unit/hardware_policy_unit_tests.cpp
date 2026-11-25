// Unit tests (F4.3): the hardware decisions that session/ needs without Windows: the CPU partition by processor group, the
// policy from a profile, and a game session cycle driven by a fake environment that also answers the window's thread.
// Runs on the host.

#include "bridge/hardware_policy.hpp"
#include "session/game_session.hpp"
#include "fake_device_probe.hpp"
#include "../test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

namespace vftest {

namespace {

namespace gs = vegaflow::session;

// Two processor groups of 4 cores x 2 threads each, same cache, one efficiency class.
std::vector<vegaflow::CpuCoreSetInfo> TwoGroups() {
    std::vector<vegaflow::CpuCoreSetInfo> sets;
    uint32_t id = 0x100;
    for (uint16_t group = 0; group < 2; ++group) {
        for (uint32_t core = 0; core < 4; ++core) {
            for (uint32_t t = 0; t < 2; ++t) {
                vegaflow::CpuCoreSetInfo cs{};
                cs.cpu_set_id = id++;
                cs.group = group;
                cs.core_index = core;
                cs.logical_processor_index = static_cast<uint8_t>(core * 2 + t);
                sets.push_back(cs);
            }
        }
    }
    return sets;
}

std::set<uint32_t> IdsOfGroup(const std::vector<vegaflow::CpuCoreSetInfo>& sets, uint16_t group) {
    std::set<uint32_t> ids;
    for (const auto& s : sets) if (s.group == group) ids.insert(s.cpu_set_id);
    return ids;
}

bool AllIn(const std::vector<ULONG>& v, const std::set<uint32_t>& ids) {
    return !v.empty() && std::all_of(v.begin(), v.end(), [&](ULONG x) { return ids.count(x) != 0; });
}

class RecordingEnv final : public gs::GameSessionEnvironment {
public:
    uint32_t thread_for_window = 4242;
    uint32_t presenter_seen = 0;
    int window_asks = 0;

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const vegaflow::ProcessPolicyConfig&) override { return std::nullopt; }
    bool IsWindowValid(HWND) override { return true; }
    bool IsProcessAlive(uint32_t) override { return true; }
    uint32_t WindowThreadId(HWND) override { ++window_asks; return thread_for_window; }
    std::vector<vegaflow::BackgroundProcessTarget> ClassifyBackgroundProcesses(uint32_t, const vegaflow::ProcessPolicyConfig&) override { return {}; }
    void QueryLiveMemory(uint64_t& b, uint64_t& u, bool& v) override { b = 0; u = 0; v = false; }
    void SetDynamicPrioritySeparation(uint32_t) override {}
    void SetGameDvrCaptureDisabled(bool) override {}
    void SetApuLaptopPowerProfile(bool, const vegaflow::ApuOptimizationPolicy&) override {}
    void ApplyDxgiFlipModelUpgrade(const std::wstring&) override {}
    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring&, uint32_t, const std::string&) override { return {}; }
    void ApplyEngineUmaPreset(const vegaflow::SteamGameEntry&, const vegaflow::ApuOptimizationPolicy&, uint32_t) override {}
    void ApplyCpuOptimizations(uint32_t, uint64_t, uint32_t presenter, bool, const std::vector<ULONG>&, const std::vector<ULONG>&,
                               const std::vector<vegaflow::BackgroundProcessTarget>&, uint32_t& out) override {
        presenter_seen = presenter;
        out = 0;
    }
    void RestoreCpuAll() override {}
    void EvaluateMemory(uint32_t, const std::vector<uint32_t>&, bool) override {}
    void EvaluateUmaGuard(const vegaflow::ApuOptimizationPolicy&, const std::vector<uint32_t>&) override {}
    void SaveSessionReport(const std::string&, const std::string&, uint32_t) override {}
};

} // namespace

void Test146() {
    // 1. The partition follows the processor group it is given: each answer lies inside that group, and an unknown group
    //    falls back to the first one.
    const auto sets = TwoGroups();
    vegaflow::CpuTopologyProfile g0;
    g0.all_cpu_sets = sets;
    vegaflow::PartitionCpuSets(g0, 0);
    vegaflow::CpuTopologyProfile g1;
    g1.all_cpu_sets = sets;
    vegaflow::PartitionCpuSets(g1, 1);
    vegaflow::CpuTopologyProfile gx;
    gx.all_cpu_sets = sets;
    vegaflow::PartitionCpuSets(gx, 9);
    RecordResult(AllIn(g0.game_optimal_cpu_set_ids, IdsOfGroup(sets, 0)) && AllIn(g1.game_optimal_cpu_set_ids, IdsOfGroup(sets, 1)) &&
                     g0.game_optimal_cpu_set_ids != g1.game_optimal_cpu_set_ids && gx.game_optimal_cpu_set_ids == g0.game_optimal_cpu_set_ids &&
                     g0.logical_processors == 16 && g0.physical_cores == 8,
                 "Test 146.1: PartitionCpuSets keeps the game's CPU sets inside the given processor group and falls back to the first group");

    // 2. Without CPU sets there is nothing to partition and nothing is invented.
    vegaflow::CpuTopologyProfile empty;
    empty.game_optimal_cpu_set_ids = {1, 2};
    vegaflow::PartitionCpuSets(empty, 0);
    RecordResult(empty.game_optimal_cpu_set_ids.empty() && empty.background_cpu_set_ids.empty(), "Test 146.2: PartitionCpuSets with no CPU sets leaves both lists empty");

    // 3. A snapshot carries the group it was probed in, and the profile built from it partitions by that group.
    vegaflow::RawDeviceSnapshot snap{};
    snap.cpu_probe_ok = true;
    snap.cpu_topology.all_cpu_sets = sets;
    snap.current_processor_group = 1;
    const auto hw = gs::BuildHardwareProfileFromSnapshot(snap);
    RecordResult(AllIn(hw.cpu_topology.game_optimal_cpu_set_ids, IdsOfGroup(sets, 1)),
                 "Test 146.3: BuildHardwareProfileFromSnapshot partitions by the processor group the snapshot was probed in");

    // 4. The policy of a profile comes from Rust with a label and a guard name, with no Windows read.
    const auto policy = vegaflow::BuildOptimizationPolicy(hw);
    RecordResult(!policy.tier_label.empty() && !policy.guard_mode_name.empty(), "Test 146.4: BuildOptimizationPolicy names the tier and the guard for any profile", policy.tier_label);

    // 5. A running game's presenter thread comes from the environment (the Windows window query), not from a Windows call.
    FakeDeviceProbe probe;
    probe.snapshot = snap;
    RecordingEnv env;
    vegaflow::app::AppConfig cfg{};
    gs::GameSessionState state{};
    state.game_active = true;
    state.active_game_pid = 77;
    state.active_game_exe = L"Game.exe";
    state.active_game_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x10));
    auto live = hw;
    gs::RunGameSessionCycle(cfg, probe, env, live, state);
    RecordResult(env.window_asks == 1 && env.presenter_seen == 4242,
                 "Test 146.5: the cycle asks the environment for the game window's thread and hands it to the CPU optimizer");
}

} // namespace vftest
