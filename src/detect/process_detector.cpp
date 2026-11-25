#include "detect/process_detector.hpp"

#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <set>
#include <vector>

namespace vegaflow {

namespace {

std::wstring ToLower(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t ch : str) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

std::wstring ExtractFileName(const std::wstring& path) {
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

std::string WideToUtf8Local(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

uint64_t FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

bool QueryProcessImageAndTime(uint32_t pid, std::wstring& out_full_path, uint64_t& out_creation_time) {
    out_full_path.clear();
    out_creation_time = 0;
    if (pid == 0 || pid == 4) return false;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    wchar_t path_buffer[MAX_PATH * 4] = {0};
    DWORD size = static_cast<DWORD>(std::size(path_buffer));
    if (QueryFullProcessImageNameW(hProcess, 0, path_buffer, &size)) {
        out_full_path.assign(path_buffer, size);
    }

    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(hProcess, &created, &exited, &kernel, &user)) {
        out_creation_time = FileTimeToU64(created);
    }

    CloseHandle(hProcess);
    return true;
}

bool HasActiveAntiCheatService() {
    static const wchar_t* const kAcServices[] = {
        L"EasyAntiCheat",
        L"EasyAntiCheat_EOS",
        L"BEService",
        L"BEDaisy",
        L"vgc",
        L"HoYoKProtect",
        L"mhyprot2",
        L"AntiCheatExpert",
        L"SGuard64",
        L"npggsvc",
    };

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    bool found = false;
    for (const wchar_t* svc_name : kAcServices) {
        SC_HANDLE svc = OpenServiceW(scm, svc_name, SERVICE_QUERY_STATUS);
        if (svc) {
            SERVICE_STATUS status{};
            if (QueryServiceStatus(svc, &status) && status.dwCurrentState == SERVICE_RUNNING) {
                found = true;
            }
            CloseServiceHandle(svc);
            if (found) break;
        }
    }
    CloseServiceHandle(scm);
    return found;
}

HWND FindPrimaryGameWindow(uint32_t pid) {
    struct WindowSearchContext {
        DWORD target_pid;
        HWND best_hwnd;
    } ctx{ pid, nullptr };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<WindowSearchContext*>(lParam);
        DWORD win_pid = 0;
        GetWindowThreadProcessId(hwnd, &win_pid);
        if (win_pid == c->target_pid && IsWindowVisible(hwnd)) {
            RECT rc{};
            if (GetWindowRect(hwnd, &rc)) {
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                if (width > 100 && height > 100) {
                    c->best_hwnd = hwnd;
                    return FALSE;
                }
            }
            if (!c->best_hwnd) {
                c->best_hwnd = hwnd;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    return ctx.best_hwnd;
}

struct PreparedPolicyBuffers {
    std::string custom_nul;
    std::string excluded_nul;
    std::vector<std::string> ex_names;
    std::vector<VfProcessExceptionEntry> ex_entries;
    VfProcessCategoryActions categories{};
    bool heavy_work_as_game = false;

    explicit PreparedPolicyBuffers(const ProcessPolicyConfig& policy)
        : heavy_work_as_game(policy.heavy_work_as_game) {
        categories.recording_streaming = static_cast<uint8_t>(policy.category_actions.recording_streaming);
        categories.calls_voice = static_cast<uint8_t>(policy.category_actions.calls_voice);
        categories.messaging = static_cast<uint8_t>(policy.category_actions.messaging);
        categories.browsers = static_cast<uint8_t>(policy.category_actions.browsers);
        categories.stores_launchers = static_cast<uint8_t>(policy.category_actions.stores_launchers);
        categories.music = static_cast<uint8_t>(policy.category_actions.music);
        categories.cloud_sync = static_cast<uint8_t>(policy.category_actions.cloud_sync);

        for (const auto& exe : policy.custom_game_exes) {
            custom_nul += WideToUtf8Local(ExtractFileName(exe));
            custom_nul.push_back('\0');
        }
        for (const auto& exe : policy.excluded_game_exes) {
            excluded_nul += WideToUtf8Local(ExtractFileName(exe));
            excluded_nul.push_back('\0');
        }
        ex_names.reserve(policy.exceptions.size());
        ex_entries.reserve(policy.exceptions.size());
        for (const auto& ex : policy.exceptions) {
            ex_names.push_back(WideToUtf8Local(ExtractFileName(ex.exe_name)));
        }
        for (size_t i = 0; i < policy.exceptions.size(); ++i) {
            VfProcessExceptionEntry e{};
            e.exe_ptr = ex_names[i].empty() ? nullptr : reinterpret_cast<const uint8_t*>(ex_names[i].data());
            e.exe_len = ex_names[i].size();
            e.action = static_cast<uint8_t>(policy.exceptions[i].action);
            ex_entries.push_back(e);
        }
    }

    VfProcessDecision Classify(
        const std::wstring& full_path,
        const std::wstring& exe_name,
        bool is_foreground_3d,
        bool has_anticheat_signal) const {

        const std::string fp_utf8 = WideToUtf8Local(full_path);
        const std::string exe_utf8 = WideToUtf8Local(exe_name);
        VfProcessClassifyInput inp{};
        inp.full_path_ptr = fp_utf8.empty() ? nullptr : reinterpret_cast<const uint8_t*>(fp_utf8.data());
        inp.full_path_len = fp_utf8.size();
        inp.exe_name_ptr = exe_utf8.empty() ? nullptr : reinterpret_cast<const uint8_t*>(exe_utf8.data());
        inp.exe_name_len = exe_utf8.size();
        inp.category_actions = categories;
        inp.exceptions_ptr = ex_entries.empty() ? nullptr : ex_entries.data();
        inp.exceptions_count = ex_entries.size();
        inp.custom_games_nul_ptr = custom_nul.empty() ? nullptr : reinterpret_cast<const uint8_t*>(custom_nul.data());
        inp.custom_games_nul_len = custom_nul.size();
        inp.excluded_games_nul_ptr = excluded_nul.empty() ? nullptr : reinterpret_cast<const uint8_t*>(excluded_nul.data());
        inp.excluded_games_nul_len = excluded_nul.size();
        inp.heavy_work_as_game = heavy_work_as_game;
        inp.is_foreground_3d = is_foreground_3d;
        inp.has_anticheat_signal = has_anticheat_signal;

        VfProcessDecision dec{};
        if (!vf_rust_classify_process(&inp, &dec)) {
            dec.process_class = VF_PROC_CLASS_UNKNOWN;
            dec.action = VF_PROC_ACTION_DO_NOT_TOUCH;
        }
        return dec;
    }
};

// True when the user listed this exe in the per-program exceptions (their choice is never overridden by inheritance).
bool HasUserException(const ProcessPolicyConfig& policy, const std::wstring& exe_name) {
    const std::wstring lower = ToLower(exe_name);
    for (const auto& ex : policy.exceptions) {
        if (ToLower(ExtractFileName(ex.exe_name)) == lower) return true;
    }
    return false;
}

} // anonymous namespace

VfProcessDecision ProcessDetector::ClassifyProcess(
    const std::wstring& full_path,
    const std::wstring& exe_name,
    const ProcessPolicyConfig& policy,
    bool is_foreground_3d,
    bool has_anticheat_signal) {

    PreparedPolicyBuffers prep(policy);
    return prep.Classify(full_path, exe_name, is_foreground_3d, has_anticheat_signal);
}

const char* ProcessDetector::RenderApiToString(RenderApi api) {
    switch (api) {
        case RenderApi::DirectX9_10: return "DirectX 9/10";
        case RenderApi::DirectX11:   return "DirectX 11";
        case RenderApi::DirectX12:   return "DirectX 12";
        case RenderApi::Vulkan:      return "Vulkan";
        case RenderApi::OpenGL:      return "OpenGL";
        case RenderApi::Unknown:
        default:                     return "Unknown";
    }
}

bool ProcessDetector::IsSystemOrExcludedProcess(const std::wstring& exe_name) {
    const std::wstring name = ExtractFileName(exe_name);
    if (name.empty()) {
        return true;
    }
    const ProcessPolicyConfig default_policy{};
    const VfProcessDecision dec = ClassifyProcess(L"", name, default_policy, true, false);
    return dec.process_class == VF_PROC_CLASS_WINDOWS_SYSTEM || dec.action != VF_PROC_ACTION_BOOST_AS_GAME;
}

std::optional<DetectedGameProcess> ProcessDetector::InspectProcess(
    uint32_t pid,
    HWND hwnd_hint,
    const ProcessPolicyConfig& policy) {

    if (pid == 0 || pid == 4) {
        return std::nullopt;
    }

    std::wstring full_path;
    uint64_t creation_time = 0;
    if (!QueryProcessImageAndTime(pid, full_path, creation_time)) {
        return std::nullopt;
    }

    std::wstring exe_name = ExtractFileName(full_path);

    // Inspect modules safely (only TH32CS_SNAPMODULE)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        // Protected process (e.g. anti-cheat) blocking module enumeration: only boost if explicitly in custom games and permitted by Rust.
        const bool ac_signal = (GetLastError() == ERROR_ACCESS_DENIED) || HasActiveAntiCheatService();
        const VfProcessDecision dec = ClassifyProcess(full_path, exe_name, policy, false, ac_signal);
        if (dec.action != VF_PROC_ACTION_BOOST_AS_GAME) {
            return std::nullopt;
        }
        HWND target_hwnd = hwnd_hint ? hwnd_hint : FindPrimaryGameWindow(pid);
        DetectedGameProcess result{};
        result.pid = pid;
        result.creation_time = creation_time;
        result.exe_name = exe_name;
        result.full_path = full_path;
        result.api = RenderApi::Unknown;
        result.main_window = target_hwnd;
        result.presenter_thread_id = target_hwnd ? static_cast<uint32_t>(GetWindowThreadProcessId(target_hwnd, nullptr)) : 0;
        result.generic_anticheat_detected = dec.protected_by_anticheat || ac_signal;
        return result;
    }

    MODULEENTRY32W me{};
    me.dwSize = sizeof(MODULEENTRY32W);

    bool has_d3d12 = false;
    bool has_vulkan = false;
    bool has_d3d11 = false;
    bool has_d3d9_10 = false;
    bool has_opengl = false;
    bool module_ac_signal = false;
    std::wstring driver_dll;

    if (Module32FirstW(hSnap, &me)) {
        do {
            if (exe_name.empty()) {
                exe_name = me.szModule;
            }

            std::wstring mod_lower = ToLower(me.szModule);

            if (!module_ac_signal) {
                const std::string mod_utf8 = WideToUtf8Local(mod_lower);
                if (!mod_utf8.empty() && vf_rust_has_anticheat_signature(mod_utf8.data(), mod_utf8.size())) {
                    module_ac_signal = true;
                }
            }

            // API detection
            if (mod_lower == L"d3d12.dll" || mod_lower == L"d3d12core.dll") {
                has_d3d12 = true;
            } else if (mod_lower == L"vulkan-1.dll") {
                has_vulkan = true;
            } else if (mod_lower == L"d3d11.dll") {
                has_d3d11 = true;
            } else if (mod_lower == L"d3d9.dll" || mod_lower == L"d3d10.dll" || mod_lower == L"d3d10_1.dll") {
                has_d3d9_10 = true;
            } else if (mod_lower == L"opengl32.dll") {
                has_opengl = true;
            }

            // Driver DLL detection (AMD, NVIDIA, Intel)
            if (driver_dll.empty()) {
                if (mod_lower == L"amdxc64.dll" || mod_lower == L"atidxx64.dll" || mod_lower == L"amdvlk64.dll" ||
                    mod_lower == L"amdxc32.dll" || mod_lower == L"atidxx32.dll" || mod_lower == L"amdvlk32.dll" ||
                    mod_lower == L"atio6axx.dll" || mod_lower == L"atioglxx.dll") { // the last two: AMD OpenGL (F5.2)
                    driver_dll = me.szModule;
                } else if (mod_lower == L"nvwgf2umx.dll" || mod_lower == L"nvoglv64.dll" ||
                           mod_lower == L"nvwgf2um.dll"  || mod_lower == L"nvoglv32.dll") {
                    driver_dll = me.szModule;
                } else if (mod_lower == L"igd12umd64.dll" || mod_lower == L"igd10iumd64.dll" ||
                           mod_lower == L"igvk64.dll"     || mod_lower == L"ze_intel_gpu64.dll" ||
                           mod_lower == L"igd12umd32.dll" || mod_lower == L"igd10iumd32.dll" ||
                           mod_lower == L"ig9icd64.dll"   || mod_lower == L"igxelpicd64.dll") { // the last two: Intel OpenGL (F5.2)
                    driver_dll = me.szModule;
                }
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);

    // F5.2: opengl32.dll alone is loaded by many non-games; it counts only with a GPU driver (the ICD, loaded when a
    // context is made) or when the user listed the exe.
    bool listed_custom_exe = false;
    {
        const std::wstring self_lower = ToLower(exe_name);
        for (const auto& ce : policy.custom_game_exes) {
            if (self_lower == ToLower(ExtractFileName(ce))) {
                listed_custom_exe = true;
                break;
            }
        }
    }
    const RenderApi api = PickRenderApi(has_d3d12, has_vulkan, has_d3d11, has_d3d9_10,
                                        has_opengl && (!driver_dll.empty() || listed_custom_exe));

    const bool has_3d_api = (api != RenderApi::Unknown);
    const bool ac_signal = module_ac_signal || HasActiveAntiCheatService();
    const VfProcessDecision dec = ClassifyProcess(full_path, exe_name, policy, has_3d_api, ac_signal);

    // Reject Windows system processes, browsers, Slack/Teams/Discord/Spotify/OBS/IDEs, excluded games, etc.
    if (dec.action != VF_PROC_ACTION_BOOST_AS_GAME) {
        return std::nullopt;
    }

    if (!has_3d_api && dec.process_class != VF_PROC_CLASS_HEAVY_WORK) {
        // Non-heavy-work process only qualifies without loaded 3D DLLs if explicitly requested in custom_game_exes
        bool in_custom = false;
        const std::wstring exe_lower = ToLower(exe_name);
        for (const auto& ce : policy.custom_game_exes) {
            if (exe_lower == ToLower(ExtractFileName(ce))) {
                in_custom = true;
                break;
            }
        }
        if (!in_custom) {
            return std::nullopt;
        }
    }

    HWND target_hwnd = hwnd_hint ? hwnd_hint : FindPrimaryGameWindow(pid);
    uint32_t presenter_tid = 0;
    if (target_hwnd) {
        presenter_tid = static_cast<uint32_t>(GetWindowThreadProcessId(target_hwnd, nullptr));
    }

    DetectedGameProcess result{};
    result.pid = pid;
    result.creation_time = creation_time;
    result.exe_name = exe_name;
    result.full_path = full_path;
    result.api = api;
    result.driver_dll_loaded = driver_dll;
    result.main_window = target_hwnd;
    result.presenter_thread_id = presenter_tid;
    result.generic_anticheat_detected = dec.protected_by_anticheat || ac_signal;

    return result;
}

RenderApi ProcessDetector::PickRenderApi(bool d3d12, bool vulkan, bool d3d11, bool d3d9_10, bool opengl) {
    // Direct3D and Vulkan win over OpenGL: a game that also links opengl32.dll (launchers, overlays) is not an OpenGL game.
    if (d3d12) return RenderApi::DirectX12;
    if (vulkan) return RenderApi::Vulkan;
    if (d3d11) return RenderApi::DirectX11;
    if (d3d9_10) return RenderApi::DirectX9_10;
    if (opengl) return RenderApi::OpenGL;
    return RenderApi::Unknown;
}

uint64_t ProcessDetector::ProcessCreationTime(uint32_t pid) {
    std::wstring path;
    uint64_t created = 0;
    return QueryProcessImageAndTime(pid, path, created) ? created : 0;
}

bool ProcessDetector::IsProcessAlive(uint32_t pid) {
    if (pid == 0 || pid == 4) {
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return false;
    }

    DWORD exit_code = 0;
    BOOL ok = GetExitCodeProcess(hProcess, &exit_code);
    CloseHandle(hProcess);

    return (ok && exit_code == STILL_ACTIVE);
}

std::optional<DetectedGameProcess> ProcessDetector::DetectActiveGame(const ProcessPolicyConfig& policy) {
    std::set<DWORD> inspected_pids;
    const DWORD my_pid = GetCurrentProcessId();

    // 1. Try foreground window first
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != 0 && pid != 4 && pid != my_pid) {
            inspected_pids.insert(pid);
            auto result = InspectProcess(pid, fg, policy);
            if (result.has_value()) {
                return result;
            }
        }
    }

    // 2. Alt+Tab resilience: inspect visible top-level windows >= 640x480
    struct BackgroundSearchContext {
        const ProcessPolicyConfig* policy = nullptr;
        std::set<DWORD>* inspected_pids = nullptr;
        std::optional<DetectedGameProcess> found_game;
        DWORD self_pid = 0;
    } ctx;
    ctx.policy = &policy;
    ctx.inspected_pids = &inspected_pids;
    ctx.self_pid = my_pid;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<BackgroundSearchContext*>(lParam);

        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
            return TRUE;
        }

        bool valid_size = false;
        if (IsIconic(hwnd)) {
            valid_size = true;
        } else {
            RECT rc{};
            if (GetClientRect(hwnd, &rc) && (rc.right - rc.left) >= 640 && (rc.bottom - rc.top) >= 480) {
                valid_size = true;
            } else if (GetWindowRect(hwnd, &rc) && (rc.right - rc.left) >= 640 && (rc.bottom - rc.top) >= 480) {
                valid_size = true;
            }
        }
        if (!valid_size) {
            return TRUE;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == 4 || pid == c->self_pid || c->inspected_pids->count(pid)) {
            return TRUE;
        }
        c->inspected_pids->insert(pid);

        auto inspected = InspectProcess(pid, hwnd, *(c->policy));
        if (inspected.has_value()) {
            bool in_custom = false;
            const std::wstring proc_name = ToLower(inspected->exe_name);
            for (const auto& custom_exe : c->policy->custom_game_exes) {
                if (proc_name == ToLower(ExtractFileName(custom_exe))) {
                    in_custom = true;
                    break;
                }
            }
            if (inspected->api != RenderApi::Unknown && (!inspected->driver_dll_loaded.empty() || in_custom)) {
                c->found_game = inspected;
                return FALSE;
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.found_game.has_value()) {
        return ctx.found_game;
    }

    // 3. Fallback scan for custom_game_exes through Rust classification (never bypasses exclusions!)
    if (!policy.custom_game_exes.empty()) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(PROCESSENTRY32W);
            PreparedPolicyBuffers prep(policy);
            const bool service_ac = HasActiveAntiCheatService();

            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4 || pe.th32ProcessID == my_pid) {
                        continue;
                    }

                    const std::wstring proc_name = ToLower(pe.szExeFile);
                    bool listed_custom = false;
                    for (const auto& custom_exe : policy.custom_game_exes) {
                        if (proc_name == ToLower(ExtractFileName(custom_exe))) {
                            listed_custom = true;
                            break;
                        }
                    }
                    if (!listed_custom) continue;

                    auto inspected = InspectProcess(pe.th32ProcessID, nullptr, policy);
                    if (inspected.has_value()) {
                        CloseHandle(hSnap);
                        return inspected;
                    }

                    std::wstring full_path;
                    uint64_t creation_time = 0;
                    QueryProcessImageAndTime(pe.th32ProcessID, full_path, creation_time);
                    const VfProcessDecision dec = prep.Classify(full_path, pe.szExeFile, false, service_ac);
                    if (dec.action == VF_PROC_ACTION_BOOST_AS_GAME) {
                        DetectedGameProcess custom_match{};
                        custom_match.pid = pe.th32ProcessID;
                        custom_match.creation_time = creation_time;
                        custom_match.exe_name = pe.szExeFile;
                        custom_match.full_path = full_path;
                        custom_match.generic_anticheat_detected = dec.protected_by_anticheat || service_ac;
                        CloseHandle(hSnap);
                        return custom_match;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    return std::nullopt;
}

std::optional<DetectedGameProcess> ProcessDetector::DetectActiveGame(const std::vector<std::wstring>& custom_game_exes) {
    ProcessPolicyConfig policy{};
    policy.custom_game_exes = custom_game_exes;
    return DetectActiveGame(policy);
}

std::vector<BackgroundProcessTarget> ProcessDetector::ClassifyBackgroundProcesses(
    uint32_t active_game_pid,
    const ProcessPolicyConfig& policy) {

    std::vector<BackgroundProcessTarget> targets;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return targets;
    }

    const DWORD my_pid = GetCurrentProcessId();
    PreparedPolicyBuffers prep(policy);

    // F5.1: every process enters the tree (the game, Windows and protected ones too: they are what cuts a chain).
    struct Seen {
        std::wstring exe_name;
        std::wstring full_path;
    };
    std::vector<Seen> seen;
    std::vector<VfProcessTreeNode> nodes;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            Seen s;
            s.exe_name = pe.szExeFile;
            uint64_t creation_time = 0;
            QueryProcessImageAndTime(pe.th32ProcessID, s.full_path, creation_time);

            const VfProcessDecision dec = prep.Classify(s.full_path, pe.szExeFile, false, false);
            VfProcessTreeNode n{};
            n.pid = pe.th32ProcessID;
            n.parent_pid = pe.th32ParentProcessID;
            n.creation_time = creation_time;
            n.process_class = dec.process_class;
            n.action = dec.action;
            n.has_user_exception = HasUserException(policy, pe.szExeFile);
            const std::string path_utf8 = WideToUtf8Local(ToLower(s.full_path.empty() ? s.exe_name : s.full_path));
            n.has_anticheat_signal = !path_utf8.empty() &&
                vf_rust_has_anticheat_signature(path_utf8.data(), path_utf8.size());
            nodes.push_back(n);
            seen.push_back(std::move(s));
        } while (Process32NextW(hSnap, &pe));
    }

