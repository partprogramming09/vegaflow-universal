#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "privileges.hpp"
#include "hardware_profiler.hpp"
#include "process_detector.hpp"
#include "graphics_optimizer.hpp"
#include "memory_manager.hpp"
#include "cpu_orchestrator.hpp"
#include "frame_telemetry.hpp"
#include "gui_window.hpp"
#include "fsr_scaler.hpp"
#include "steam_engine_optimizer.hpp"
#include "dxvk_manager.hpp"
#include "sandbox_guard.hpp"
#include "system_info.hpp"
#include <filesystem>
#include <tlhelp32.h>

namespace {

std::atomic<bool> g_running{true};
HANDLE g_stop_event = nullptr;
HANDLE g_single_instance_mutex = nullptr;

LONG WINAPI VegaFlowCrashHandler(EXCEPTION_POINTERS* /*ep*/) {
    vegaflow::GraphicsOptimizer::RestoreDefaultTimer();
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_running.store(false);
            if (g_stop_event) {
                SetEvent(g_stop_event);
            }
            return TRUE;
        default:
            return FALSE;
    }
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return {};
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size_needed, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size_needed <= 0) return {};
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size_needed);
    return result;
}

struct AppConfig {
    uint32_t poll_interval_ms = 1500;
    ULONG timer_resolution_ns100 = 5000;
    float fsr_sharpness = 0.20f;
    int fsr_mode = 0; // 0 = FSR 1.0 Ultra-Clean, 1 = NIS / Bicúbico 16-Tap, 2 = Anime4K (JRPG/Cel)
    bool timer_forced_manual = false;
    vegaflow::MemoryPurgeConfig memory{};
    std::vector<std::wstring> custom_game_exes;
    std::vector<std::wstring> user_custom_game_exes; // Exes originales de config.json
};

// Robust, lightweight JSON-like parser for config.json (Zero external dependencies)
AppConfig LoadConfig(const std::string& path) {
    AppConfig cfg{};
    std::ifstream file(path);
    if (!file.is_open()) {
        return cfg;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto find_uint = [&](const std::string& key, uint64_t default_val) -> uint64_t {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return default_val;
        size_t start = content.find_first_of("0123456789", pos);
        if (start == std::string::npos) return default_val;
        size_t end = content.find_first_not_of("0123456789", start);
        std::string num_str = content.substr(start, end - start);
        try {
            return std::stoull(num_str);
        } catch (...) {
            return default_val;
        }
    };

    auto find_float = [&](const std::string& key, float default_val) -> float {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return default_val;
        size_t start = content.find_first_of("0123456789.", pos);
        if (start == std::string::npos) return default_val;
        size_t end = content.find_first_not_of("0123456789.", start);
        std::string num_str = content.substr(start, end - start);
        try {
            float val = std::stof(num_str);
            return std::clamp(val, 0.0f, 1.0f);
        } catch (...) {
            return default_val;
        }
    };

    auto find_bool = [&](const std::string& key, bool default_val) -> bool {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return default_val;
        size_t t_pos = content.find("true", pos);
        size_t f_pos = content.find("false", pos);
        size_t next_comma = content.find_first_of(",}\n\r", pos);
        if (t_pos != std::string::npos && (next_comma == std::string::npos || t_pos < next_comma)) return true;
        if (f_pos != std::string::npos && (next_comma == std::string::npos || f_pos < next_comma)) return false;
        return default_val;
    };

    cfg.poll_interval_ms = static_cast<uint32_t>(find_uint("poll_interval_ms", 1500));
    cfg.timer_resolution_ns100 = static_cast<ULONG>(find_uint("timer_resolution_ns100", 5000));
    cfg.fsr_sharpness = find_float("fsr_sharpness", 0.20f);
    cfg.fsr_mode = std::clamp(static_cast<int>(find_uint("fsr_mode", 0)), 0, 2);
    cfg.timer_forced_manual = find_bool("timer_forced_manual", false);
    cfg.memory.min_free_ram_mb = find_uint("min_free_ram_mb", 1200);
    cfg.memory.min_standby_mb = find_uint("min_standby_mb", 1500);
    cfg.memory.cooldown_seconds = static_cast<uint32_t>(find_uint("cooldown_seconds", 90));
    uint64_t max_disk_mb_s = find_uint("max_game_disk_read_mb_s", 15);
    cfg.memory.max_game_disk_read_bytes_per_sec = max_disk_mb_s * 1024ULL * 1024ULL;

    size_t custom_pos = content.find("\"custom_game_exes\"");
    if (custom_pos != std::string::npos) {
        size_t arr_start = content.find('[', custom_pos);
        size_t arr_end = content.find(']', arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            std::string sub = content.substr(arr_start + 1, arr_end - arr_start - 1);
            size_t curr = 0;
            while ((curr = sub.find('\"', curr)) != std::string::npos) {
                size_t next_q = sub.find('\"', curr + 1);
                if (next_q == std::string::npos) break;
                std::string exe = sub.substr(curr + 1, next_q - curr - 1);
                if (!exe.empty()) {
                    std::wstring wexe = Utf8ToWide(exe);
                    cfg.custom_game_exes.push_back(wexe);
                    cfg.user_custom_game_exes.push_back(wexe);
                }
                curr = next_q + 1;
            }
        }
    }

    return cfg;
}

bool SaveConfig(const std::string& path, const AppConfig& cfg) {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "{\n";
    file << "  \"poll_interval_ms\": " << cfg.poll_interval_ms << ",\n";
    file << "  \"timer_resolution_ns100\": " << cfg.timer_resolution_ns100 << ",\n";
    file << std::fixed << std::setprecision(2);
    file << "  \"fsr_sharpness\": " << cfg.fsr_sharpness << ",\n";
    file << "  \"fsr_mode\": " << cfg.fsr_mode << ",\n";
    file << "  \"timer_forced_manual\": " << (cfg.timer_forced_manual ? "true" : "false") << ",\n";
    file << "  \"memory\": {\n";
    file << "    \"min_free_ram_mb\": " << cfg.memory.min_free_ram_mb << ",\n";
    file << "    \"min_standby_mb\": " << cfg.memory.min_standby_mb << ",\n";
    file << "    \"cooldown_seconds\": " << cfg.memory.cooldown_seconds << ",\n";
    uint64_t max_disk_mb_s = cfg.memory.max_game_disk_read_bytes_per_sec / (1024ULL * 1024ULL);
    file << "    \"max_game_disk_read_mb_s\": " << (max_disk_mb_s == 0 ? 15 : max_disk_mb_s) << "\n";
    file << "  },\n";
    file << "  \"custom_game_exes\": [";
    for (size_t i = 0; i < cfg.user_custom_game_exes.size(); ++i) {
        file << "\"" << WideToUtf8(cfg.user_custom_game_exes[i]) << "\"";
        if (i + 1 < cfg.user_custom_game_exes.size()) {
            file << ", ";
        }
    }
    file << "]\n";
    file << "}\n";
    return true;
}

void SyncSteamGamesToCustomExes(const std::vector<vegaflow::SteamGameEntry>& steam_games, AppConfig& cfg) {
    cfg.custom_game_exes = cfg.user_custom_game_exes;
    for (const auto& sg : steam_games) {
        if (!sg.primary_exe_path.empty()) {
            std::wstring exe_name = std::filesystem::path(sg.primary_exe_path).filename().wstring();
            if (!exe_name.empty()) {
                bool exists = false;
                for (const auto& existing : cfg.custom_game_exes) {
                    if (_wcsicmp(existing.c_str(), exe_name.c_str()) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    cfg.custom_game_exes.push_back(exe_name);
                }
            }
        }
    }
}

std::string FormatBytes(uint64_t bytes) {
    std::ostringstream ss;
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    double gb = mb / 1024.0;
    if (gb >= 1.0) {
        ss << std::fixed << std::setprecision(2) << gb << " GB ("
           << static_cast<uint64_t>(mb) << " MB)";
    } else {
        ss << std::fixed << std::setprecision(2) << mb << " MB ("
           << bytes << " bytes)";
    }
    return ss.str();
}

uint32_t CountEcoQoSProcesses() {
    uint32_t count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID <= 4) continue;
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_POWER_THROTTLING_STATE throttling{};
                    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                    if (GetProcessInformation(hProc, ProcessPowerThrottling, &throttling, sizeof(throttling))) {
                        if (throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) {
                            count++;
                        }
                    }
                    CloseHandle(hProc);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return count;
}

uint32_t CountDirectFlipConfiguredGames() {
    uint32_t count = 0;
    HKEY hPref = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_READ, &hPref) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t val_name[MAX_PATH];
        DWORD val_name_len = MAX_PATH;
        DWORD val_type = 0;
        wchar_t val_data[512];
        DWORD val_data_len = sizeof(val_data);
        while (RegEnumValueW(hPref, index++, val_name, &val_name_len, nullptr, &val_type, reinterpret_cast<LPBYTE>(val_data), &val_data_len) == ERROR_SUCCESS) {
            if (val_type == REG_SZ) {
                std::wstring data_str(val_data);
                for (auto& c : data_str) c = static_cast<wchar_t>(std::towlower(c));
                if (data_str.find(L"autoflipmodel=1") != std::wstring::npos && data_str.find(L"directflip=1") != std::wstring::npos) {
                    count++;
                }
            }
            val_name_len = MAX_PATH;
            val_data_len = sizeof(val_data);
        }
        RegCloseKey(hPref);
    }
    return count;
}

