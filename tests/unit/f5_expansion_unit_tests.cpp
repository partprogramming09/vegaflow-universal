// F5.1 process tree through the Rust bridge, and F5.2 targets without a window / OpenGL, driven by fakes.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "bridge/rust_bridge.h"
#include "detect/process_detector.hpp"
#include "fake_device_probe.hpp"
#include "model/game.hpp"
#include "session/config.hpp"
#include "session/game_session.hpp"

#include "../test_support.hpp"

#include <map>
#include <string>
#include <vector>

namespace vftest {

namespace {

constexpr uint32_t kGame = 900;

VfProcessTreeNode Node(uint32_t pid, uint32_t ppid, uint64_t t, uint8_t cls, uint8_t action) {
    VfProcessTreeNode n{};
    n.pid = pid;
    n.parent_pid = ppid;
    n.creation_time = t;
    n.process_class = cls;
    n.action = action;
    return n;
}

std::vector<VfProcessDecision> Inherit(const std::vector<VfProcessTreeNode>& nodes) {
    std::vector<VfProcessDecision> out(nodes.size());
    if (!vf_rust_inherit_process_actions(nodes.data(), nodes.size(), kGame, out.data())) out.clear();
    return out;
}

// One scripted process table for the session cycle: who is alive and when each was created.
class ScriptedEnv final : public vegaflow::session::GameSessionEnvironment {
public:
    std::vector<vegaflow::DetectedGameProcess> targets; // what a full scan would find, first one wins
    std::map<uint32_t, uint64_t> alive;                 // pid -> creation time
    int detect_calls = 0;
    std::vector<uint32_t> boosted_pids;
    std::string last_api;

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const vegaflow::ProcessPolicyConfig&) override {
        ++detect_calls;
        for (const auto& t : targets) {
            if (alive.count(t.pid)) return t;
        }
        return std::nullopt;
    }
    bool IsWindowValid(HWND) override { return true; }
    uint32_t WindowThreadId(HWND) override { return 0; }
    bool IsProcessAlive(uint32_t pid) override { return alive.count(pid) != 0; }
    uint64_t ProcessCreationTime(uint32_t pid) override {
        auto it = alive.find(pid);
        return it == alive.end() ? 0 : it->second;
    }
    std::vector<vegaflow::BackgroundProcessTarget> ClassifyBackgroundProcesses(uint32_t, const vegaflow::ProcessPolicyConfig&) override { return {}; }
    void QueryLiveMemory(uint64_t& b, uint64_t& u, bool& v) override { b = 0; u = 0; v = false; }
    void SetDynamicPrioritySeparation(uint32_t) override {}
    void SetGameDvrCaptureDisabled(bool) override {}
    void SetApuLaptopPowerProfile(bool, const vegaflow::ApuOptimizationPolicy&) override {}
    void ApplyDxgiFlipModelUpgrade(const std::wstring&) override {}
    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring&, uint32_t, const std::string&) override { return {}; }
    void ApplyEngineUmaPreset(const vegaflow::SteamGameEntry&, const vegaflow::ApuOptimizationPolicy&, uint32_t) override {}
    void ApplyCpuOptimizations(uint32_t pid, uint64_t, uint32_t, bool, const std::vector<ULONG>&, const std::vector<ULONG>&,
                               const std::vector<vegaflow::BackgroundProcessTarget>&, uint32_t& out_throttled) override {
        boosted_pids.push_back(pid);
        out_throttled = 0;
    }
    void RestoreCpuAll() override {}
    void EvaluateMemory(uint32_t, const std::vector<uint32_t>&, bool) override {}
    void EvaluateUmaGuard(const vegaflow::ApuOptimizationPolicy&, const std::vector<uint32_t>&) override {}
    void SaveSessionReport(const std::string&, const std::string& api, uint32_t) override { last_api = api; }
};

vegaflow::DetectedGameProcess Target(uint32_t pid, uint64_t t, const wchar_t* exe, HWND hwnd, vegaflow::RenderApi api) {
    vegaflow::DetectedGameProcess d{};
    d.pid = pid;
    d.creation_time = t;
    d.exe_name = exe;
    d.full_path = std::wstring(L"C:\\Tools\\") + exe;
    d.api = api;
    d.main_window = hwnd;
    return d;
}

} // namespace

// F5.1: the Rust inheritance rule reached through the C ABI.
void Test170() {
    // steam (throttle) -> unknown tool: inherited; game and its helper: not; Windows parent: not; orphan: not.
    const std::vector<VfProcessTreeNode> nodes = {
        Node(10, 1, 100, VF_PROC_CLASS_STORES_LAUNCHERS, VF_PROC_ACTION_THROTTLE),        // 0 steam
        Node(11, 10, 200, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),            // 1 tool -> throttle
        Node(kGame, 10, 300, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME),        // 2 the game
        Node(901, kGame, 400, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),        // 3 game helper
        Node(20, 1, 100, VF_PROC_CLASS_WINDOWS_SYSTEM, VF_PROC_ACTION_DO_NOT_TOUCH),      // 4 windows
        Node(21, 20, 200, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),            // 5 under windows
        Node(30, 77, 200, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),            // 6 parent gone
        Node(40, 41, 100, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),            // 7 cycle
        Node(41, 40, 100, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),            // 8 cycle
        Node(50, 10, 50, VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH),             // 9 older than "parent": reused PID
    };
    const auto d = Inherit(nodes);
    const bool shape = d.size() == nodes.size();
    const bool ok = shape &&
        d[1].action == VF_PROC_ACTION_THROTTLE && d[1].process_class == VF_PROC_CLASS_STORES_LAUNCHERS &&
        d[2].action == VF_PROC_ACTION_BOOST_AS_GAME && d[3].action == VF_PROC_ACTION_DO_NOT_TOUCH &&
        d[5].action == VF_PROC_ACTION_DO_NOT_TOUCH && d[6].action == VF_PROC_ACTION_DO_NOT_TOUCH &&
        d[7].action == VF_PROC_ACTION_DO_NOT_TOUCH && d[8].action == VF_PROC_ACTION_DO_NOT_TOUCH &&
        d[9].action == VF_PROC_ACTION_DO_NOT_TOUCH;
    const bool null_refused = !vf_rust_inherit_process_actions(nullptr, 3, kGame, nullptr);
    const bool empty_ok = vf_rust_inherit_process_actions(nullptr, 0, kGame, nullptr);
    RecordResult(ok && null_refused && empty_ok, "Test 170: child inherits the parent's Throttle/EcoOnly; the game, its helpers, Windows, orphans, cycles and reused PIDs are cut (F5.1)",
                 std::string("Shape=") + (shape ? "1" : "0") + ", NullRefused=" + (null_refused ? "1" : "0"));
}