    std::vector<VfProcessDecision> decided(nodes.size());
    if (!nodes.empty() &&
        !vf_rust_inherit_process_actions(nodes.data(), nodes.size(), active_game_pid, decided.data())) {
        // Bridge refused: fall back to each process's own decision (the pre-F5.1 behavior).
        for (size_t i = 0; i < nodes.size(); ++i) {
            decided[i].process_class = nodes[i].process_class;
            decided[i].action = nodes[i].action;
        }
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        const VfProcessTreeNode& n = nodes[i];
        if (n.pid == 0 || n.pid == 4 || n.pid == active_game_pid || n.pid == my_pid) {
            continue;
        }
        const VfProcessDecision& dec = decided[i];
        if (dec.action == VF_PROC_ACTION_ECO_ONLY || dec.action == VF_PROC_ACTION_THROTTLE) {
            BackgroundProcessTarget t{};
            t.pid = n.pid;
            t.creation_time = n.creation_time;
            t.exe_name = seen[i].exe_name;
            t.full_path = std::move(seen[i].full_path);
            t.process_class = static_cast<ProcessClass>(dec.process_class);
            t.action = static_cast<ProcessAction>(dec.action);
            targets.push_back(std::move(t));
        }
    }

    CloseHandle(hSnap);
    return targets;
}

std::vector<uint32_t> ProcessDetector::FindBackgroundNoisyProcesses(
    uint32_t active_game_pid,
    const ProcessPolicyConfig& policy) {

    auto targets = ClassifyBackgroundProcesses(active_game_pid, policy);
    std::vector<uint32_t> pids;
    pids.reserve(targets.size());
    for (const auto& t : targets) {
        pids.push_back(t.pid);
    }
    return pids;
}

} // namespace vegaflow