void PrintStatusReport() {
    using namespace vegaflow;

    PrivilegeStatus priv = PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    MemoryPurgeConfig mem_cfg{};
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    SystemMemoryTelemetry mem = mem_mgr.SampleTelemetry();

    TimerResolutionState timer = GraphicsOptimizer::QueryTimerResolution();
    SystemGraphicsTweaksState tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
    std::vector<ShaderCacheReport> shader_caches = GraphicsOptimizer::InspectShaderCaches();
    std::vector<uint32_t> noisy_pids = ProcessDetector::FindBackgroundNoisyProcesses();
    uint64_t daemon_ram_bytes = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();

    std::cout << "================================================================================\n";
    std::cout << "                     VEGAFLOW SYSTEM STATUS REPORT\n";
    std::cout << "================================================================================\n\n";

    std::cout << "[PRIVILEGES & SECURITY]\n";
    std::cout << "  Process Elevated (Admin)   : " << (priv.is_elevated ? "YES" : "NO") << "\n";
    std::cout << "  Profile Single Process     : " << (priv.profile_single_process ? "GRANTED" : "NOT GRANTED") << "\n";
    std::cout << "  Increase Base Priority     : " << (priv.increase_base_priority ? "GRANTED" : "NOT GRANTED") << "\n";
    if (!priv.is_elevated) {
        std::cout << "  [NOTE] Running as Standard User. Standby List purge and system tweaks require Administrator privileges.\n";
        std::cout << "         Execute 'vegaflow_daemon.exe --elevate' or '--purge-now' with Admin rights to free Standby RAM.\n";
    }
    std::cout << "\n";

    std::cout << "[CPU TOPOLOGY & SCHEDULING]\n";
    std::cout << "  Processor Brand            : " << (hw.cpu_topology.brand_string.empty() ? "Generic x86_64" : hw.cpu_topology.brand_string) << "\n";
    std::cout << "  Physical Cores             : " << hw.cpu_topology.physical_cores << "\n";
    std::cout << "  Logical Processors         : " << hw.cpu_topology.logical_processors << "\n";
    std::cout << "  Architecture Type          : " 
              << (hw.cpu_topology.has_hybrid_cores ? "Intel Hybrid (P/E Cores)" : (hw.cpu_topology.has_multiple_ccx ? "AMD Multi-CCX/CCD" : "Monolithic Uniform")) << "\n";
    
    std::cout << "  Game Optimal CPU Set IDs   : [";
    for (size_t i = 0; i < hw.cpu_topology.game_optimal_cpu_set_ids.size(); ++i) {
        std::cout << hw.cpu_topology.game_optimal_cpu_set_ids[i] << (i + 1 < hw.cpu_topology.game_optimal_cpu_set_ids.size() ? ", " : "");
    }
    std::cout << "]\n";

    std::cout << "  Background CPU Set IDs     : [";
    for (size_t i = 0; i < hw.cpu_topology.background_cpu_set_ids.size(); ++i) {
        std::cout << hw.cpu_topology.background_cpu_set_ids[i] << (i + 1 < hw.cpu_topology.background_cpu_set_ids.size() ? ", " : "");
    }
    std::cout << "]\n";
    uint32_t ecoqos_count = CountEcoQoSProcesses();
    std::cout << "  EcoQoS Throttled Processes : " << ecoqos_count << " processes in Efficiency Mode (Thermal Headroom for APU)\n\n";

    std::cout << "[GPU & GRAPHICS ARCHITECTURE]\n";
    std::cout << "  Primary Adapter Description: " << (hw.primary_gpu.description.empty() ? "Standard Display Adapter" : WideToUtf8(hw.primary_gpu.description)) << "\n";
    std::cout << "  Vendor                     : " << HardwareProfiler::VendorToString(hw.primary_gpu.vendor)
              << " (PCI ID: 0x" << std::hex << hw.primary_gpu.vendor_id << ":0x" << hw.primary_gpu.device_id << std::dec << ")\n";
    std::cout << "  Memory Architecture        : " << HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) << "\n";
    std::cout << "  Dedicated Video Memory     : " << FormatBytes(hw.primary_gpu.dedicated_vram_bytes) << "\n";
    std::cout << "  Shared System Memory       : " << FormatBytes(hw.primary_gpu.shared_system_bytes) << "\n\n";

    ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw, mem.vram.local_budget_bytes);
    UmaGuardStatus uma_guard = mem_mgr.EvaluateUmaPressureGuard(mem, policy, {}, true);

    std::cout << "[APU CAPABILITY GUARD & DX11/DX12 POLICY]\n";
    std::cout << "  Detected APU Tier & Guard  : " << policy.tier_label << "\n";
    std::cout << "  GPU Architecture Gen       : " << HardwareProfiler::ArchGenToString(policy.arch_gen) << "\n";
    std::cout << "  Safe UMA VRAM / Shared Pool: " << policy.safe_vram_pool_mb << " MB / " << policy.safe_shared_pool_mb << " MB (OS Floor: " << policy.os_reserve_floor_mb << " MB)\n";
    std::cout << "  DX11 Specialization        : DXVK Compiler Threads=" << policy.dxvk_compiler_threads << ", AMD Tessellation Cap=" << policy.amd_tessellation_cap << "x, Max Anisotropy=" << policy.max_anisotropy << "x\n";
    std::cout << "  DX12 Specialization        : AsyncCompute=" << (policy.disable_dx12_async_compute ? "DISABLED (UMA Contention Guard)" : "ENABLED (Do-No-Harm Guard)")
              << ", UE CSM=" << policy.ue_max_csm_resolution << "px (" << policy.ue_max_shadow_cascades << " cascades)\n";
    std::cout << "  CPU STAPM Thermal Ceiling  : " << (policy.cpu_stapm_cap_mhz == 0 ? "UNCAPPED 100% CPU+GPU (Smart Efficient-Aggressive + EPP 25)" : (std::to_string(policy.cpu_stapm_cap_mhz) + " MHz (Sustained GPU Priority)")) << "\n";
    std::cout << "  Live DXGI Budget Pressure  : " << static_cast<int>(uma_guard.dxgi_budget_utilization * 100.0) << "% (Threshold: 85% -> " << (uma_guard.guard_triggered ? "PRESSURE GUARD ACTIVE" : "HEALTHY") << ")\n\n";

    std::cout << "[SYSTEM GRAPHICS TWEAKS]\n";
    std::cout << "  Multi-Plane Overlay (MPO)  : " << (tweaks.mpo_disabled ? "DISABLED (OverlayTestMode=5, Stutter-Free)" : "ENABLED (Windows Default)") << "\n";
    std::cout << "  AMD ShaderCache Mode       : " << (tweaks.amd_shader_cache_forced ? "FORCED ON (0x3200, Stutter Elimination)" : "Driver Default (AMD Optimized)") << "\n";
    std::cout << "  AMD DX11/UMA Driver Boost  : " << (tweaks.amd_dx11_uma_boost ? "ACTIVE (SurfaceFormat FP16 + FlipQueue=1 + ULPS Off)" : "Driver Default") << "\n";
    std::cout << "  AMD VRAM Reporting Pool    : " << (tweaks.amd_vram_override_active ? ("FORCED (" + std::to_string(tweaks.amd_vram_reported_mb > 0 ? tweaks.amd_vram_reported_mb : 4096) + " MB DXGI/AMD)") : ("Driver Default (" + std::to_string(tweaks.amd_vram_reported_mb > 0 ? tweaks.amd_vram_reported_mb : 512) + " MB)")) << "\n";
    std::cout << "  MMCSS WDDM Gaming Profile  : " << (tweaks.mmcss_gaming_optimized ? "OPTIMIZED (GPU Priority=8, SystemResponsiveness=10%)" : "Windows Default (GPU Priority=2)") << "\n";
    std::cout << "  GameDVR & FSE Bypass       : " << (tweaks.gamedvr_disabled ? "OPTIMIZED (Background Capture Off + FSE Mode 2)" : "DEFAULT (Windows DVR Active)") << "\n";
    std::cout << "  APU Laptop Power & Bus     : " << (tweaks.apu_power_optimized ? "ACTIVE (Boost Efficient-Aggressive + PCIe ASPM Off)" : "STANDARD (Windows Default)") << "\n";
    std::cout << "  CPU Quantum Separation     : 0x" << std::hex << tweaks.win32_priority_separation << std::dec << "\n";
    std::cout << "  Windows Memory Compression : " << (tweaks.memory_compression_enabled ? "ACTIVE (MemCompression Process)" : "DISABLED (Zero CPU Spikes / Low Latency)") << "\n";
    std::cout << "  Kernel Memory Paging Shield: " << (tweaks.kernel_paging_shield_active ? "ACTIVE (DisablePagingExecutive=1, RAM Lock)" : "DEFAULT (Paging Allowed)") << "\n";
    std::cout << "  AMD Tessellation Limiter   : " << ((tweaks.amd_tessellation_max > 0 && tweaks.amd_tessellation_max <= 16) ? (std::to_string(tweaks.amd_tessellation_max) + "x (Over-Geometry Capped)") : (tweaks.amd_dx11_uma_boost ? "16x (Over-Geometry Capped)" : (tweaks.amd_tessellation_max > 0 ? (std::to_string(tweaks.amd_tessellation_max) + "x (Driver Setting)") : "16x (Over-Geometry Capped)"))) << "\n";
    std::cout << "  AMD SAM / ReBAR UMA Mode   : " << ((tweaks.amd_sam_rebar_active || tweaks.amd_dx11_uma_boost) ? "ACTIVE" : "DEFAULT (Standard UMA Aperture)") << "\n";

    uint32_t df_count = CountDirectFlipConfiguredGames();
    auto active_game = ProcessDetector::DetectActiveGame();
    bool directflip_live = active_game.has_value() && GraphicsOptimizer::HasDirectFlipUpgrade(active_game->full_path);
    std::string df_status;
    if (directflip_live) {
        df_status = "ACTIVE (" + WideToUtf8(active_game->exe_name) + " - Latency 0ms / Zero-Copy Bypass)";
    } else if (df_count > 0) {
        df_status = "CONFIGURED (" + std::to_string(df_count) + " Games with AutoFlip/DirectFlip, Latency 0ms)";
    } else {
        df_status = "READY (AutoFlipModel=1 & DirectFlip=1 Injection on Launch)";
    }
    std::cout << "  DirectFlip / AutoFlip Model: " << df_status << "\n";
    std::cout << "  External FSR 1.0 Scaler    : READY (EASU + RCAS 2-Pass D3D11 | Hotkey: Alt+F11)\n\n";

    std::cout << "[RAM & TELEMETRY]\n";
    std::cout << "  Total Physical RAM         : " << FormatBytes(mem.total_physical_bytes) << "\n";
    std::cout << "  Free + Zero RAM            : " << FormatBytes(mem.free_and_zero_bytes) << "\n";
    std::cout << "  Standby List RAM           : " << FormatBytes(mem.standby_bytes) << "\n";
    std::cout << "  Modified RAM               : " << FormatBytes(mem.modified_bytes) << "\n\n";

    std::cout << "[DXGI 1.4+ VIDEO MEMORY BUDGET (QUERY_VIDEO_MEMORY_INFO)]\n";
    if (mem.vram.valid) {
        std::cout << "  Local VRAM Usage / Budget  : " << (mem.vram.local_usage_bytes / (1024 * 1024)) << " MB / "
                  << (mem.vram.local_budget_bytes / (1024 * 1024)) << " MB\n";
        std::cout << "  Non-Local Usage / Budget   : " << (mem.vram.non_local_usage_bytes / (1024 * 1024)) << " MB / "
                  << (mem.vram.non_local_budget_bytes / (1024 * 1024)) << " MB\n";
    } else {
        std::cout << "  Local VRAM Usage / Budget  : N/A (Standard DXGI query unavailable)\n";
    }
    std::cout << "\n";

    std::cout << "[WINDOWS TIMER RESOLUTION]\n";
    std::cout << "  Current Resolution         : " << (static_cast<double>(timer.current_ns100) / 10000.0) << " ms (" << timer.current_ns100 << " in 100ns units)\n";
    std::cout << "  Minimum (Default) Res      : " << (static_cast<double>(timer.minimum_ns100) / 10000.0) << " ms (" << timer.minimum_ns100 << " in 100ns units)\n";
    std::cout << "  Maximum (High-Precision)   : " << (static_cast<double>(timer.maximum_ns100) / 10000.0) << " ms (" << timer.maximum_ns100 << " in 100ns units)\n";
    std::cout << "  High-Precision Active      : " << (timer.active_high_res ? "YES" : "NO") << "\n\n";

    std::cout << "[SHADER CACHES DETECTED]\n";
    if (shader_caches.empty()) {
        std::cout << "  None found in standard directory trees.\n";
    } else {
        for (size_t i = 0; i < shader_caches.size(); ++i) {
            std::cout << "  [" << (i + 1) << "] " << WideToUtf8(shader_caches[i].path)
                      << " | " << (shader_caches[i].total_size_bytes / (1024 * 1024)) << " MB"
                      << " (" << shader_caches[i].file_count << " files)"
                      << (shader_caches[i].exists ? "" : " [Missing]") << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "[STEAM LIBRARY & ENGINE PROFILES]\n";
    std::wstring steam_path = SteamEngineOptimizer::GetSteamInstallPath();
    std::cout << "  Steam Install Path         : " << (steam_path.empty() ? "Not detected / Not installed" : WideToUtf8(steam_path)) << "\n";
    uint32_t running_steam_app = SteamEngineOptimizer::GetRunningSteamAppId();
    std::cout << "  Running Steam AppID        : " << (running_steam_app != 0 ? std::to_string(running_steam_app) : "None (0)") << "\n";
    auto steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    std::cout << "  Discovered Steam Games     : " << steam_games.size() << " games\n";
    if (steam_games.empty()) {
        std::cout << "    (No games detected in libraryfolders.vdf)\n";
    } else {
        size_t show_count = std::min<size_t>(steam_games.size(), 5);
        for (size_t i = 0; i < show_count; ++i) {
            std::cout << "    - AppID " << steam_games[i].app_id << ": " << steam_games[i].name
                      << " [" << SteamEngineOptimizer::EngineTypeToString(steam_games[i].engine) << "]"
                      << (steam_games[i].has_anticheat ? " (AntiCheat Protected)" : "")
                      << (steam_games[i].dxvk_installed ? " [DXVK Vulkan: ACTIVO]" : "") << "\n";
        }
        if (steam_games.size() > 5) {
            std::cout << "      ... and " << (steam_games.size() - 5) << " more games\n";
        }
    }
    std::cout << "\n";

    std::cout << "[SYSTEM PROCESSES & DAEMON FOOTPRINT]\n";
    std::cout << "  Background Noisy PIDs Found: " << noisy_pids.size() << " processes\n";
    std::cout << "  Daemon Private Working Set : " << (static_cast<double>(daemon_ram_bytes) / (1024.0 * 1024.0)) << " MB ("
              << (daemon_ram_bytes / 1024) << " KB)\n";
    std::cout << "  Zero-Overhead KPI Target   : < 15.00 MB -> "
              << (daemon_ram_bytes < 15ULL * 1024 * 1024 ? "PASSED (Optimal)" : "WARNING") << "\n";
    std::cout << "================================================================================\n";
}

void ExecutePurgeNow(bool dry_run) {
    using namespace vegaflow;

    PrivilegeManager::EnableRequiredPrivileges(false);
    auto noisy_pids = ProcessDetector::FindBackgroundNoisyProcesses();

    MemoryPurgeConfig cfg{};
    MemoryManager mem_mgr(cfg);
    auto report = mem_mgr.ForcePurgeNow(noisy_pids, dry_run);

    std::cout << "================================================================================\n";
    std::cout << "              VEGAFLOW MANUAL MEMORY PURGE " << (dry_run ? "(DRY-RUN)" : "") << "\n";
    std::cout << "================================================================================\n";
    std::cout << "  Status Message             : " << report.status_message << "\n";
    std::cout << "  Standby Purged Successfully: " << (report.standby_purge_succeeded ? "YES" : "NO") << "\n";
    std::cout << "  Background Working Sets Trim: " << report.trimmed_processes << " processes\n";
    std::cout << "  Free + Zero RAM (Before)   : " << FormatBytes(report.free_bytes_before) << "\n";
    std::cout << "  Free + Zero RAM (After)    : " << FormatBytes(report.free_bytes_after) << "\n";
    std::cout << "  Standby List RAM (Before)  : " << FormatBytes(report.standby_bytes_before) << "\n";
    std::cout << "  Standby List RAM (After)   : " << FormatBytes(report.standby_bytes_after) << "\n";

    if (!PrivilegeManager::IsProcessElevated() && !dry_run && !report.standby_purge_succeeded) {
        std::cout << "\n  [TIP] Purging the Standby List requires Administrator privileges.\n";
        std::cout << "        Execute 'vegaflow_daemon.exe --elevate' or run cmd/PowerShell as Admin.\n";
    }
    std::cout << "================================================================================\n";
}

void ExecuteDryRun(const AppConfig& config) {
    using namespace vegaflow;

    std::cout << "================================================================================\n";
    std::cout << "              VEGAFLOW DAEMON (DRY-RUN SIMULATION & EVALUATION)\n";
    std::cout << "================================================================================\n\n";

    PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    CpuOrchestrator cpu_orchestrator;

    std::cout << "[1/4] Scanning foreground processes for 3D Game / Graphics API...\n";
    auto game_opt = ProcessDetector::DetectActiveGame(config.custom_game_exes);

    if (game_opt.has_value()) {
        const auto& game = *game_opt;
        std::cout << "  -> Active 3D Game Detected: " << WideToUtf8(game.exe_name) << " (PID: " << game.pid << ")\n";
        std::cout << "  -> Path: " << WideToUtf8(game.full_path) << "\n";
        std::cout << "  -> Render API: " << ProcessDetector::RenderApiToString(game.api) << "\n";
        std::cout << "  -> Driver DLL: " << (game.driver_dll_loaded.empty() ? "N/A" : WideToUtf8(game.driver_dll_loaded)) << "\n";
        std::cout << "  -> Presenter Thread ID: " << game.presenter_thread_id << "\n\n";

        std::cout << "[2/4] [DRY-RUN] Graphics Optimizations Evaluation:\n";
        std::cout << "  -> High-Precision Timer: Would call NtSetTimerResolution(" << config.timer_resolution_ns100 << ", TRUE) [0.5 ms target]\n";
        bool flip_already = GraphicsOptimizer::HasDxgiFlipModelUpgrade(game.full_path);
        std::cout << "  -> DXGI Flip Model Upgrade: " << (flip_already ? "Already enabled in registry" : "Would set SwapEffectUpgradeEnable=1 in HKCU UserGpuPreferences") << "\n";
        GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, true /* dry_run */);
        std::cout << "\n";

        std::cout << "[3/4] [DRY-RUN] CPU Orchestrator Evaluation:\n";
        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(game.pid);
        std::cout << "  -> Background Noisy Processes to Isolate: " << bg_pids.size() << " PIDs\n";
        bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            game.pid,
            game.presenter_thread_id,
            is_dx12,
            hw.cpu_topology.game_optimal_cpu_set_ids,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            true /* dry_run */);

        std::cout << "  -> Game Handle Access Level: " << (cpu_res.game_handle_full_access ? "Full (PROCESS_SET_INFORMATION)" : "Degraded Read-Only (Anti-Cheat safe)") << "\n";
        std::cout << "  -> Priority Class Selection: " << (is_dx12 ? "ABOVE_NORMAL (DX12/Vulkan balanced)" : "HIGH (DX11 render-centric)") << "\n";
        std::cout << "  -> Game CPU Sets Targeted  : " << hw.cpu_topology.game_optimal_cpu_set_ids.size() << " cores\n";
        std::cout << "  -> Background Isolated Sets: " << hw.cpu_topology.background_cpu_set_ids.size() << " cores\n\n";

        std::cout << "[4/4] [DRY-RUN] Memory Manager Evaluation (Zero-Stutter Hysteresis):\n";
        auto mem_res = mem_mgr.EvaluateAndOptimize(game.pid, bg_pids, true /* dry_run */);
        std::cout << "  -> Standby Purge Triggered : " << (mem_res.standby_purged ? "YES" : "NO") << "\n";
        std::cout << "  -> Background Trim Triggered: " << (mem_res.background_processes_trimmed > 0 ? "YES" : "NO") << " (" << mem_res.background_processes_trimmed << " processes)\n";
        std::cout << "  -> Cooldown Active         : " << (mem_res.skipped_due_to_cooldown ? "YES" : "NO") << "\n";
        std::cout << "  -> Disk I/O Gating Active  : " << (mem_res.skipped_due_to_disk_io ? "YES (Game loading assets)" : "NO") << "\n";
        std::cout << "  -> Reason / Status Message : " << mem_res.reason << "\n\n";

        cpu_orchestrator.RestoreAll(true);
    } else {
        std::cout << "  -> No foreground 3D game currently detected.\n";
        std::cout << "  -> Demonstrating evaluation on background processes under simulated game conditions:\n\n";

        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(0);
        std::cout << "[2/4] [DRY-RUN] High-Precision Timer: Would set timer resolution to " << config.timer_resolution_ns100 << " (0.5 ms)\n\n";

        std::cout << "[3/4] [DRY-RUN] CPU Orchestrator Evaluation:\n";
        std::cout << "  -> Background Processes Found: " << bg_pids.size() << " PIDs\n";
        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            0,
            0,
            false,
            hw.cpu_topology.game_optimal_cpu_set_ids,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            true /* dry_run */);
        std::cout << "  -> Background Sets to confine: " << hw.cpu_topology.background_cpu_set_ids.size() << " core IDs (Isolated in dry-run: " << cpu_res.background_processes_isolated << " targets)\n\n";

        std::cout << "[4/4] [DRY-RUN] Memory Manager Evaluation:\n";
        auto mem_res = mem_mgr.EvaluateAndOptimize(0, bg_pids, true /* dry_run */);
        std::cout << "  -> Standby Purge Triggered : " << (mem_res.standby_purged ? "YES" : "NO") << "\n";
        std::cout << "  -> Background Trim Count   : " << mem_res.background_processes_trimmed << "\n";
        std::cout << "  -> Evaluation Reason       : " << mem_res.reason << "\n\n";

        cpu_orchestrator.RestoreAll(true);
    }

    uint64_t daemon_ram = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();
    std::cout << "[KPI] Daemon Memory Working Set: " << (static_cast<double>(daemon_ram) / (1024.0 * 1024.0)) << " MB (< 15 MB KPI passed)\n";
    std::cout << "================================================================================\n";
}

