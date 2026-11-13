#include "session/game_session.hpp"

#include "bridge/rust_bridge.h"
#include "detect/hardware_profiler.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "session/text_convert.hpp"

#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace vegaflow::session {

namespace {

std::string WideToUtf8Session(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

uint8_t SystemArchToFfiByte(SystemArch a) {
    switch (a) {
        case SystemArch::X64:            return VF_ARCH_X64;
        case SystemArch::ARM64_Emulated: return VF_ARCH_ARM64_EMULATED;
        case SystemArch::ARM64_Native:   return VF_ARCH_ARM64_NATIVE;
        case SystemArch::X86:            return VF_ARCH_X86;
        case SystemArch::Unknown:
        default:                         return VF_ARCH_UNKNOWN;
    }
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

ProcessPolicyConfig EffectivePolicy(const app::AppConfig& config) {
    ProcessPolicyConfig policy = config.process_policy;
    if (policy.custom_game_exes.empty() && !config.custom_game_exes.empty()) {
        policy.custom_game_exes = config.custom_game_exes;
    }
    return policy;
}

bool IsExeInList(const std::vector<std::wstring>& list, const std::wstring& exe) {
    for (const auto& item : list) {
        if (EqualsIgnoreCase(item, exe)) return true;
    }
    return false;
}

void RestoreEndOfSession(
    const app::AppConfig& config,
    GameSessionEnvironment& env,
    GameSessionState& state) {

    env.StopAndSaveTelemetry(app::WideToUtf8(state.active_game_exe));
    env.RestoreCpuAll();
    env.SetDynamicPrioritySeparation(0x02);
    if (state.timer_boosted && !config.timer_forced_manual) {
        env.RestoreDefaultTimer();
        state.timer_boosted = false;
    }
    state.game_active = false;
    state.active_game_pid = 0;
    state.active_game_creation_time = 0;
    state.active_game_exe.clear();
    state.active_game_api.clear();
    state.active_game_hwnd = nullptr;
    state.active_game_path.clear();
    state.active_game_anticheat = false;
}

} // namespace

SystemHardwareProfile BuildHardwareProfileFromSnapshot(
    const RawDeviceSnapshot& snap,
    bool experimental_egpu_streaming,
    uint8_t user_gpu_pref) {

    SystemHardwareProfile hw{};
    hw.total_physical_ram_bytes = snap.total_physical_ram_bytes;
    hw.ram_probe_ok = snap.ram_probe_ok;
    hw.gpu_probe_ok = snap.gpu_probe_ok;
    hw.cpu_probe_ok = snap.cpu_probe_ok;
    hw.dxcore_available = snap.dxcore_available;
    hw.all_gpus = snap.gpus;
    hw.cpu_topology = snap.cpu_topology;
    hw.cpu_family = snap.cpu_family;
    hw.cpu_model = snap.cpu_model;
    hw.user_gpu_pref = user_gpu_pref;
    hw.power_status = snap.power_status;
    hw.system_arch = snap.system_arch;
    hw.windows_version = snap.windows_version;
    hw.displays = snap.displays;
    hw.adapter_signature = snap.adapter_signature;
    hw.experimental_egpu_streaming = experimental_egpu_streaming;

    if (!hw.cpu_topology.all_cpu_sets.empty() &&
        (hw.cpu_topology.game_optimal_cpu_set_ids.empty() || hw.cpu_topology.background_cpu_set_ids.empty())) {
        HardwareProfiler::PartitionCpuSetsFromTopology(hw.cpu_topology);
    }

    std::vector<std::string> names;
    std::vector<VfGpuAdapterInput> ffi_adapters;
    names.reserve(hw.all_gpus.size());
    ffi_adapters.reserve(hw.all_gpus.size());
    for (const auto& g : hw.all_gpus) {
        names.push_back(WideToUtf8Session(g.description));
        VfGpuAdapterInput a{};
        a.vendor_id = g.vendor_id;
        a.device_id = g.device_id;
        a.dedicated_vram_mb = g.dedicated_vram_bytes / (1024ULL * 1024ULL);
        a.shared_sys_mb = g.shared_system_bytes / (1024ULL * 1024ULL);
        a.dxcore_available = g.dxcore_available;
        a.dxcore_integrated = g.dxcore_integrated;
        a.dxcore_detachable = g.dxcore_detachable;
        a.removal_detachable = g.removal_policy_detachable;
        a.name_ptr = names.back().empty() ? nullptr : reinterpret_cast<const uint8_t*>(names.back().data());
        a.name_len = names.back().size();
        ffi_adapters.push_back(a);
    }

    VfDeviceProfileInput input{};
    input.total_ram_mb = hw.total_physical_ram_bytes / (1024ULL * 1024ULL);
    input.physical_cores = hw.cpu_topology.physical_cores;
    input.cpu_family = hw.cpu_family;
    input.cpu_model = hw.cpu_model;
    input.user_gpu_pref = hw.user_gpu_pref;
    input.system_arch = SystemArchToFfiByte(hw.system_arch);
    input.power_known = hw.power_status.known;
    input.has_battery = hw.power_status.has_battery;
    input.ac_online = hw.power_status.ac_online;
    input.battery_saver = hw.power_status.battery_saver;
    input.dxcore_available = hw.dxcore_available;
    input.ram_probe_ok = hw.ram_probe_ok;
    input.experimental_egpu_streaming = hw.experimental_egpu_streaming;
    input.override_arch_gen = 0;

    VfDeviceProfileDecision dec{};
    vf_rust_evaluate_device_profile(
        ffi_adapters.empty() ? nullptr : ffi_adapters.data(),
        ffi_adapters.size(),
        &input,
        &dec);

    if (dec.gpu_probe_ok && dec.selected_gpu_index < hw.all_gpus.size()) {
        hw.primary_gpu = hw.all_gpus[dec.selected_gpu_index];
    }
    hw.primary_gpu.connection = static_cast<GpuConnection>(dec.gpu_connection);
    hw.primary_gpu.arch_gen = static_cast<GpuArchitectureGen>(dec.gpu_arch_gen);
    hw.primary_gpu.mem_arch = (hw.primary_gpu.connection == GpuConnection::Integrated)
        ? MemoryArchitecture::UMA_Integrated
        : (dec.gpu_connection == VF_GPU_CONN_UNKNOWN ? MemoryArchitecture::Unknown : MemoryArchitecture::Dedicated_VRAM);
    hw.apu_tier = static_cast<ApuTier>(dec.apu_tier);

    return hw;
}

bool RefreshHardwareProfileIfChanged(
    DeviceProbe& probe,
    SystemHardwareProfile& hw,
    uint8_t user_gpu_pref) {

    const uint64_t live_sig = probe.QueryAdapterSignature();
    if (live_sig == hw.adapter_signature && user_gpu_pref == hw.user_gpu_pref) {
        return false;
    }

    const GameDiskType prev_disk = hw.last_game_disk;
    const bool exp_streaming = hw.experimental_egpu_streaming;
    const auto prev_displays = hw.displays;
    RawDeviceSnapshot snap = probe.ProbeSystem(!prev_displays.empty());
    hw = BuildHardwareProfileFromSnapshot(snap, exp_streaming, user_gpu_pref);
    if (hw.displays.empty()) {
        hw.displays = prev_displays;
    }
    hw.last_game_disk = prev_disk;
    return true;
}

void RunGameSessionCycle(
    const app::AppConfig& config,
    DeviceProbe& device_probe,
    GameSessionEnvironment& env,
    SystemHardwareProfile& hw,
    GameSessionState& state,
    uint32_t* out_noisy_bg_count,
    const std::function<void(const std::string&)>& audit_cb) {

    const ProcessPolicyConfig policy = EffectivePolicy(config);

    // If the currently cached game was just excluded by the user ("Me fue mal en este juego"), restore immediately.
    if (state.game_active && !state.active_game_exe.empty() &&
        IsExeInList(policy.excluded_game_exes, state.active_game_exe)) {
        RestoreEndOfSession(config, env, state);
        RefreshHardwareProfileIfChanged(device_probe, hw, 0);
        if (out_noisy_bg_count) *out_noisy_bg_count = 0;
        return;
    }

    std::optional<DetectedGameProcess> game_opt;
    if (state.game_active && state.active_game_pid != 0 &&
        state.active_game_hwnd != nullptr && env.IsWindowValid(state.active_game_hwnd) &&
        env.IsProcessAlive(state.active_game_pid)) {
        DetectedGameProcess cached{};
        cached.pid = state.active_game_pid;
        cached.creation_time = state.active_game_creation_time;
        cached.exe_name = state.active_game_exe;
        cached.full_path = state.active_game_path;
        if (state.active_game_api == "DirectX 12") cached.api = RenderApi::DirectX12;
        else if (state.active_game_api == "Vulkan") cached.api = RenderApi::Vulkan;
        else if (state.active_game_api == "DirectX 9/10") cached.api = RenderApi::DirectX9_10;
        else cached.api = RenderApi::DirectX11;
        cached.main_window = state.active_game_hwnd;
        cached.presenter_thread_id = static_cast<uint32_t>(GetWindowThreadProcessId(state.active_game_hwnd, nullptr));
        cached.generic_anticheat_detected = state.active_game_anticheat;
        game_opt = cached;
    } else {
        game_opt = env.DetectActiveGame(policy);
    }

    if (game_opt.has_value()) {
        const auto& game = *game_opt;
        const bool is_new_game = (!state.game_active || state.active_game_pid != game.pid ||
                                  (game.creation_time != 0 && state.active_game_creation_time != 0 &&
                                   game.creation_time != state.active_game_creation_time));

        if (is_new_game && state.game_active && state.active_game_pid != 0) {
            // Game switched directly from A to B: restore A before applying B.
            RestoreEndOfSession(config, env, state);
        }

        state.active_game_hwnd = game.main_window;
        state.active_game_exe = game.exe_name;
        state.active_game_path = game.full_path;
        state.active_game_creation_time = game.creation_time;
        switch (game.api) {
            case RenderApi::DirectX12:   state.active_game_api = "DirectX 12"; break;
            case RenderApi::Vulkan:      state.active_game_api = "Vulkan"; break;
            case RenderApi::DirectX9_10: state.active_game_api = "DirectX 9/10"; break;
            default:                     state.active_game_api = "DirectX 11"; break;
        }

        const uint8_t app_gpu_pref = device_probe.QueryAppGpuPreference(game.full_path);
        const bool hw_changed = RefreshHardwareProfileIfChanged(device_probe, hw, app_gpu_pref);

        if (is_new_game) {
            hw.last_game_disk = device_probe.ProbeDiskType(game.full_path);
        }

        uint64_t live_vram_budget = 0;
        uint64_t live_vram_usage = 0;
        bool live_vram_valid = false;
        env.QueryLiveMemory(live_vram_budget, live_vram_usage, live_vram_valid);
        (void)live_vram_usage;

        const ApuOptimizationPolicy static_policy = HardwareProfiler::BuildOptimizationPolicy(hw);
        const ApuOptimizationPolicy live_policy = HardwareProfiler::BuildOptimizationPolicy(
            hw, live_vram_valid ? live_vram_budget : 0);

        if (hw_changed && audit_cb) {
            audit_cb("[GPU] Cambio de adaptador o preferencia detectado: GPU=" +
                     WideToUtf8Session(hw.primary_gpu.description) + " [" +
                     HardwareProfiler::ConnectionToString(hw.primary_gpu.connection) + "]");
        }

        if (is_new_game) {
            if (audit_cb) {
                audit_cb("Juego detectado: " + app::WideToUtf8(game.exe_name) +
                         " (PID: " + std::to_string(game.pid) + ") [" + state.active_game_api + "]");
            }

            if (!state.timer_boosted) {
                if (env.EnableHighPrecisionTimer(config.timer_resolution_ns100)) {
                    state.timer_boosted = true;
                }
            }

            const bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
            env.SetDynamicPrioritySeparation(is_dx12 ? 0x26 : 0x28);
            env.SetGameDvrAndFseOptimized(true);
            if (static_policy.force_high_perf_power_plan) {
                env.SetApuLaptopPowerProfile(true, static_policy);
            }

            std::filesystem::path pExe(game.full_path);
            auto inspected = env.InspectGameDirectory(
                pExe.parent_path().wstring(),
                0,
                app::WideToUtf8(game.exe_name));
            if (inspected.primary_exe_path.empty()) {
                inspected.primary_exe_path = game.full_path;
            }

            const bool anticheat_protected = game.generic_anticheat_detected || inspected.has_anticheat;
            state.active_game_anticheat = anticheat_protected;

            if (anticheat_protected) {
                if (audit_cb) {
                    audit_cb("[SECURITY] Anti-Cheat detectado (firma o señal genérica). Perfil Pasivo Seguro activado: DXGI Flip Model + Aislamiento CPU (0 archivos modificados).");
                }
                env.ApplyDxgiFlipModelUpgrade(game.full_path);
            } else if (inspected.engine == GameEngineType::UnrealEngine4_5) {
                env.ApplyDxgiFlipModelUpgrade(game.full_path);
                const uint32_t pool_mb = static_policy.safe_vram_pool_mb;
                const std::string async_comp_str = (!static_policy.disable_dx12_async_compute) ? "ON" : "OFF";
                std::string preset_msg;
                if (!static_policy.apply_visual_cuts) {
                    preset_msg = static_policy.apply_egpu_streaming_tweaks
                        ? "[ENGINE] Unreal Engine (eGPU) detectado. ShaderCache=ON + Streaming amortizado, sin recortes visuales."
                        : "[ENGINE] Unreal Engine detectado en GPU dedicada/desconocida. ShaderCache=ON, sin recortes visuales.";
                } else {
                    preset_msg = "[ENGINE] Unreal Engine detectado. Preset UMA (" + static_policy.guard_mode_name +
                        ") aplicado (Pool " + std::to_string(pool_mb) + " MB, LimitVRAM=0, AsyncCompute=" + async_comp_str + ").";
                }
                if (!inspected.engine_preset_applied) {
                    env.ApplyEngineUmaPreset(inspected, static_policy, pool_mb);
                }
                if (audit_cb) {
                    audit_cb(preset_msg);
                }
            } else {
                env.ApplyDxgiFlipModelUpgrade(game.full_path);
                if (audit_cb) {
                    audit_cb("[OPTIMIZE] Juego nativo detectado. DXGI Flip Model y prioridad de renderizador aplicados.");
                }
            }

            env.StartTelemetry(game.pid);
            state.game_active = true;
            state.active_game_pid = game.pid;
        }

        auto bg_targets = env.ClassifyBackgroundProcesses(game.pid, policy);
        if (out_noisy_bg_count) {
            *out_noisy_bg_count = static_cast<uint32_t>(bg_targets.size());
        }
        const bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
        const std::vector<ULONG> no_sets;
        uint32_t throttled = 0;
        env.ApplyCpuOptimizations(
            game.pid,
            game.creation_time,
            game.presenter_thread_id,
            is_dx12,
            config.restrict_game_cpu_sets ? hw.cpu_topology.game_optimal_cpu_set_ids : no_sets,
            hw.cpu_topology.background_cpu_set_ids,
            bg_targets,
            throttled);

        if (is_new_game && audit_cb) {
            audit_cb("[CPU] EcoQoS activado en procesos secundarios (" +
                     std::to_string(throttled) + " procesos en modo eficiencia térmica para ahorro APU).");
            audit_cb("[I/O NT] Jerarquía I/O activada: IoPriorityHigh en juego (PID " +
                     std::to_string(game.pid) + ") y IoPriorityVeryLow en procesos frenados.");
        }

        std::vector<uint32_t> throttle_pids;
        throttle_pids.reserve(bg_targets.size());
        for (const auto& t : bg_targets) {
            if (t.action == ProcessAction::Throttle) {
                throttle_pids.push_back(t.pid);
            }
        }
        if (static_policy.allow_ram_purge) {
            env.EvaluateMemory(game.pid, throttle_pids, true);
        }
        if (live_policy.apply_uma_memory_guard) {
            env.EvaluateUmaGuard(live_policy, throttle_pids);
        }
    } else if (state.game_active) {
        if (state.active_game_pid != 0 && env.IsProcessAlive(state.active_game_pid)) {
            RefreshHardwareProfileIfChanged(device_probe, hw, hw.user_gpu_pref);
            return;
        }
        RestoreEndOfSession(config, env, state);
        RefreshHardwareProfileIfChanged(device_probe, hw, 0);
    } else {
        RefreshHardwareProfileIfChanged(device_probe, hw, 0);
    }
}

bool ExcludeGameAndRevert(
    app::ConfigStore& config_store,
    app::AppConfig& config,
    const std::wstring& exe_name,
    GameSessionEnvironment* env,
    CpuOrchestrator* cpu,
    GameSessionState* state,
    ChangeJournal* journal,
    std::string* out_why) {

    std::wstring clean_name = std::filesystem::path(exe_name).filename().wstring();
    const std::string utf8_name = app::WideToUtf8(clean_name);
    if (!vf_rust_is_allowed_game_exe(utf8_name.data(), utf8_name.size())) {
        if (out_why) *out_why = "El nombre del ejecutable no es válido para exclusión.";
        return false;
    }

    const bool already_excluded = IsExeInList(config.process_policy.excluded_game_exes, clean_name);
    if (!already_excluded) {
        config.process_policy.excluded_game_exes.push_back(clean_name);
        if (!config_store.Save(config, false, out_why)) {
            config.process_policy.excluded_game_exes.pop_back();
            return false;
        }
    }

    if (journal) {
        journal->RecordAuditEvent(
            "game:" + utf8_name,
            "Juego excluido por el usuario (Me fue mal en este juego)",
            "Se excluyó " + utf8_name + " de futuras optimizaciones y se revirtió la sesión.");
    }

    if (state && state->game_active && EqualsIgnoreCase(state->active_game_exe, clean_name)) {
        if (env) {
            RestoreEndOfSession(config, *env, *state);
        } else {
            if (cpu) {
                cpu->RestoreAll(false);
            }
            state->game_active = false;
            state->active_game_pid = 0;
            state->active_game_creation_time = 0;
            state->active_game_exe.clear();
            state->active_game_api.clear();
            state->active_game_hwnd = nullptr;
            state->active_game_path.clear();
            state->active_game_anticheat = false;
        }
    } else if (!state && env) {
        env->RestoreCpuAll();
    } else if (!state && cpu) {
        cpu->RestoreAll(false);
    }

    return true;
}

} // namespace vegaflow::session
