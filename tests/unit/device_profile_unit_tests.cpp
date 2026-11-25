// Unit tests for Phase 3: device_profiles.tsv shared vectors, Engine.ini cut gating, unreadable-probe fallbacks,
// hot-plug adapter refresh, and RunGameSessionCycle passing static policy vs live DXGI budget.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "detect/hardware_profiler.hpp"
#include "optimize/steam/steam_engine_optimizer.hpp"
#include "session/game_session.hpp"
#include "session/system_info.hpp"
#include "fake_system_reads.hpp"
#include "fake_device_probe.hpp"
#include "../test_support.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace vftest {

namespace {

std::wstring Utf8ToWideTest(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::vector<std::string> SplitChar(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        out.push_back(item);
    }
    if (!s.empty() && s.back() == delim) {
        out.emplace_back();
    }
    return out;
}

uint32_t ParseHexOrDec(const std::string& s) {
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
        return static_cast<uint32_t>(std::stoul(s.substr(2), nullptr, 16));
    }
    return static_cast<uint32_t>(std::stoul(s));
}

vegaflow::GpuConnection ParseConn(const std::string& s) {
    if (s == "integrated") return vegaflow::GpuConnection::Integrated;
    if (s == "discrete")   return vegaflow::GpuConnection::Discrete;
    if (s == "external")   return vegaflow::GpuConnection::External;
    return vegaflow::GpuConnection::Unknown;
}

vegaflow::SystemArch ParseArch(const std::string& s) {
    if (s == "x64")            return vegaflow::SystemArch::X64;
    if (s == "arm64_emu")      return vegaflow::SystemArch::ARM64_Emulated;
    if (s == "arm64_native")   return vegaflow::SystemArch::ARM64_Native;
    if (s == "x86")            return vegaflow::SystemArch::X86;
    return vegaflow::SystemArch::Unknown;
}

vegaflow::GpuVendor VendorFromIdTest(uint32_t vid) {
    switch (vid) {
        case 0x1002: return vegaflow::GpuVendor::AMD;
        case 0x8086: return vegaflow::GpuVendor::Intel;
        case 0x10DE: return vegaflow::GpuVendor::NVIDIA;
        default:     return vegaflow::GpuVendor::Unknown;
    }
}

class FakeGameSessionEnv final : public vegaflow::session::GameSessionEnvironment {
public:
    std::optional<vegaflow::DetectedGameProcess> active_game;
    uint64_t live_vram_budget = 0;
    uint64_t live_vram_usage = 0;
    bool live_vram_valid = false;
    vegaflow::SteamGameEntry inspected_game{};

    bool power_profile_called = false;
    vegaflow::ApuOptimizationPolicy power_profile_policy{};
    bool unreal_preset_called = false;
    vegaflow::ApuOptimizationPolicy unreal_preset_policy{};
    uint32_t unreal_preset_pool_mb = 0;
    bool memory_evaluated = false;
    bool uma_guard_evaluated = false;
    vegaflow::ApuOptimizationPolicy uma_guard_policy{};

    std::optional<vegaflow::DetectedGameProcess> DetectActiveGame(const vegaflow::ProcessPolicyConfig&) override {
        return active_game;
    }
    bool IsWindowValid(HWND) override { return true; }
    uint32_t WindowThreadId(HWND) override { return 0; }
    bool IsProcessAlive(uint32_t) override { return active_game.has_value(); }
    std::vector<vegaflow::BackgroundProcessTarget> ClassifyBackgroundProcesses(uint32_t, const vegaflow::ProcessPolicyConfig&) override {
        vegaflow::BackgroundProcessTarget t{};
        t.pid = 4001;
        t.creation_time = 100;
        t.exe_name = L"chrome.exe";
        t.action = vegaflow::ProcessAction::Throttle;
        return {t};
    }