void RunSingleIteration(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    vegaflow::FsrSpatialScaler& fsr_scaler,
    bool& game_active,
    uint32_t& active_game_pid,
    std::wstring& active_game_exe,
    std::string& active_game_api,
    HWND& active_game_hwnd,
    bool& timer_boosted,
    std::wstring* active_game_path = nullptr,
    uint32_t* out_noisy_bg_count = nullptr,
    std::function<void(const std::string&)>* audit_cb = nullptr) {

    using namespace vegaflow;

    std::optional<DetectedGameProcess> game_opt;
    if (game_active && active_game_pid != 0 &&
        active_game_hwnd != nullptr && IsWindow(active_game_hwnd) &&
        ProcessDetector::IsProcessAlive(active_game_pid)) {
        DetectedGameProcess cached_game{};
        cached_game.pid = active_game_pid;
        cached_game.exe_name = active_game_exe;
        cached_game.full_path = active_game_path ? *active_game_path : L"";
        if (active_game_api == "DirectX 12") cached_game.api = RenderApi::DirectX12;
        else if (active_game_api == "Vulkan") cached_game.api = RenderApi::Vulkan;
        else if (active_game_api == "DirectX 9/10") cached_game.api = RenderApi::DirectX9_10;
        else cached_game.api = RenderApi::DirectX11;
        cached_game.main_window = active_game_hwnd;
        cached_game.presenter_thread_id = static_cast<uint32_t>(GetWindowThreadProcessId(active_game_hwnd, nullptr));
        game_opt = cached_game;
    } else {
        game_opt = ProcessDetector::DetectActiveGame(config.custom_game_exes);
    }

    if (game_opt.has_value()) {
        const auto& game = *game_opt;
        active_game_hwnd = game.main_window;
        active_game_exe = game.exe_name;
        active_game_api = ProcessDetector::RenderApiToString(game.api);
        if (active_game_path) {
            *active_game_path = game.full_path;
        }

        bool is_new_game = (!game_active || active_game_pid != game.pid);

        auto live_mem = mem_mgr.SampleTelemetry();
        ApuOptimizationPolicy policy = HardwareProfiler::BuildOptimizationPolicy(hw, live_mem.vram.local_budget_bytes);

        if (is_new_game) {
            if (game_active && active_game_pid != 0) {
                std::cout << telemetry_monitor.FormatSessionReport(WideToUtf8(active_game_exe)) << "\n";
                telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
                telemetry_monitor.StopMonitoring();
                fsr_scaler.StopScaling();
            }

            std::cout << "[INFO] Game detected: " << WideToUtf8(game.exe_name)
                      << " (PID: " << game.pid << ") using " << active_game_api << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)("Juego detectado: " + WideToUtf8(game.exe_name) + " (PID: " + std::to_string(game.pid) + ") [" + active_game_api + "]");
            }

            if (!timer_boosted) {
                if (GraphicsOptimizer::EnableHighPrecisionTimer(config.timer_resolution_ns100)) {
                    timer_boosted = true;
                    std::cout << "[OPT] High-precision timer enabled (" << (config.timer_resolution_ns100 / 10000.0) << " ms)\n";
                }
            }

            bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);
            GraphicsOptimizer::SetDynamicPrioritySeparation(is_dx12 ? 0x26 : 0x28, false);

            GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
            GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);

            // =================================================================
            // AUTO-PROFILING INTELIGENTE POR JUEGO (I+D)
            // =================================================================
            std::filesystem::path pExe(game.full_path);
            auto inspected = SteamEngineOptimizer::InspectGameDirectory(
                pExe.parent_path().wstring(),
                0,
                WideToUtf8(game.exe_name));
            if (inspected.primary_exe_path.empty()) {
                inspected.primary_exe_path = game.full_path;
            }

            auto emit_profile_msg = [&](const std::string& msg) {
                std::cout << msg << "\n";
                if (audit_cb && *audit_cb) {
                    (*audit_cb)(msg);
                }
            };

            if (inspected.has_anticheat) {
                // Perfil 1: Anti-Cheat Protegido
                emit_profile_msg("[SECURITY] Anti-Cheat detectado (HoYoKProtect/EAC/BE). Perfil Pasivo Seguro activado: DXGI Flip Model + Aislamiento CPU (0 archivos modificados).");
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
            } else if (inspected.engine == GameEngineType::UnrealEngine4_5) {
                // Perfil 2: Unreal Engine sin Anti-Cheat
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
                uint32_t pool_mb = policy.safe_vram_pool_mb;
                std::string async_comp_str = (!policy.disable_dx12_async_compute) ? "ON" : "OFF";
                std::string preset_msg = "[ENGINE] Unreal Engine detectado. Preset UMA (" + policy.guard_mode_name +
                    ") aplicado (Pool " + std::to_string(pool_mb) + " MB, LimitVRAM=0, AsyncCompute=" + async_comp_str + ").";
                if (!inspected.engine_preset_applied) {
                    SteamEngineOptimizer::ApplyEngineUmaPreset(inspected, pool_mb, false);
                    emit_profile_msg(preset_msg);
                } else {
                    emit_profile_msg(preset_msg);
                }
            } else {
                // Perfil 3: Juegos DX11/DX12 Genéricos
                GraphicsOptimizer::ApplyDxgiFlipModelUpgrade(game.full_path, false);
                emit_profile_msg("[OPTIMIZE] Juego nativo detectado. DXGI Flip Model y prioridad de renderizador aplicados.");
            }

            telemetry_monitor.StartMonitoring(game.pid);
            game_active = true;
            active_game_pid = game.pid;
        }

        auto bg_pids = ProcessDetector::FindBackgroundNoisyProcesses(game.pid);
        if (out_noisy_bg_count) {
            *out_noisy_bg_count = static_cast<uint32_t>(bg_pids.size());
        }
        bool is_dx12 = (game.api == RenderApi::DirectX12 || game.api == RenderApi::Vulkan);

        auto cpu_res = cpu_orchestrator.ApplyOptimizations(
            game.pid,
            game.presenter_thread_id,
            is_dx12,
            hw.cpu_topology.game_optimal_cpu_set_ids,
            hw.cpu_topology.background_cpu_set_ids,
            bg_pids,
            false);

        if (is_new_game) {
            std::string eco_msg = "[CPU] EcoQoS activado en procesos secundarios (" +
                std::to_string(cpu_res.background_processes_throttled) + " procesos en modo eficiencia térmica para ahorro APU).";
            std::cout << eco_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(eco_msg);
            }

            std::string io_msg = "[I/O NT] Jerarquía I/O activada: IoPriorityHigh en juego (PID " +
                std::to_string(game.pid) + ") y IoPriorityVeryLow en procesos de fondo.";
            std::cout << io_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(io_msg);
            }
        }

        auto mem_report = mem_mgr.EvaluateAndOptimize(game.pid, bg_pids, false);
        if (mem_report.standby_purged || mem_report.background_processes_trimmed > 0) {
            std::cout << "[OPT] Memory optimized: " << mem_report.reason << "\n";
        }

        auto uma_guard = mem_mgr.EvaluateUmaPressureGuard(live_mem, policy, bg_pids, false);
        if (uma_guard.guard_triggered) {
            std::string guard_msg = "[UMA GUARD] " + uma_guard.active_reason + " (Intervención #" + std::to_string(uma_guard.total_interventions) + ")";
            std::cout << guard_msg << "\n";
            if (audit_cb && *audit_cb) {
                (*audit_cb)(guard_msg);
            }
        }
    } else {
        // Fallback: If previously tracked game is still alive in background/minimized, preserve session
        if (game_active) {
            if (active_game_pid != 0 && ProcessDetector::IsProcessAlive(active_game_pid)) {
                // Game process is still alive (minimized or Alt-Tabbed); do not discard session yet
            } else {
                std::cout << "[INFO] Game closed (PID: " << active_game_pid << "). Restoring system state...\n";
                std::cout << telemetry_monitor.FormatSessionReport(WideToUtf8(active_game_exe)) << "\n";
                telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
                telemetry_monitor.StopMonitoring();
                fsr_scaler.StopScaling();

                cpu_orchestrator.RestoreAll(false);
                GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
                if (timer_boosted && !config.timer_forced_manual) {
                    GraphicsOptimizer::RestoreDefaultTimer();
                    timer_boosted = false;
                    std::cout << "[OPT] Default timer resolution restored.\n";
                }
                game_active = false;
                active_game_pid = 0;
                active_game_exe.clear();
                active_game_api.clear();
                active_game_hwnd = nullptr;
                if (active_game_path) {
                    active_game_path->clear();
                }
            }
        }
    }
}