// F5.2: a custom exe without a window is trusted from the cache (no full scan every cycle), a reused PID is not,
// a second target does not steal the session, and OpenGL is a named API.
void Test171() {
    FakeDeviceProbe probe;
    probe.snapshot.dxcore_available = true;
    probe.snapshot.ram_probe_ok = true;
    probe.snapshot.total_physical_ram_bytes = 16ULL * 1024 * 1024 * 1024;
    probe.snapshot.cpu_probe_ok = true;
    probe.snapshot.cpu_topology.physical_cores = 6;
    auto hw = vegaflow::session::BuildHardwareProfileFromSnapshot(probe.ProbeSystem(false), false, 0);
    vegaflow::app::AppConfig cfg{};
    vegaflow::session::GameSessionState state{};
    ScriptedEnv env;

    // A: llama-bench, no window. B: a second listed target, also without window.
    env.targets.push_back(Target(100, 5000, L"llama-bench.exe", nullptr, vegaflow::RenderApi::Unknown));
    env.targets.push_back(Target(200, 6000, L"llama-cli.exe", nullptr, vegaflow::RenderApi::Unknown));
    env.alive[100] = 5000;
    env.alive[200] = 6000;

    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state);
    const bool a_first = state.game_active && state.active_game_pid == 100 && env.detect_calls == 1;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state);
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state);
    const bool fast_path = env.detect_calls == 1 && env.boosted_pids.size() == 3; // no rescan, still boosted each cycle
    const bool two_targets = state.active_game_pid == 100; // B alive the whole time, A keeps the session

    // A exits: the next cycle falls back to the full scan and finds B.
    env.alive.erase(100);
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state);
    const bool switched_to_b = state.active_game_pid == 200 && state.game_active;

    // B exits and its PID is reused by another process (new creation time): never taken for B.
    env.alive[200] = 9999;
    env.targets.clear();
    const int before = env.detect_calls;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state);
    const bool reuse_rescans = env.detect_calls == before + 1;

    // Unknown creation time on the saved target: no blind trust either.
    vegaflow::session::GameSessionState no_time{};
    no_time.game_active = true;
    no_time.active_game_pid = 300;
    no_time.active_game_creation_time = 0;
    ScriptedEnv env2;
    env2.alive[300] = 1234;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env2, hw, no_time);
    const bool unknown_time_rescans = env2.detect_calls == 1;

    // OpenGL: named, ranked after Direct3D/Vulkan, and only true when the caller says the driver backs it.
    using vegaflow::ProcessDetector;
    using vegaflow::RenderApi;
    const bool gl_ok =
        ProcessDetector::PickRenderApi(false, false, false, false, true) == RenderApi::OpenGL &&
        ProcessDetector::PickRenderApi(false, false, false, false, false) == RenderApi::Unknown &&
        ProcessDetector::PickRenderApi(true, false, false, false, true) == RenderApi::DirectX12 &&
        ProcessDetector::PickRenderApi(false, true, true, false, true) == RenderApi::Vulkan &&
        std::string(ProcessDetector::RenderApiToString(RenderApi::OpenGL)) == "OpenGL";

    // An OpenGL target keeps its label across the cached cycles.
    vegaflow::session::GameSessionState gl_state{};
    ScriptedEnv gl_env;
    gl_env.targets.push_back(Target(400, 7000, L"retro.exe", reinterpret_cast<HWND>(static_cast<uintptr_t>(0x42)), RenderApi::OpenGL));
    gl_env.alive[400] = 7000;
    vegaflow::session::RunGameSessionCycle(cfg, probe, gl_env, hw, gl_state);
    vegaflow::session::RunGameSessionCycle(cfg, probe, gl_env, hw, gl_state);
    const bool gl_label = gl_state.active_game_api == "OpenGL" && gl_env.detect_calls == 1;

    RecordResult(a_first && fast_path && two_targets && switched_to_b && reuse_rescans && unknown_time_rescans && gl_ok && gl_label,
                 "Test 171: windowless custom target uses the cache, reused PID/unknown time rescan, two targets keep the first, OpenGL is named (F5.2)",
                 std::string("AFirst=") + (a_first ? "1" : "0") + ", FastPath=" + (fast_path ? "1" : "0") +
                     ", TwoTargets=" + (two_targets ? "1" : "0") + ", SwitchedToB=" + (switched_to_b ? "1" : "0") +
                     ", ReuseRescans=" + (reuse_rescans ? "1" : "0") + ", UnknownTimeRescans=" + (unknown_time_rescans ? "1" : "0") +
                     ", GlOk=" + (gl_ok ? "1" : "0") + ", GlLabel=" + (gl_label ? "1" : "0"));
}

} // namespace vftest