    void QueryLiveMemory(uint64_t& out_budget, uint64_t& out_usage, bool& out_valid) override {
        out_budget = live_vram_budget;
        out_usage = live_vram_usage;
        out_valid = live_vram_valid;
    }

    void SetDynamicPrioritySeparation(uint32_t) override {}
    void SetGameDvrCaptureDisabled(bool) override {}

    void SetApuLaptopPowerProfile(bool optimize, const vegaflow::ApuOptimizationPolicy& static_policy) override {
        if (optimize) {
            power_profile_called = true;
            power_profile_policy = static_policy;
        }
    }

    void ApplyDxgiFlipModelUpgrade(const std::wstring&) override {}

    vegaflow::SteamGameEntry InspectGameDirectory(const std::wstring&, uint32_t, const std::string&) override {
        return inspected_game;
    }

    void ApplyEngineUmaPreset(const vegaflow::SteamGameEntry&, const vegaflow::ApuOptimizationPolicy& static_policy, uint32_t pool_mb) override {
        unreal_preset_called = true;
        unreal_preset_policy = static_policy;
        unreal_preset_pool_mb = pool_mb;
    }

    void ApplyCpuOptimizations(uint32_t, uint64_t, uint32_t, bool, const std::vector<ULONG>&, const std::vector<ULONG>&, const std::vector<vegaflow::BackgroundProcessTarget>&, uint32_t& out_throttled) override {
        out_throttled = 1;
    }
    void RestoreCpuAll() override {}

    void EvaluateMemory(uint32_t, const std::vector<uint32_t>&, bool allow_ram_purge) override {
        if (allow_ram_purge) memory_evaluated = true;
    }
    void EvaluateUmaGuard(const vegaflow::ApuOptimizationPolicy& live_policy, const std::vector<uint32_t>&) override {
        uma_guard_evaluated = true;
        uma_guard_policy = live_policy;
    }
    void SaveSessionReport(const std::string&, const std::string&, uint32_t) override {}
};

} // namespace