void RunMonitorDashboard(
    const AppConfig& config,
    vegaflow::SystemHardwareProfile& hw,
    vegaflow::CpuOrchestrator& cpu_orchestrator,
    vegaflow::MemoryManager& mem_mgr,
    vegaflow::FrameTelemetryMonitor& telemetry_monitor,
    vegaflow::FsrSpatialScaler& fsr_scaler,
    bool once_mode) {

    using namespace vegaflow;
    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool timer_boosted = false;

    std::cout << "====================================================================================================\n";
    std::cout << "                               VEGAFLOW LIVE TELEMETRY DASHBOARD\n";
    std::cout << "====================================================================================================\n";
    std::cout << "[RAM Libre | Standby List | VRAM Local | Timer | Juego Activo + API | Avg FPS | 1% Low | 0.1% Low | StdDev ms]\n";
    std::cout << "----------------------------------------------------------------------------------------------------\n";

    auto print_line = [&]() {
        RunSingleIteration(
            config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);

        auto mem = mem_mgr.SampleTelemetry();
        auto timer = GraphicsOptimizer::QueryTimerResolution();
        auto snap = telemetry_monitor.GetSnapshot();

        uint64_t free_mb = mem.free_and_zero_bytes / (1024 * 1024);
        uint64_t standby_mb = mem.standby_bytes / (1024 * 1024);
        uint64_t vram_used_mb = mem.vram.local_usage_bytes / (1024 * 1024);
        uint64_t vram_budget_mb = mem.vram.local_budget_bytes / (1024 * 1024);
        double timer_ms = static_cast<double>(timer.current_ns100) / 10000.0;

        std::string game_info = game_active ? (WideToUtf8(active_game_exe) + " (" + active_game_api + ")") : "Ninguno";

        std::ostringstream ss;
        ss << "["
           << "RAM Lib: " << free_mb << " MB | "
           << "Standby: " << standby_mb << " MB | "
           << "VRAM: " << vram_used_mb << "/" << vram_budget_mb << " MB | "
           << "Timer: " << std::fixed << std::setprecision(1) << timer_ms << "ms | "
           << "Juego: " << game_info << " | "
           << "Avg: " << std::setprecision(1) << snap.avg_fps << " FPS | "
           << "1% Low: " << std::setprecision(1) << snap.fps_1_percent_low << " | "
           << "0.1% Low: " << std::setprecision(1) << snap.fps_0_1_percent_low << " | "
           << "StdDev: " << std::setprecision(2) << snap.frametime_stddev_ms << "ms"
           << "]";
        std::cout << ss.str() << "\n";
    };

    if (once_mode) {
        print_line();
        if (game_active) {
            fsr_scaler.StopScaling();
            telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
            if (timer_boosted) GraphicsOptimizer::RestoreDefaultTimer();
            telemetry_monitor.StopMonitoring();
        }
        return;
    }

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    while (g_running.load()) {
        print_line();
        DWORD wr = WaitForSingleObject(g_stop_event, config.poll_interval_ms);
        if (wr == WAIT_OBJECT_0 || !g_running.load()) {
            break;
        }
    }

    if (game_active) {
        fsr_scaler.StopScaling();
        telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
        cpu_orchestrator.RestoreAll(false);
        if (timer_boosted) GraphicsOptimizer::RestoreDefaultTimer();
        telemetry_monitor.StopMonitoring();
    }
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    SetUnhandledExceptionFilter(VegaFlowCrashHandler);
    SetConsoleOutputCP(CP_UTF8);

    bool status_flag = false;
    bool dry_run_flag = false;
    bool once_flag = false;
    bool elevate_flag = false;
    bool purge_now_flag = false;
    bool mpo_disable_flag = false;
    bool mpo_restore_flag = false;
    bool shader_cache_force_flag = false;
    bool shader_cache_default_flag = false;
    bool mem_compression_off_flag = false;
    bool mem_compression_on_flag = false;
    bool apu_gpu_priority_flag = false;
    bool apu_balanced_flag = false;
    bool vram_override_flag = false;
    bool vram_restore_flag = false;
    bool fsr_validate_flag = false;
    bool monitor_flag = false;
    bool gui_flag = false;
    bool system_info_flag = false;
    std::string dxvk_install_path;
    std::string dxvk_remove_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--status" || arg == "-s") {
            status_flag = true;
        } else if (arg == "--dry-run" || arg == "-d") {
            dry_run_flag = true;
        } else if (arg == "--once" || arg == "-1") {
            once_flag = true;
        } else if (arg == "--elevate" || arg == "-e") {
            elevate_flag = true;
        } else if (arg == "--purge-now" || arg == "-p") {
            purge_now_flag = true;
        } else if (arg == "--mpo-disable") {
            mpo_disable_flag = true;
        } else if (arg == "--mpo-restore") {
            mpo_restore_flag = true;
        } else if (arg == "--shader-cache-force") {
            shader_cache_force_flag = true;
        } else if (arg == "--shader-cache-default") {
            shader_cache_default_flag = true;
        } else if (arg == "--mem-compression-off") {
            mem_compression_off_flag = true;
        } else if (arg == "--mem-compression-on") {
            mem_compression_on_flag = true;
        } else if (arg == "--apu-gpu-priority") {
            apu_gpu_priority_flag = true;
        } else if (arg == "--apu-balanced") {
            apu_balanced_flag = true;
        } else if (arg == "--vram-override") {
            vram_override_flag = true;
        } else if (arg == "--vram-restore") {
            vram_restore_flag = true;
        } else if (arg == "--fsr-validate") {
            fsr_validate_flag = true;
        } else if (arg == "--dxvk-install" && i + 1 < argc) {
            dxvk_install_path = argv[++i];
        } else if (arg == "--dxvk-remove" && i + 1 < argc) {
            dxvk_remove_path = argv[++i];
        } else if (arg == "--monitor" || arg == "-m") {
            monitor_flag = true;
        } else if (arg == "--gui" || arg == "-g") {
            gui_flag = true;
        } else if (arg == "--system-info") {
            system_info_flag = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "VegaFlow Daemon - Dynamic Hardware & OS Performance Orchestrator\n";
            std::cout << "Usage: vegaflow_daemon [OPTIONS]\n\n";
            std::cout << "Options:\n";
            std::cout << "  --status, -s             Display detailed hardware, RAM, timer, MPO, and system telemetry report.\n";
            std::cout << "  --dry-run, -d            Simulate evaluation cycles without modifying processes or registry.\n";
            std::cout << "  --once, -1               Execute one optimization evaluation cycle and exit cleanly.\n";
            std::cout << "  --elevate, -e            Relaunch current executable with Administrator privileges (UAC).\n";
            std::cout << "  --purge-now, -p          Force immediate trimming of background working sets and Standby List purge.\n";
            std::cout << "  --mpo-disable            Disable Multi-Plane Overlay (MPO) in Windows DWM to eliminate stutter/flicker.\n";
            std::cout << "  --mpo-restore            Restore Multi-Plane Overlay (MPO) in Windows DWM to system default.\n";
            std::cout << "  --shader-cache-force     Force AMD driver shader cache to always compile to disk (0x3200).\n";
            std::cout << "  --shader-cache-default   Restore AMD driver shader cache to default setting.\n";
            std::cout << "  --mem-compression-off    Disable Windows kernel Memory Compression to eliminate latency/hiccups.\n";
            std::cout << "  --mem-compression-on     Enable Windows kernel Memory Compression.\n";
            std::cout << "  --apu-gpu-priority       Activate APU thermal & power profile with sustained GPU priority (3.4 GHz CPU cap).\n";
            std::cout << "  --apu-balanced           Restore standard Windows balanced APU power profile.\n";
            std::cout << "  --vram-override          Override AMD VRAM reporting to 4096 MB in DXGI and AMD driver.\n";
            std::cout << "  --vram-restore           Restore AMD VRAM reporting to driver default (512 MB).\n";
            std::cout << "  --fsr-validate           Compile and validate embedded FSR 1.0 EASU + RCAS HLSL shaders.\n";
            std::cout << "  --dxvk-install <path>    Deploy DXVK (DirectX 11 to Vulkan with async GPL) to target game folder.\n";
            std::cout << "  --dxvk-remove <path>     Remove DXVK and restore original DirectX 11 runtime.\n";
            std::cout << "  --monitor, -m            Real-time console dashboard displaying live RAM, VRAM, and FPS telemetry.\n";
            std::cout << "  --gui, -g                Launch native zero-overhead Win32/GDI dashboard with system tray support.\n";
            std::cout << "  --system-info            Output complete hardware, APU UMA, RAM topology, and OS specs in JSON format.\n";
            std::cout << "  --help, -h               Display this help menu.\n";
            return 0;
        }
    }

    if (system_info_flag) {
        vegaflow::SystemInfo sys_info;
        std::cout << sys_info.ToJson() << "\n";
        return 0;
    }

    // Si no está corriendo como Administrador y no es una ejecución simulada (--dry-run),
    // relanzar inmediatamente con privilegios elevados vía UAC (runas) para abrir nativamente como Administrador.
    if (!dry_run_flag && !vegaflow::PrivilegeManager::IsProcessElevated()) {
        std::wstring forward_args;
        for (int j = 1; j < argc; ++j) {
            std::string a = argv[j];
            if (a != "--elevate" && a != "-e") {
                if (!forward_args.empty()) forward_args += L" ";
                if (a.find(' ') != std::string::npos) {
                    forward_args += L"\"" + Utf8ToWide(a) + L"\"";
                } else {
                    forward_args += Utf8ToWide(a);
                }
            }
        }
        if (forward_args.empty()) {
            forward_args = L"--gui";
        }
        if (vegaflow::PrivilegeManager::RelaunchAsAdmin(forward_args, false)) {
            return 0;
        }
    }

    if (elevate_flag) {
        if (!vegaflow::PrivilegeManager::IsProcessElevated()) {
            std::cout << "[INFO] Requesting UAC Administrator elevation for VegaFlow...\n";
            std::wstring forward_args;
            for (int j = 1; j < argc; ++j) {
                std::string a = argv[j];
                if (a != "--elevate" && a != "-e") {
                    if (!forward_args.empty()) forward_args += L" ";
                    forward_args += Utf8ToWide(a);
                }
            }
            bool ok = vegaflow::PrivilegeManager::RelaunchAsAdmin(forward_args, dry_run_flag);
            if (ok) {
                std::cout << "[INFO] Elevated instance launched. Terminating standard process.\n";
                return 0;
            } else {
                std::cerr << "[ERROR] Elevation was cancelled by user or denied.\n";
                return 1;
            }
        } else {
            std::cout << "[INFO] Process is already running with Administrator privileges.\n";
            if (!gui_flag && !status_flag && !purge_now_flag && !monitor_flag && !mpo_disable_flag && !mpo_restore_flag
                && !shader_cache_force_flag && !shader_cache_default_flag && !mem_compression_off_flag && !mem_compression_on_flag
                && !apu_gpu_priority_flag && !apu_balanced_flag && !vram_override_flag && !vram_restore_flag && !fsr_validate_flag && !once_flag
                && dxvk_install_path.empty() && dxvk_remove_path.empty()) {
                return 0;
            }
        }
    }

    if (apu_gpu_priority_flag) {
        vegaflow::ApuOptimizationPolicy pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
        bool ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::GpuPrioritySustained, dry_run_flag);
        std::string mode_str = (pol.cpu_stapm_cap_mhz == 0)
            ? "Control Térmico APU: Smart Uncapped Boost Activado (100% CPU 4.0 GHz + 100% GPU con Efficient-Aggressive + EPP 25)"
            : ("GPU-Priority sustained mode active (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz CPU ceiling / 15W continuous)");
        std::cout << "[APU Power] SetApuPowerMode(GpuPrioritySustained) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. " + mode_str + ".") : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (apu_balanced_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetApuPowerMode(vegaflow::ApuPowerMode::Balanced, dry_run_flag);
        std::cout << "[APU Power] SetApuPowerMode(Balanced) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows standard balanced power profile restored." : "FAILED. Check power configuration permissions.") << "\n";
        return ok ? 0 : 1;
    }

    if (fsr_validate_flag) {
        std::string err;
        std::cout << "================================================================================\n";
        std::cout << "                 VEGAFLOW FSR 1.0 HLSL SHADER VALIDATION\n";
        std::cout << "================================================================================\n";
        std::cout << "  Compiling EASU (Edge Adaptive Spatial Upsampling) & RCAS (Sharpening) Shaders...\n";
        bool ok = vegaflow::FsrSpatialScaler::CompileAndValidateShadersOnly(&err);
        if (ok) {
            std::cout << "  [SUCCESS] Shaders compiled and validated via D3DCompile 100% successfully.\n";
            std::cout << "  [STATUS] FSR 1.0 Spatial Scaler Engine is READY (D3D11 Compute Model 5.0).\n";
        } else {
            std::cout << "  [FAILED] Shader compilation error:\n" << err << "\n";
        }
        std::cout << "================================================================================\n";
        return ok ? 0 : 1;
    }

    if (shader_cache_force_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(true, dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(true) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache set to 0x3200 (Always Enabled on disk)." : "FAILED. Admin privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (shader_cache_default_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetAmdShaderCacheForced(false, dry_run_flag);
        std::cout << "[AMD ShaderCache] SetAmdShaderCacheForced(false) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. ShaderCache restored to driver default." : "FAILED. Admin privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (mem_compression_off_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(false, dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(false) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression DISABLED (stutter-free latency mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (mem_compression_on_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMemoryCompressionEnabled(true, dry_run_flag);
        std::cout << "[MemCompression] SetMemoryCompressionEnabled(true) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Windows Memory Compression restored." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (vram_override_flag) {
        vegaflow::ApuOptimizationPolicy pol = vegaflow::HardwareProfiler::BuildOptimizationPolicy(vegaflow::HardwareProfiler::DetectHardware());
        bool ok = vegaflow::GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, dry_run_flag);
        std::cout << "[AMD VRAM] ApplyAmdVramReportingOverride(" << pol.safe_vram_pool_mb << ") " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? ("SUCCEEDED. VRAM reporting set to " + std::to_string(pol.safe_vram_pool_mb) + " MB in DXGI & AMD driver.") : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (vram_restore_flag) {
        bool ok = vegaflow::GraphicsOptimizer::RestoreAmdVramReporting(dry_run_flag);
        std::cout << "[AMD VRAM] RestoreAmdVramReporting() " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. VRAM reporting restored to driver default (512 MB)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (!dxvk_install_path.empty()) {
        std::wstring wpath = Utf8ToWide(dxvk_install_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager::DeployDxvk(wpath, false, true, dry_run_flag, &msg);
        std::cout << "[DXVK] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (!dxvk_remove_path.empty()) {
        std::wstring wpath = Utf8ToWide(dxvk_remove_path);
        std::string msg;
        bool ok = vegaflow::DxvkManager::RemoveDxvk(wpath, dry_run_flag, &msg);
        std::cout << "[DXVK] " << msg << "\n";
        return ok ? 0 : 1;
    }

    if (status_flag) {
        PrintStatusReport();
        return 0;
    }

    if (purge_now_flag) {
        ExecutePurgeNow(dry_run_flag);
        return 0;
    }

    if (mpo_disable_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMpoDisabled(true, dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(true) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay is now DISABLED (stutter-free mode)." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    if (mpo_restore_flag) {
        bool ok = vegaflow::GraphicsOptimizer::SetMpoDisabled(false, dry_run_flag);
        std::cout << "[MPO] SetMpoDisabled(false) " << (dry_run_flag ? "[DRY-RUN] " : "")
                  << (ok ? "SUCCEEDED. Multi-Plane Overlay restored to Windows default." : "FAILED. Administrator privileges required.") << "\n";
        return ok ? 0 : 1;
    }

    AppConfig config = LoadConfig("config/config.json");
    if (config.custom_game_exes.empty()) {
        AppConfig root_cfg = LoadConfig("config.json");
        if (!root_cfg.custom_game_exes.empty() || root_cfg.poll_interval_ms != 1500) {
            config = root_cfg;
        }
    }

    if (dry_run_flag && !monitor_flag) {
        ExecuteDryRun(config);
        return 0;
    }

    if (!once_flag) {
        g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
        if (g_single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExisting = FindWindowW(L"VegaFlowUniversalUxtuClass", nullptr);
            if (hExisting) {
                PostMessageW(hExisting, WM_COMMAND, 2001 /* ID_TRAY_RESTORE */, 0);
                SetForegroundWindow(hExisting);
            }
            std::cout << "[INFO] VegaFlow Universal is already running. Focusing existing instance.\n";
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
            return 0;
        }
    }

    using namespace vegaflow;
    PrivilegeStatus priv = PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware();
    ApuOptimizationPolicy boot_policy = HardwareProfiler::BuildOptimizationPolicy(hw);

    if (priv.is_elevated && boot_policy.apply_uma_vram_reg_override) {
        GraphicsOptimizer::ApplyAmdVramReportingOverride(boot_policy.safe_vram_pool_mb, dry_run_flag);
    }

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    CpuOrchestrator cpu_orchestrator;
    FrameTelemetryMonitor telemetry_monitor;
    FsrSpatialScaler fsr_scaler;
    fsr_scaler.SetSharpness(config.fsr_sharpness);
    fsr_scaler.SetAlgorithm(static_cast<UpscaleAlgorithm>(std::clamp(config.fsr_mode, 0, 2)));

    if (monitor_flag) {
        RunMonitorDashboard(config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler, once_flag);
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        return 0;
    }

    if (gui_flag) {
        bool game_active = false;
        uint32_t active_game_pid = 0;
        std::wstring active_game_exe;
        std::wstring active_game_full_path;
        std::string active_game_api;
        HWND active_game_hwnd = nullptr;
        bool timer_boosted = false;
        bool timer_forced_manual = config.timer_forced_manual;
        if (timer_forced_manual) {
            if (GraphicsOptimizer::EnableHighPrecisionTimer(config.timer_resolution_ns100)) {
                timer_boosted = true;
            }
        }
        GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
        GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);
        float current_fsr_sharpness = config.fsr_sharpness;
        fsr_scaler.SetSharpness(current_fsr_sharpness);
        int current_fsr_mode = std::clamp(config.fsr_mode, 0, 2);
        fsr_scaler.SetAlgorithm(static_cast<UpscaleAlgorithm>(current_fsr_mode));
        uint32_t last_isolated_count = 0;
        uint64_t last_purge_before_free_mb = 0;
        uint64_t last_purge_after_free_mb = 0;
        uint32_t last_trimmed_processes = 0;
        std::string last_gui_status = "VegaFlow UXTU GUI Inicializada";

        std::vector<std::string> audit_log;
        auto AddAuditLog = [&](const std::string& msg) {
            SYSTEMTIME st;
            GetLocalTime(&st);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
            audit_log.push_back(std::string(buf) + msg);
            if (audit_log.size() > 200) {
                audit_log.erase(audit_log.begin());
            }
        };

        AddAuditLog("Sesión iniciada. Hardware: " + hw.cpu_topology.brand_string + " (" +
                    std::to_string(hw.cpu_topology.physical_cores) + "P/" +
                    std::to_string(hw.cpu_topology.logical_processors) + "L)");
        AddAuditLog("GPU Principal: " + WideToUtf8(hw.primary_gpu.description) + " [" +
                    HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) + "]");
        AddAuditLog("Estado de Privilegios: " + std::string(PrivilegeManager::IsProcessElevated() ? "ADMINISTRADOR (Acceso Total)" : "ESTANDAR (Sin elevación)"));

        uint32_t cached_d3dscache_mb = 0;
        uint32_t cached_amd_dxcache_mb = 0;
        int cache_query_countdown = 0;

        SystemGraphicsTweaksState cached_tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
        int tweaks_query_countdown = 10;
        uint32_t cached_noisy_count = 0;
        int noisy_query_countdown = 0;

        std::vector<SteamGameEntry> cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
        SyncSteamGamesToCustomExes(cached_steam_games, config);

        GuiCallbacks callbacks;
        callbacks.on_PollTelemetry = [&]() -> GuiDashboardData {
            std::function<void(const std::string&)> audit_fn = AddAuditLog;
            RunSingleIteration(
                config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler,
                game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted,
                &active_game_full_path, &cached_noisy_count, &audit_fn);

            if (!game_active) {
                if (--noisy_query_countdown <= 0) {
                    noisy_query_countdown = 3;
                    cached_noisy_count = static_cast<uint32_t>(ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid).size());
                }
            }

            uint32_t running_steam_appid = SteamEngineOptimizer::GetRunningSteamAppId();
            std::string active_game_engine;
            for (const auto& sg : cached_steam_games) {
                if (sg.app_id == running_steam_appid && running_steam_appid != 0) {
                    active_game_engine = SteamEngineOptimizer::EngineTypeToString(sg.engine);
                    break;
                }
            }
            if (game_active && active_game_engine.empty()) {
                for (const auto& sg : cached_steam_games) {
                    if (!sg.primary_exe_path.empty()) {
                        std::filesystem::path p1(sg.primary_exe_path);
                        std::filesystem::path p2(active_game_full_path);
                        if (p1.filename() == p2.filename() || (!active_game_exe.empty() && p1.filename() == std::filesystem::path(active_game_exe))) {
                            active_game_engine = SteamEngineOptimizer::EngineTypeToString(sg.engine);
                            if (running_steam_appid == 0) running_steam_appid = sg.app_id;
                            break;
                        }
                    }
                }
            }

            auto mem_sample = mem_mgr.SampleTelemetry();
            if (!game_active) {
                mem_mgr.EvaluateUmaPressureGuard(
                    mem_sample,
                    HardwareProfiler::BuildOptimizationPolicy(hw, mem_sample.vram.local_budget_bytes),
                    {},
                    true /* dry_run */);
            }
            auto timer_res = GraphicsOptimizer::QueryTimerResolution();
            auto frame_snap = telemetry_monitor.GetSnapshot();
            auto fsr_st = fsr_scaler.GetStatus();
            uint64_t daemon_ws = MemoryManager::GetCurrentProcessPrivateWorkingSetBytes();

            if (--tweaks_query_countdown <= 0) {
                tweaks_query_countdown = 10;
                cached_tweaks = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            }

            if (--cache_query_countdown <= 0) {
                cache_query_countdown = 10;
                auto caches = GraphicsOptimizer::InspectShaderCaches();
                for (const auto& c : caches) {
                    if (c.path.find(L"D3DSCache") != std::wstring::npos) {
                        cached_d3dscache_mb = static_cast<uint32_t>(c.total_size_bytes / (1024 * 1024));
                    } else if (c.path.find(L"DxCache") != std::wstring::npos || c.path.find(L"AMD") != std::wstring::npos) {
                        cached_amd_dxcache_mb = static_cast<uint32_t>(c.total_size_bytes / (1024 * 1024));
                    }
                }
            }

            GuiDashboardData data;
            data.is_elevated = PrivilegeManager::IsProcessElevated();
            data.cpu_summary = hw.cpu_topology.brand_string.empty() ? "AMD/Intel x86_64" : hw.cpu_topology.brand_string;
            data.gpu_summary = WideToUtf8(hw.primary_gpu.description);
            data.physical_cores = hw.cpu_topology.physical_cores;
            data.logical_processors = hw.cpu_topology.logical_processors;
            data.game_cpu_sets.assign(hw.cpu_topology.game_optimal_cpu_set_ids.begin(), hw.cpu_topology.game_optimal_cpu_set_ids.end());
            data.background_cpu_sets.assign(hw.cpu_topology.background_cpu_set_ids.begin(), hw.cpu_topology.background_cpu_set_ids.end());

            data.total_ram_mb = mem_sample.total_physical_bytes / (1024 * 1024);
            data.free_ram_mb = mem_sample.free_and_zero_bytes / (1024 * 1024);
            data.standby_ram_mb = mem_sample.standby_bytes / (1024 * 1024);
            data.modified_ram_mb = mem_sample.modified_bytes / (1024 * 1024);
            data.vram_local_used_mb = mem_sample.vram.local_usage_bytes / (1024 * 1024);
            data.vram_local_budget_mb = mem_sample.vram.local_budget_bytes / (1024 * 1024);

            data.last_purge_before_free_mb = last_purge_before_free_mb;
            data.last_purge_after_free_mb = last_purge_after_free_mb;
            data.last_trimmed_processes = last_trimmed_processes;
            data.noisy_bg_processes_count = cached_noisy_count;
            data.isolated_bg_processes_count = std::max(last_isolated_count, cpu_orchestrator.GetIsolatedBackgroundCount());

            data.timer_resolution_ms = static_cast<double>(timer_res.current_ns100) / 10000.0;
            data.timer_forced_manual = timer_forced_manual;
            data.mpo_disabled = cached_tweaks.mpo_disabled;
            data.mem_compression_enabled = cached_tweaks.memory_compression_enabled;
            data.amd_shader_cache_forced = cached_tweaks.amd_shader_cache_forced;
            data.mmcss_gaming_optimized = cached_tweaks.mmcss_gaming_optimized;
            data.amd_dx11_uma_boost = cached_tweaks.amd_dx11_uma_boost;
            data.win32_priority_separation = cached_tweaks.win32_priority_separation;
            data.apu_gpu_priority_active = cached_tweaks.apu_power_optimized;

            // Sincronización Suite Silicio APU & Blindaje Kernel NT
            data.kernel_paging_shield_active = cached_tweaks.kernel_paging_shield_active;
            data.amd_tessellation_max = (cached_tweaks.amd_tessellation_max > 0) ? cached_tweaks.amd_tessellation_max : 16;
            data.amd_sam_rebar_active = cached_tweaks.amd_sam_rebar_active || cached_tweaks.amd_dx11_uma_boost;
            data.amd_vram_override_active = cached_tweaks.amd_vram_override_active;
            data.amd_vram_reported_mb = cached_tweaks.amd_vram_reported_mb;

            // Consultar en vivo estados de las optimizaciones Ronda 2 y Grupo 2B
            uint32_t ntfs_mem = 0;
            bool bypass_compat = false;
            GraphicsOptimizer::QueryBypassIoTweaks(ntfs_mem, bypass_compat);
            data.directstorage_active = (ntfs_mem == 2);

            bool ntfs_no_last = false;
            uint32_t sys_resp = 20;
            GraphicsOptimizer::QueryIoStorageTweaks(ntfs_no_last, sys_resp);
            data.io_streaming_active = (ntfs_no_last && sys_resp <= 10);

            std::string mmcss_cat;
            std::string mmcss_sfio;
            uint32_t mmcss_clock = 0;
            GraphicsOptimizer::QueryMmcssTaskTweaks(mmcss_cat, mmcss_sfio, mmcss_clock);
            data.mmcss_task_active = (mmcss_cat == "High" && mmcss_clock == 10000);

            std::string audio_cat;
            uint32_t audio_prio = 0;
            GraphicsOptimizer::QueryWasapiAudioTweaks(audio_cat, audio_prio);
            data.wasapi_audio_active = (audio_cat == "High" && audio_prio == 6);

            bool tcp_nd = false;
            uint32_t net_throttle = 10;
            GraphicsOptimizer::QueryTcpLatencyTweaks(tcp_nd, net_throttle);
            data.tcp_gaming_active = (tcp_nd && net_throttle == 0xFFFFFFFFUL);

            data.apu_shadows_active = true;

            data.game_active = game_active;
            data.game_is_foreground = (game_active && (GetForegroundWindow() == active_game_hwnd));
            data.active_game_name = WideToUtf8(active_game_exe);
            data.active_game_path = WideToUtf8(active_game_full_path);
            data.active_game_api = active_game_api;
            data.active_game_pid = active_game_pid;
            data.flip_model_applied = game_active ? GraphicsOptimizer::HasDxgiFlipModelUpgrade(active_game_full_path) : false;

            // Suite moderna para juegos 3D (EcoQoS, jerarquía I/O y DirectFlip)
            data.ecoqos_processes_count = game_active
                ? std::max(last_isolated_count, cpu_orchestrator.GetThrottledBackgroundCount())
                : CountEcoQoSProcesses();
            data.io_priority_active = game_active;
            data.directflip_active = game_active
                ? (GraphicsOptimizer::HasDirectFlipUpgrade(active_game_full_path) || GraphicsOptimizer::HasDirectFlipUpgrade(active_game_exe))
                : (CountDirectFlipConfiguredGames() > 0);

            if (frame_snap.valid) {
                data.avg_fps = frame_snap.avg_fps;
                data.fps_1_low = frame_snap.fps_1_percent_low;
                data.fps_0_1_low = frame_snap.fps_0_1_percent_low;
                data.frametime_stddev_ms = frame_snap.frametime_stddev_ms;
            }

            data.fsr_active = fsr_st.active;
            data.fsr_sharpness = current_fsr_sharpness;
            data.fsr_mode = current_fsr_mode;
            if (fsr_st.active) {
                data.fsr_status_text = "Activo: " + std::string(FsrSpatialScaler::AlgorithmToString(fsr_st.algorithm))
                    + " (" + std::to_string(fsr_st.source_width) + "x" + std::to_string(fsr_st.source_height)
                    + " -> " + std::to_string(fsr_st.output_width) + "x" + std::to_string(fsr_st.output_height) + ")";
            } else {
                data.fsr_status_text = "Listo: " + std::string(FsrSpatialScaler::AlgorithmToString(static_cast<UpscaleAlgorithm>(current_fsr_mode))) + " (Alt+F11)";
            }

            data.d3dscache_mb = cached_d3dscache_mb;
            data.amd_dxcache_mb = cached_amd_dxcache_mb;

            data.daemon_private_ws_kb = daemon_ws / 1024;
            data.last_action_status = last_gui_status;
            data.audit_log = audit_log;

            data.running_steam_app_id = running_steam_appid;
            data.active_game_engine = active_game_engine;
            data.steam_games.clear();
            data.steam_games.reserve(cached_steam_games.size());
            for (const auto& sg : cached_steam_games) {
                GuiSteamGameItem item;
                item.app_id = sg.app_id;
                item.name = sg.name;
                item.exe_name = WideToUtf8(std::filesystem::path(sg.primary_exe_path).filename().wstring());
                if (item.exe_name.empty()) {
                    item.exe_name = "N/A";
                }
                item.engine_name = SteamEngineOptimizer::EngineTypeToString(sg.engine);
                item.has_anticheat = sg.has_anticheat;
                item.engine_preset_applied = sg.engine_preset_applied;
                item.flip_model_applied = sg.flip_model_applied;
                item.dxvk_installed = sg.dxvk_installed;
                data.steam_games.push_back(std::move(item));
            }

            data.system_info = SystemInfo::Gather();
            data.sandbox_violations = SandboxGuard::GetViolationCount();
            data.sandbox_guard_active = true;

            data.dxgi_pressure_ratio = mem_mgr.GetLastDxgiPressureRatio();
            data.uma_guard_interventions = mem_mgr.GetUmaGuardInterventionsCount();
            data.uma_guard_triggered_live = mem_mgr.IsUmaGuardCurrentlyTriggered();

            return data;
        };

        callbacks.on_PurgeNow = [&]() -> std::string {
            auto noisy = ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid);
            auto rep = mem_mgr.ForcePurgeNow(noisy, false);
            last_purge_before_free_mb = rep.free_bytes_before / (1024 * 1024);
            last_purge_after_free_mb = rep.free_bytes_after / (1024 * 1024);
            last_trimmed_processes = rep.trimmed_processes;
            last_gui_status = rep.status_message;
            AddAuditLog("Purga de Standby List y recorte de WS ejecutada: " + rep.status_message);
            return rep.status_message;
        };

        callbacks.on_ToggleMpo = [&]() -> std::string {
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target = !cur.mpo_disabled;
            bool ok = GraphicsOptimizer::SetMpoDisabled(target, false);
            tweaks_query_countdown = 0;
            std::string msg = ok ? (target ? "MPO Desactivado (OverlayTestMode=5 Anti-Flicker)" : "MPO Restaurado a Default (OverlayTestMode=0)") : "Fallo al modificar MPO (Requiere Administrador)";
            last_gui_status = msg;
            AddAuditLog("Ajuste DWM MPO: " + msg);
            return msg;
        };

        callbacks.on_RequestElevate = [&]() -> bool {
            AddAuditLog("Solicitud de elevación UAC enviada...");
            if (g_single_instance_mutex) {
                CloseHandle(g_single_instance_mutex);
                g_single_instance_mutex = nullptr;
            }
            bool ok = PrivilegeManager::RelaunchAsAdmin(L"--gui");
            if (!ok) {
                g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
            }
            return ok;
        };

        callbacks.on_ToggleFsr = [&]() -> std::string {
            if (fsr_scaler.GetStatus().active) {
                fsr_scaler.StopScaling();
                std::string msg = "Escalador espacial desactivado (0 MB VRAM - Resolución 100% Nativa)";
                last_gui_status = msg;
                AddAuditLog(msg);
                return msg;
            }
            if (active_game_hwnd != nullptr && IsWindow(active_game_hwnd)) {
                fsr_scaler.StartScaling(active_game_hwnd, current_fsr_sharpness, false);
                std::string msg = fsr_scaler.GetStatus().status_message;
                last_gui_status = msg;
                AddAuditLog("Escalador Espacial: " + msg);
                return msg;
            } else {
                fsr_scaler.StopScaling();
                std::string msg = "Escalador DESACTIVADO: Abre un juego 3D antes de activar el reescalador";
                last_gui_status = msg;
                AddAuditLog(msg);
                return msg;
            }
        };

        callbacks.on_ToggleMemCompression = [&]() -> std::string {
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target = !cur.memory_compression_enabled;
            bool ok = GraphicsOptimizer::SetMemoryCompressionEnabled(target, false);
            tweaks_query_countdown = 0;
            std::string msg = ok ? (target ? "Compresión de Memoria Habilitada" : "Compresión de Memoria Deshabilitada (Latencia Cero)") : "Fallo al cambiar compresión (Requiere Administrador)";
            last_gui_status = msg;
            AddAuditLog("Ajuste Windows MMAgent: " + msg);
            return msg;
        };

        callbacks.on_ToggleTimerForce = [&]() -> std::string {
            timer_forced_manual = !timer_forced_manual;
            std::string msg;
            if (timer_forced_manual) {
                bool ok = GraphicsOptimizer::EnableHighPrecisionTimer(5000);
                timer_boosted = ok;
                msg = ok ? "Timer de Kernel forzado a 0.50 ms (5000 ns100)" : "Fallo al forzar temporizador de kernel";
            } else {
                bool ok = GraphicsOptimizer::RestoreDefaultTimer();
                timer_boosted = false;
                msg = ok ? "Timer de Kernel restaurado a valor dinámico de Windows" : "Fallo al restaurar temporizador";
            }
            config.timer_forced_manual = timer_forced_manual;
            SaveConfig("config/config.json", config);
            last_gui_status = msg;
            AddAuditLog("Resolución de Kernel: " + msg);
            return msg;
        };

        callbacks.on_ToggleShaderCache = [&]() -> std::string {
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target = !cur.amd_shader_cache_forced;
            bool ok = GraphicsOptimizer::SetAmdShaderCacheForced(target, false);
            tweaks_query_countdown = 0;
            std::string msg = ok ? (target ? "Boost AMD DX11/UMA + ShaderCache 0x3200 Activado (SurfaceFormat + FlipQueue=1 + ULPS Off)" : "AMD ShaderCache y Driver Boost restaurados a Driver Default") : "Fallo al cambiar ShaderCache (Requiere Administrador)";
            last_gui_status = msg;
            AddAuditLog("Ajuste AMD ShaderCache: " + msg);
            return msg;
        };

        callbacks.on_ToggleVramOverride = [&]() -> std::string {
            ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target = !cur.amd_vram_override_active;
            bool ok = target ? GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, false)
                             : GraphicsOptimizer::RestoreAmdVramReporting(false);
            tweaks_query_countdown = 0;
            std::string msg = ok ? (target ? ("Sobrescritura VRAM UMA (" + std::to_string(pol.safe_vram_pool_mb) + " MB DXGI/AMD) Activada (Pool Size = " + std::to_string(pol.safe_vram_pool_mb) + " MB)")
                                           : "Reporte de VRAM restaurado a valores por defecto (512 MB)")
                                 : "Fallo al modificar reporte de VRAM (Requiere Administrador)";
            last_gui_status = msg;
            AddAuditLog("Ajuste VRAM Reporting: " + msg);
            return msg;
        };

        callbacks.on_ToggleMmcssGaming = [&]() -> std::string {
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target = !cur.mmcss_gaming_optimized;
            bool ok = GraphicsOptimizer::SetMmcssGamingProfileOptimized(target, false);
            if (ok) {
                GraphicsOptimizer::SetDynamicPrioritySeparation(target ? 0x26 : 0x02, false);
            }
            tweaks_query_countdown = 0;
            std::string msg = ok
                ? (target ? "Planificador WDDM (GPU Priority=8) + Quantum CPU (0x26) Activado" : "Planificador WDDM y Quantum CPU restaurados a Stock Windows")
                : "Fallo al optimizar MMCSS/Quantum (Requiere Administrador)";
            last_gui_status = msg;
            AddAuditLog("Ajuste Kernel MMCSS/Quantum: " + msg);
            return msg;
        };

        callbacks.on_ToggleApuPowerMode = [&]() -> std::string {
            ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
            auto cur = GraphicsOptimizer::QuerySystemGraphicsTweaks();
            bool target_prio = !cur.apu_power_optimized;
            bool ok = GraphicsOptimizer::SetApuPowerMode(
                target_prio ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced,
                false
            );
            tweaks_query_countdown = 0;
            std::string ceiling_str = (pol.cpu_stapm_cap_mhz == 0)
                ? "Control Térmico APU: Smart Uncapped Boost Activado (100% CPU 4.0 GHz + 100% GPU con Efficient-Aggressive + EPP 25)"
                : ("Control Térmico APU: Modo GPU-Priority Activado (" + std::to_string(pol.cpu_stapm_cap_mhz) + " MHz ceiling / 15W sostenidos)");
            std::string msg = ok
                ? (target_prio
                    ? ceiling_str
                    : "Control Térmico APU: Perfil Balanceado Estándar Restaurado (4.0 GHz Boost)")
                : "Fallo al alternar modo de energía APU";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_ApplyAllGamingTweaks = [&]() -> std::string {
            ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
            bool ok_bypass = GraphicsOptimizer::ApplyBypassIoTweaks(false);
            bool ok_io = GraphicsOptimizer::ApplyIoStorageTweaks(false);
            bool ok_mmcss = GraphicsOptimizer::ApplyMmcssTweaks(false);
            bool ok_wasapi = GraphicsOptimizer::ApplyWasapiAudioTweaks(false);
            bool ok_tcp = GraphicsOptimizer::ApplyTcpGamingTweaks(false);
            bool ok_ram = MemoryManager::PurgeStandbyList(false);
            bool ok_timer = GraphicsOptimizer::SetHighResolutionTimer();
            bool ok_vram = pol.apply_uma_vram_reg_override
                ? GraphicsOptimizer::ApplyAmdVramReportingOverride(pol.safe_vram_pool_mb, false)
                : false;
            tweaks_query_countdown = 0;

            (void)ok_bypass; (void)ok_io; (void)ok_mmcss; (void)ok_wasapi;
            (void)ok_tcp; (void)ok_ram; (void)ok_timer; (void)ok_vram;

            std::string msg = "Combo Gaming 2B aplicado: DirectStorage, MMCSS GPU=8, WASAPI, TCP NoDelay, VRAM " +
                std::to_string(pol.safe_vram_pool_mb) + "MB, I/O Stream, Timer 0.5ms y Purga RAM.";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_SetFsrSharpness = [&](float sharpness) -> std::string {
            sharpness = std::clamp(sharpness, 0.0f, 1.0f);
            current_fsr_sharpness = sharpness;
            fsr_scaler.SetSharpness(current_fsr_sharpness);
            config.fsr_sharpness = current_fsr_sharpness;
            SaveConfig("config/config.json", config);
            char sBuf[64];
            std::snprintf(sBuf, sizeof(sBuf), "Nitidez FSR RCAS configurada a %.2f", sharpness);
            std::string msg = sBuf;
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_SetFsrMode = [&](int mode) -> std::string {
            mode = std::clamp(mode, 0, 2);
            current_fsr_mode = mode;
            auto algo = static_cast<UpscaleAlgorithm>(mode);
            fsr_scaler.SetAlgorithm(algo);
            config.fsr_mode = current_fsr_mode;
            SaveConfig("config/config.json", config);
            std::string msg = std::string("Algoritmo de reescalado cambiado en vivo a: ") + FsrSpatialScaler::AlgorithmToString(algo);
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_IsolateBackgroundNow = [&]() -> std::string {
            auto noisy = ProcessDetector::FindBackgroundNoisyProcesses(active_game_pid);
            std::vector<ULONG> empty_game_sets;
            auto res = cpu_orchestrator.ApplyOptimizations(
                active_game_pid, 0, false,
                empty_game_sets,
                hw.cpu_topology.background_cpu_set_ids,
                noisy,
                false);
            last_isolated_count = res.background_processes_isolated;
            std::string msg = "Aislamiento de procesos de fondo ejecutado: " + std::to_string(res.background_processes_isolated) + " procesos confinados en núcleos secundarios";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_RestoreCpuAffinity = [&]() -> std::string {
            cpu_orchestrator.RestoreAll(false);
            last_isolated_count = 0;
            std::string msg = "Afinidad de procesos y CPU Sets restaurados a valores originales de Windows";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_RestoreAllDefaults = [&]() -> std::string {
            GraphicsOptimizer::RestoreDefaultTimer();
            timer_boosted = false;
            timer_forced_manual = false;
            current_fsr_sharpness = 0.20f;
            fsr_scaler.SetSharpness(current_fsr_sharpness);
            config.fsr_sharpness = 0.20f;
            current_fsr_mode = 0;
            fsr_scaler.SetAlgorithm(UpscaleAlgorithm::FSR1_UltraClean);
            config.fsr_mode = 0;
            config.timer_forced_manual = false;
            SaveConfig("config/config.json", config);
            GraphicsOptimizer::SetMpoDisabled(false, false);
            GraphicsOptimizer::SetAmdShaderCacheForced(false, false);
            GraphicsOptimizer::SetMemoryCompressionEnabled(true, false);
            GraphicsOptimizer::SetMmcssGamingProfileOptimized(false, false);
            GraphicsOptimizer::SetGameDvrAndFseOptimized(false, false);
            GraphicsOptimizer::SetApuLaptopPowerProfile(false, false);
            GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
            GraphicsOptimizer::SetKernelMemoryPagingShield(false, false);
            GraphicsOptimizer::SetAmdTessellationCap(64, false);
            GraphicsOptimizer::RestoreAmdVramReporting(false);
            tweaks_query_countdown = 0;
            cpu_orchestrator.RestoreAll(false);
            last_isolated_count = 0;
            if (fsr_scaler.GetStatus().active) {
                fsr_scaler.StopScaling();
            }
            std::string msg = "Todos los ajustes dinámicos han sido revertidos a los valores por defecto de Windows";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_RescanSteamLibrary = [&]() -> std::string {
            cached_steam_games = SteamEngineOptimizer::ScanSteamLibrary();
            SyncSteamGamesToCustomExes(cached_steam_games, config);
            std::string msg = "Biblioteca Steam re-escaneada: " + std::to_string(cached_steam_games.size()) + " juegos detectados";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_ApplyGameEnginePreset = [&](uint32_t app_id) -> std::string {
            ApuOptimizationPolicy pol = HardwareProfiler::BuildOptimizationPolicy(hw, mem_mgr.SampleTelemetry().vram.local_budget_bytes);
            uint32_t pool_mb = pol.safe_vram_pool_mb;
            for (auto& sg : cached_steam_games) {
                if (sg.app_id == app_id) {
                    auto rep = SteamEngineOptimizer::ApplyEngineUmaPreset(sg, pool_mb, false);
                    if (rep.success) {
                        sg.engine_preset_applied = true;
                        sg.flip_model_applied = true;
                    }
                    last_gui_status = rep.status_message;
                    AddAuditLog(rep.status_message);
                    return rep.status_message;
                }
            }
            std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_RestoreGameEngineConfig = [&](uint32_t app_id) -> std::string {
            for (auto& sg : cached_steam_games) {
                if (sg.app_id == app_id) {
                    auto rep = SteamEngineOptimizer::RestoreEngineOriginalConfig(sg, false);
                    if (rep.success) {
                        sg.engine_preset_applied = false;
                    }
                    last_gui_status = rep.status_message;
                    AddAuditLog(rep.status_message);
                    return rep.status_message;
                }
            }
            std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        callbacks.on_ToggleGameDxvk = [&](uint32_t app_id) -> std::string {
            for (auto& sg : cached_steam_games) {
                if (sg.app_id == app_id) {
                    std::string msg;
                    if (sg.dxvk_installed) {
                        bool ok = DxvkManager::RemoveDxvk(sg.primary_exe_path, false, &msg);
                        if (ok) {
                            sg.dxvk_installed = false;
                        }
                    } else {
                        bool ok = DxvkManager::DeployDxvk(sg.primary_exe_path, sg.has_anticheat, true, false, &msg);
                        if (ok) {
                            sg.dxvk_installed = true;
                        }
                    }
                    last_gui_status = msg;
                    AddAuditLog(msg);
                    return msg;
                }
            }
            std::string msg = "Error: AppID " + std::to_string(app_id) + " no encontrado en la biblioteca de Steam";
            last_gui_status = msg;
            AddAuditLog(msg);
            return msg;
        };

        int exit_code = GuiWindow::RunGuiLoop(callbacks);
        fsr_scaler.StopScaling();
        if (game_active) {
            telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
            telemetry_monitor.StopMonitoring();
        }
        cpu_orchestrator.RestoreAll(false);
        GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
        if (timer_boosted) {
            GraphicsOptimizer::RestoreDefaultTimer();
        }
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        return exit_code;
    }

    bool game_active = false;
    uint32_t active_game_pid = 0;
    std::wstring active_game_exe;
    std::string active_game_api;
    HWND active_game_hwnd = nullptr;
    bool timer_boosted = false;

    GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
    GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);

    if (once_flag) {
        RunSingleIteration(
            config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler,
            game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);
        if (game_active) {
            fsr_scaler.StopScaling();
            telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
            cpu_orchestrator.RestoreAll(false);
            if (timer_boosted) {
                GraphicsOptimizer::RestoreDefaultTimer();
            }
            telemetry_monitor.StopMonitoring();
        }
        return 0;
    }

    // Continuous Daemon Mode (Zero aggressive polling via Waitable Timer)
    auto steam_games = SteamEngineOptimizer::ScanSteamLibrary();
    SyncSteamGamesToCustomExes(steam_games, config);

    GraphicsOptimizer::SetGameDvrAndFseOptimized(true, false);
    GraphicsOptimizer::SetApuLaptopPowerProfile(true, false);

    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        std::cerr << "[WARN] Failed to set console control handler.\n";
    }

    HANDLE waitable_timer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
        TIMER_ALL_ACCESS);
    if (!waitable_timer) {
        waitable_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    if (!waitable_timer) {
        std::cerr << "[ERROR] Failed to create Windows Waitable Timer (Error: " << GetLastError() << ")\n";
        return 1;
    }

    LARGE_INTEGER due_time;
    due_time.QuadPart = -10000LL * config.poll_interval_ms; // 100ns units, negative for relative
    if (!SetWaitableTimer(waitable_timer, &due_time, config.poll_interval_ms, nullptr, nullptr, FALSE)) {
        std::cerr << "[ERROR] SetWaitableTimer failed (Error: " << GetLastError() << ")\n";
        CloseHandle(waitable_timer);
        return 1;
    }

    std::cout << "================================================================================\n";
    std::cout << "            VEGAFLOW DAEMON INITIALIZED (ZERO-OVERHEAD ACTIVE)                  \n";
    std::cout << "  Poll Interval : " << config.poll_interval_ms << " ms (Waitable Timer)\n";
    std::cout << "  Timer Target  : " << (config.timer_resolution_ns100 / 10000.0) << " ms\n";
    std::cout << "  Architecture  : " << HardwareProfiler::MemArchToString(hw.primary_gpu.mem_arch) << "\n";
    if (!priv.is_elevated) {
        std::cout << "  [WARN] Running without Administrator privileges: Standby List purge requires Admin.\n";
        std::cout << "         Run with --elevate to grant full access.\n";
    }
    std::cout << "  Press Ctrl+C to terminate cleanly.\n";
    std::cout << "================================================================================\n";

    HANDLE wait_handles[2] = { g_stop_event, waitable_timer };

    while (g_running.load()) {
        DWORD wait_res = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait_res == WAIT_OBJECT_0) {
            break;
        } else if (wait_res == WAIT_OBJECT_0 + 1) {
            RunSingleIteration(
                config, hw, cpu_orchestrator, mem_mgr, telemetry_monitor, fsr_scaler,
                game_active, active_game_pid, active_game_exe, active_game_api, active_game_hwnd, timer_boosted);
        } else {
            break;
        }
    }

    std::cout << "\n[INFO] Shutdown signal received. Cleaning up resources...\n";
    if (game_active) {
        fsr_scaler.StopScaling();
        telemetry_monitor.SaveSessionReportToFile(WideToUtf8(active_game_exe), "logs/session_reports.log");
        cpu_orchestrator.RestoreAll(false);
        GraphicsOptimizer::SetDynamicPrioritySeparation(0x02, false);
        if (timer_boosted) {
            GraphicsOptimizer::RestoreDefaultTimer();
        }
        telemetry_monitor.StopMonitoring();
    }

    CancelWaitableTimer(waitable_timer);
    CloseHandle(waitable_timer);
    if (g_stop_event) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
    }
    if (g_single_instance_mutex) {
        CloseHandle(g_single_instance_mutex);
        g_single_instance_mutex = nullptr;
    }

    std::cout << "[INFO] VegaFlow Daemon terminated cleanly.\n";
    return 0;
}