void Test51() {
    // 1. Verify every row in tests/vectors/device_profiles.tsv through C++ -> Rust FFI.
    int total_vectors = 0;
    int vector_mismatches = 0;
    std::string first_mismatch;

    ForEachTsvRow("tests/vectors/device_profiles.tsv", [&](const std::vector<std::string>& col, const std::string& line) {
        if (col.size() != 23) {
            ++vector_mismatches;
            if (first_mismatch.empty()) first_mismatch = "bad col count: " + line;
            return;
        }

        vegaflow::RawDeviceSnapshot snap{};
        snap.dxcore_available = (col[11] == "1");
        if (col[1] != "-") {
            for (const auto& gpu_str : SplitChar(col[1], ';')) {
                const auto p = SplitChar(gpu_str, ':');
                if (p.size() != 8) {
                    ++vector_mismatches;
                    if (first_mismatch.empty()) first_mismatch = "bad gpu spec in " + col[0];
                    return;
                }
                vegaflow::GpuProfile g{};
                g.vendor_id = ParseHexOrDec(p[0]);
                g.device_id = ParseHexOrDec(p[1]);
                g.vendor = VendorFromIdTest(g.vendor_id);
                g.dedicated_vram_bytes = std::stoull(p[2]) * 1024ULL * 1024ULL;
                g.dxcore_available = (p[3] == "1");
                g.dxcore_integrated = (p[4] == "1");
                g.dxcore_detachable = (p[5] == "1");
                g.removal_policy_detachable = (p[6] == "1");
                g.description = Utf8ToWideTest(p[7]);
                snap.gpus.push_back(std::move(g));
            }
        }
        snap.gpu_probe_ok = !snap.gpus.empty();
        const uint8_t pref = static_cast<uint8_t>(std::stoul(col[2]));
        snap.total_physical_ram_bytes = std::stoull(col[3]) * 1024ULL * 1024ULL;
        snap.ram_probe_ok = (col[4] == "1");
        snap.cpu_topology.physical_cores = static_cast<uint32_t>(std::stoul(col[5]));
        snap.cpu_family = ParseHexOrDec(col[6]);
        snap.cpu_model = ParseHexOrDec(col[7]);
        snap.system_arch = ParseArch(col[8]);
        snap.power_status.has_battery = (col[10] == "1");
        if (col[9] == "ac") {
            snap.power_status.known = true;
            snap.power_status.ac_online = true;
            snap.power_status.battery_saver = false;
        } else if (col[9] == "battery") {
            snap.power_status.known = true;
            snap.power_status.ac_online = false;
            snap.power_status.battery_saver = false;
        } else if (col[9] == "saver") {
            snap.power_status.known = true;
            snap.power_status.ac_online = true;
            snap.power_status.battery_saver = true;
        } else {
            snap.power_status.known = false;
        }

        const auto hw = vegaflow::session::BuildHardwareProfileFromSnapshot(snap, false, pref);
        const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw);

        (void)col[12]; // expected selected_idx tested directly in Rust
        const auto exp_conn = ParseConn(col[13]);
        const auto exp_arch = static_cast<vegaflow::GpuArchitectureGen>(std::stoul(col[14]));
        const auto exp_tier = static_cast<vegaflow::ApuTier>(std::stoul(col[15]));
        const bool exp_visual = (col[16] == "1");
        const bool exp_guard = (col[17] == "1");
        const bool exp_vram_reg = (col[18] == "1");
        const bool exp_purge = (col[19] == "1");
        const bool exp_power = (col[20] == "1");
        const bool exp_block_drv = (col[21] == "1");
        const uint64_t exp_reasons = std::stoull(col[22], nullptr, 16); // F2.7: the rules that decided, one bit each

        ++total_vectors;
        if (pol.decision_reasons != exp_reasons ||
            pol.gpu_connection != exp_conn ||
            pol.arch_gen != exp_arch ||
            pol.tier != exp_tier ||
            pol.apply_visual_cuts != exp_visual ||
            pol.apply_uma_memory_guard != exp_guard ||
            pol.apply_uma_vram_reg_override != exp_vram_reg ||
            pol.allow_ram_purge != exp_purge ||
            pol.force_high_perf_power_plan != exp_power ||
            pol.block_driver_tweaks != exp_block_drv) {
            ++vector_mismatches;
            if (first_mismatch.empty()) {
                first_mismatch = col[0] + " (conn=" + std::to_string(static_cast<int>(pol.gpu_connection)) +
                                 " arch=" + std::to_string(static_cast<int>(pol.arch_gen)) +
                                 " tier=" + std::to_string(static_cast<int>(pol.tier)) + ")";
            }
        }
    });
    const bool vectors_ok = (total_vectors >= 34 && vector_mismatches == 0);

    // 2. Engine.ini block: Discrete/External have NO visual cuts; unverified CVars removed everywhere.
    vegaflow::ApuOptimizationPolicy integrated_pol{};
    integrated_pol.tier = vegaflow::ApuTier::Tier2_MidVegaUMA;
    integrated_pol.apply_visual_cuts = true;
    integrated_pol.safe_vram_pool_mb = 4096;
    const std::string uma_ini = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(integrated_pol);
    const bool uma_ini_ok =
        uma_ini.find("r.ShaderPipelineCache.Enabled=1") != std::string::npos &&
        uma_ini.find("r.AOQuality=0") != std::string::npos &&
        uma_ini.find("r.ShaderPipelineCache.AsyncCompileRate") == std::string::npos &&
        uma_ini.find("r.GTSyncType") == std::string::npos;

    vegaflow::ApuOptimizationPolicy dgpu_pol{};
    dgpu_pol.tier = vegaflow::ApuTier::Tier4_DiscreteOrCustom;
    dgpu_pol.gpu_connection = vegaflow::GpuConnection::Discrete;
    dgpu_pol.apply_visual_cuts = false;
    const std::string dgpu_ini = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(dgpu_pol);
    const bool dgpu_ini_ok =
        dgpu_ini.find("r.ShaderPipelineCache.Enabled=1") != std::string::npos &&
        dgpu_ini.find("r.AOQuality") == std::string::npos &&
        dgpu_ini.find("r.SSR.Quality") == std::string::npos &&
        dgpu_ini.find("r.Shadow.") == std::string::npos &&
        dgpu_ini.find("r.Streaming.PoolSize") == std::string::npos &&
        dgpu_ini.find("r.Streaming.AmortizeCPUToGPUCopy") == std::string::npos;

    vegaflow::ApuOptimizationPolicy egpu_pol = dgpu_pol;
    egpu_pol.gpu_connection = vegaflow::GpuConnection::External;
    egpu_pol.apply_egpu_streaming_tweaks = true;
    const std::string egpu_ini = vegaflow::SteamEngineOptimizer::GenerateUnrealUmaBlock(egpu_pol);
    const bool egpu_ini_ok =
        egpu_ini.find("r.ShaderPipelineCache.Enabled=1") != std::string::npos &&
        egpu_ini.find("r.Streaming.AmortizeCPUToGPUCopy=1") != std::string::npos &&
        egpu_ini.find("r.Streaming.MaxNumTexturesToStreamPerFrame=40") != std::string::npos &&
        egpu_ini.find("r.AOQuality") == std::string::npos;

    // 3. Simulated unreadable probe fields -> "No se pudo leer" + prudent gates.
    vegaflow::RawDeviceSnapshot failed_snap{};
    failed_snap.gpu_probe_ok = false;
    failed_snap.ram_probe_ok = false;
    failed_snap.cpu_probe_ok = false;
    failed_snap.power_status.known = false;
    failed_snap.system_arch = vegaflow::SystemArch::Unknown;
    failed_snap.windows_version.known = false;
    const auto failed_hw = vegaflow::session::BuildHardwareProfileFromSnapshot(failed_snap, false, 0);
    const auto failed_pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(failed_hw);
    FakeSystemReads reads;
    const auto failed_sys = vegaflow::SystemInfo::FromHardware(failed_hw, reads, false);
    const std::string failed_json = vegaflow::SystemInfo::ToJson(failed_sys);
    const bool unreadable_ok =
        !failed_pol.apply_visual_cuts &&
        !failed_pol.apply_uma_memory_guard &&
        !failed_pol.allow_ram_purge &&
        !failed_pol.force_high_perf_power_plan &&
        failed_pol.block_driver_tweaks &&
        failed_sys.gpu.adapter_name == "No se pudo leer" &&
        failed_sys.gpu.connection == "No se pudo leer" &&
        failed_sys.cpu.microarchitecture == "No se pudo leer" &&
        failed_sys.ram.channel_topology == "No se pudo leer" &&
        failed_sys.os_kernel.full_version_string == "No se pudo leer" &&
        failed_sys.last_game_disk == "No se pudo leer" &&
        failed_json.find("No se pudo leer") != std::string::npos;

    // 4. RunGameSessionCycle: passes static_policy (NOT live DXGI budget) to power + Engine preset,
    //    detects NVMe disk, respects battery mode, and hot-swaps profile when adapter_signature changes.
    FakeDeviceProbe probe;
    probe.snapshot.dxcore_available = true;
    probe.snapshot.ram_probe_ok = true;
    probe.snapshot.total_physical_ram_bytes = 32ULL * 1024 * 1024 * 1024; // Tier 3 RDNA3 -> static pool 8192 MB
    probe.snapshot.cpu_probe_ok = true;
    probe.snapshot.cpu_topology.physical_cores = 8;
    probe.snapshot.cpu_family = 0x19;
    probe.snapshot.cpu_model = 0x74;
    probe.snapshot.power_status = {true, true, true, false, 100};
    probe.snapshot.system_arch = vegaflow::SystemArch::X64;
    probe.snapshot.adapter_signature = 101;
    vegaflow::GpuProfile igpu{};
    igpu.vendor_id = 0x1002;
    igpu.device_id = 0x15BF;
    igpu.vendor = vegaflow::GpuVendor::AMD;
    igpu.dxcore_available = true;
    igpu.dxcore_integrated = true;
    igpu.description = L"AMD Radeon 780M Graphics";
    probe.snapshot.gpus.push_back(igpu);
    const std::wstring game_exe = L"C:\\Games\\UEGame\\UEGame.exe";
    probe.disk_by_path[game_exe] = vegaflow::GameDiskType::NVMe;

    auto hw = vegaflow::session::BuildHardwareProfileFromSnapshot(probe.ProbeSystem(false), false, 0);
    FakeGameSessionEnv env;
    vegaflow::DetectedGameProcess dg{};
    dg.pid = 7777;
    dg.exe_name = L"UEGame.exe";
    dg.full_path = game_exe;
    dg.api = vegaflow::RenderApi::DirectX12;
    dg.main_window = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1234));
    env.active_game = dg;
    env.inspected_game.engine = vegaflow::GameEngineType::UnrealEngine4_5;
    env.inspected_game.primary_exe_path = game_exe;
    env.live_vram_valid = true;
    env.live_vram_budget = 2048ULL * 1024 * 1024; // 80% = 1638 MB -> clamped to 4096 floor on Tier 3 32GB

    vegaflow::app::AppConfig cfg{};
    cfg.auto_power_profile = true; // E2.2: the power profile is opt-in; this test checks which policy it receives
    cfg.auto_engine_presets = true; // F2.3: so is the Unreal preset
    vegaflow::session::GameSessionState state{};
    uint32_t noisy_cnt = 0;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state, &noisy_cnt);

    const bool static_vs_live_ok =
        env.power_profile_called &&
        env.unreal_preset_called &&
        env.unreal_preset_pool_mb == 8192 &&
        env.unreal_preset_policy.safe_vram_pool_mb == 8192 &&
        env.uma_guard_evaluated &&
        env.uma_guard_policy.safe_vram_pool_mb == 4096 &&
        hw.last_game_disk == vegaflow::GameDiskType::NVMe;

    // Hot-plug an external NVIDIA GPU while idle/next cycle
    vegaflow::GpuProfile egpu{};
    egpu.vendor_id = 0x10DE;
    egpu.device_id = 0x2704;
    egpu.vendor = vegaflow::GpuVendor::NVIDIA;
    egpu.dxcore_available = true;
    egpu.dxcore_detachable = true;
    egpu.description = L"NVIDIA GeForce RTX 4070 (eGPU)";
    probe.snapshot.gpus.push_back(egpu);
    probe.snapshot.adapter_signature = 202;
    probe.snapshot.power_status.ac_online = false; // also switch to battery!
    env.active_game.reset();
    env.power_profile_called = false;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state, &noisy_cnt); // closes game + refreshes hw
    // Launch another game on battery + eGPU
    dg.pid = 8888;
    env.active_game = dg;
    env.unreal_preset_called = false;
    env.uma_guard_evaluated = false;
    vegaflow::session::RunGameSessionCycle(cfg, probe, env, hw, state, &noisy_cnt);

    const bool hotplug_and_battery_ok =
        hw.primary_gpu.connection == vegaflow::GpuConnection::External &&
        !env.power_profile_called &&
        env.unreal_preset_called &&
        !env.unreal_preset_policy.apply_visual_cuts &&
        !env.uma_guard_evaluated;

    const bool passed = vectors_ok && uma_ini_ok && dgpu_ini_ok && egpu_ini_ok &&
                        unreadable_ok && static_vs_live_ok && hotplug_and_battery_ok;

    RecordResult(
        passed,
        "Test 51: Universal device profile vectors, no-cut Engine.ini, unreadable fallbacks, hot-plug & static vs live session cycle",
        std::to_string(total_vectors) + " vectors (" + std::to_string(vector_mismatches) + " mismatches" +
        (first_mismatch.empty() ? "" : ": " + first_mismatch) + "), Engine.ini: " +
        (uma_ini_ok && dgpu_ini_ok && egpu_ini_ok ? "PASS" : "FAIL") + ", Unreadable: " +
        (unreadable_ok ? "PASS" : "FAIL") + ", StaticVsLive: " +
        (static_vs_live_ok ? "PASS" : "FAIL") + ", HotPlug+Battery: " +
        (hotplug_and_battery_ok ? "PASS" : "FAIL"));
}

void Test101() {
    // E1.6: the GUI hands SystemInfo the memory sample and tweak state it already has; the snapshot uses them as given.
    vegaflow::SystemHardwareProfile hw{};
    hw.gpu_probe_ok = true;
    hw.ram_probe_ok = true;
    hw.total_physical_ram_bytes = 16ULL << 30;
    hw.primary_gpu.vendor = vegaflow::GpuVendor::AMD;
    hw.primary_gpu.connection = vegaflow::GpuConnection::Integrated;

    vegaflow::SystemMemoryTelemetry mem{};
    mem.total_physical_bytes = 15ULL << 30;
    mem.free_and_zero_bytes = 3ULL << 30;
    mem.standby_bytes = 5ULL << 30;
    mem.vram.valid = true;
    mem.vram.local_budget_bytes = 1ULL << 30;
    mem.vram.local_usage_bytes = 512ULL << 20;
    vegaflow::SystemGraphicsTweaksState tweaks{};
    tweaks.memory_compression_enabled = true;
    tweaks.mmcss_gaming_optimized = true;

    FakeSystemReads reads;
    const auto s = vegaflow::SystemInfo::FromHardware(hw, mem, tweaks, reads);
    const auto pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw, mem.vram.local_budget_bytes);
    const bool gpu_ok = s.gpu.dxgi_budget.valid && s.gpu.dxgi_budget.local_budget_bytes == mem.vram.local_budget_bytes &&
                        s.gpu.dxgi_budget.local_usage_bytes == mem.vram.local_usage_bytes;
    const bool ram_ok = s.ram.total_physical_bytes == mem.total_physical_bytes && s.ram.free_real_bytes == mem.free_and_zero_bytes &&
                        s.ram.standby_cache_bytes == mem.standby_bytes;
    const bool os_ok = s.os_kernel.memory_compression_enabled && s.os_kernel.mmcss_gaming_optimized;
    const bool guard_ok = s.apu_guard.safe_vram_pool_mb == pol.safe_vram_pool_mb;
    const bool live_ok = gpu_ok && ram_ok && os_ok && guard_ok;
    auto pf = [](bool b) { return b ? "PASS" : "FAIL"; };

    // A sample with no valid VRAM reading keeps the budget invalid (no made-up numbers) and the guard on the static policy.
    vegaflow::SystemMemoryTelemetry no_vram{};
    const auto n = vegaflow::SystemInfo::FromHardware(hw, no_vram, tweaks, reads);
    const bool invalid_ok = !n.gpu.dxgi_budget.valid && n.gpu.dxgi_budget.local_budget_bytes == 0 &&
                            n.ram.total_physical_bytes == hw.total_physical_ram_bytes &&
                            n.apu_guard.safe_vram_pool_mb == vegaflow::HardwareProfiler::BuildOptimizationPolicy(hw, 0).safe_vram_pool_mb;

    RecordResult(live_ok && invalid_ok, "Test 101: SystemInfo built from the GUI's own memory sample and tweak state",
                 std::string("gpu: ") + pf(gpu_ok) + ", ram: " + pf(ram_ok) + ", os: " + pf(os_ok) + ", guard: " + pf(guard_ok) +
                 ", no VRAM reading: " + pf(invalid_ok));
}

} // namespace vftest
