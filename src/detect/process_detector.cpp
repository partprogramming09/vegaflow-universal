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

} // anonymous namespace

const char* ProcessDetector::RenderApiToString(RenderApi api) {
    switch (api) {
        case RenderApi::DirectX9_10: return "DirectX 9/10";
        case RenderApi::DirectX11:   return "DirectX 11";
        case RenderApi::DirectX12:   return "DirectX 12";
        case RenderApi::Vulkan:      return "Vulkan";
        case RenderApi::Unknown:
        default:                     return "Unknown";
    }
}

bool ProcessDetector::IsSystemOrExcludedProcess(const std::wstring& exe_name) {
    std::wstring name = ToLower(ExtractFileName(exe_name));
    if (name.empty()) {
        return true;
    }

    static const wchar_t* const kExcludedProcesses[] = {
        // System and core OS processes
        L"system",
        L"registry",
        L"smss.exe",
        L"csrss.exe",
        L"wininit.exe",
        L"services.exe",
        L"lsass.exe",
        L"svchost.exe",
        L"fontdrvhost.exe",
        L"winlogon.exe",
        L"sihost.exe",
        L"spoolsv.exe",
        L"dwm.exe",
        L"explorer.exe",
        L"taskhostw.exe",
        L"shellexperiencehost.exe",
        L"startmenuexperiencehost.exe",
        L"searchhost.exe",
        L"searchindexer.exe",
        L"searchapp.exe",
        L"systemsettings.exe",
        L"applicationframehost.exe",
        L"textinputhost.exe",
        L"runtimebroker.exe",
        L"ctfmon.exe",
        L"securityhealthservice.exe",
        L"securityhealthsystray.exe",
        L"smartscreen.exe",
        L"audiodg.exe",
        L"conhost.exe",
        L"taskmgr.exe",
        L"wlanext.exe",
        L"dashost.exe",
        L"dllhost.exe",
        L"wmpnetwk.exe",

        // Web browsers that utilize D3D11/D3D12 hardware acceleration
        L"chrome.exe",
        L"msedge.exe",
        L"firefox.exe",
        L"brave.exe",
        L"opera.exe",
        L"operagx.exe",
        L"vivaldi.exe",

        // Chat & Store helpers that render with embedded Chromium / Direct3D
        L"discord.exe",
        L"discordcanary.exe",
        L"discordptb.exe",
        L"steamwebhelper.exe",

        // IDEs, Terminals and Developer tools
        L"devenv.exe",
        L"code.exe",
        L"trae.exe",
        L"powershell.exe",
        L"pwsh.exe",
        L"cmd.exe",
        L"windowsterminal.exe",
        L"openconsole.exe",

        // VegaFlow daemon and tools
        L"vegaflow_daemon.exe",
        L"vegaflow_daemon.old.exe",
        L"vegaflow.exe"
    };

    for (const auto* excluded : kExcludedProcesses) {
        if (name == excluded) {
            return true;
        }
    }

    return false;
}

std::optional<DetectedGameProcess> ProcessDetector::InspectProcess(uint32_t pid, HWND hwnd_hint) {
    if (pid == 0 || pid == 4) {
        return std::nullopt;
    }

    // Anti-Cheat safety rule: ONLY PROCESS_QUERY_LIMITED_INFORMATION
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return std::nullopt;
    }

    wchar_t path_buffer[MAX_PATH * 4] = {0};
    DWORD size = static_cast<DWORD>(std::size(path_buffer));
    std::wstring full_path;
    if (QueryFullProcessImageNameW(hProcess, 0, path_buffer, &size)) {
        full_path.assign(path_buffer, size);
    }
    CloseHandle(hProcess);

    std::wstring exe_name = ExtractFileName(full_path);
    if (!exe_name.empty() && IsSystemOrExcludedProcess(exe_name)) {
        return std::nullopt;
    }

    // Inspect modules safely
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) {
        // Protected process or Anti-Cheat preventing module snapshot
        return std::nullopt;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);

    bool has_d3d12 = false;
    bool has_vulkan = false;
    bool has_d3d11 = false;
    bool has_d3d9_10 = false;
    std::wstring driver_dll;

    if (Module32FirstW(hSnap, &me)) {
        do {
            if (exe_name.empty()) {
                exe_name = me.szModule;
                if (IsSystemOrExcludedProcess(exe_name)) {
                    CloseHandle(hSnap);
                    return std::nullopt;
                }
            }

            std::wstring mod_lower = ToLower(me.szModule);

            // API detection
            if (mod_lower == L"d3d12.dll" || mod_lower == L"d3d12core.dll") {
                has_d3d12 = true;
            } else if (mod_lower == L"vulkan-1.dll") {
                has_vulkan = true;
            } else if (mod_lower == L"d3d11.dll") {
                has_d3d11 = true;
            } else if (mod_lower == L"d3d9.dll" || mod_lower == L"d3d10.dll" || mod_lower == L"d3d10_1.dll") {
                has_d3d9_10 = true;
            }

            // Driver DLL detection (AMD, NVIDIA, Intel)
            if (driver_dll.empty()) {
                if (mod_lower == L"amdxc64.dll" || mod_lower == L"atidxx64.dll" || mod_lower == L"amdvlk64.dll" ||
                    mod_lower == L"amdxc32.dll" || mod_lower == L"atidxx32.dll" || mod_lower == L"amdvlk32.dll") {
                    driver_dll = me.szModule;
                } else if (mod_lower == L"nvwgf2umx.dll" || mod_lower == L"nvoglv64.dll" ||
                           mod_lower == L"nvwgf2um.dll"  || mod_lower == L"nvoglv32.dll") {
                    driver_dll = me.szModule;
                } else if (mod_lower == L"igd12umd64.dll" || mod_lower == L"igd10iumd64.dll" ||
                           mod_lower == L"igvk64.dll"     || mod_lower == L"ze_intel_gpu64.dll" ||
                           mod_lower == L"igd12umd32.dll" || mod_lower == L"igd10iumd32.dll") {
                    driver_dll = me.szModule;
                }
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);

    RenderApi api = RenderApi::Unknown;
    if (has_d3d12) {
        api = RenderApi::DirectX12;
    } else if (has_vulkan) {
        api = RenderApi::Vulkan;
    } else if (has_d3d11) {
        api = RenderApi::DirectX11;
    } else if (has_d3d9_10) {
        api = RenderApi::DirectX9_10;
    }

    if (api == RenderApi::Unknown) {
        return std::nullopt;
    }

    HWND target_hwnd = hwnd_hint;
    if (!target_hwnd) {
        struct WindowSearchContext {
            DWORD target_pid;
            HWND best_hwnd;
        } ctx{ pid, nullptr };

        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto* c = reinterpret_cast<WindowSearchContext*>(lParam);
            DWORD win_pid = 0;
            GetWindowThreadProcessId(hwnd, &win_pid);
            if (win_pid == c->target_pid && IsWindowVisible(hwnd)) {
                RECT rc;
                if (GetWindowRect(hwnd, &rc)) {
                    int width = rc.right - rc.left;
                    int height = rc.bottom - rc.top;
                    if (width > 100 && height > 100) {
                        c->best_hwnd = hwnd;
                        return FALSE; // Found primary game window
                    }
                }
                if (!c->best_hwnd) {
                    c->best_hwnd = hwnd;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        target_hwnd = ctx.best_hwnd;
    }

    uint32_t presenter_tid = 0;
    if (target_hwnd) {
        presenter_tid = static_cast<uint32_t>(GetWindowThreadProcessId(target_hwnd, nullptr));
    }

    DetectedGameProcess result;
    result.pid = pid;
    result.exe_name = exe_name;
    result.full_path = full_path;
    result.api = api;
    result.driver_dll_loaded = driver_dll;
    result.main_window = target_hwnd;
    result.presenter_thread_id = presenter_tid;

    return result;
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

std::optional<DetectedGameProcess> ProcessDetector::DetectActiveGame(const std::vector<std::wstring>& custom_game_exes) {
    std::set<DWORD> inspected_pids;
    const DWORD my_pid = GetCurrentProcessId();

    // 1. Try foreground window first
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        if (pid != 0 && pid != 4 && pid != my_pid) {
            inspected_pids.insert(pid);
            auto result = InspectProcess(pid, fg);
            if (result.has_value()) {
                return result;
            }
        }
    }

    // 2. Resiliencia al Alt+Tab: Si la ventana en primer plano NO es un juego,
    // enumerar ventanas visibles con EnumWindows
    struct BackgroundSearchContext {
        const std::vector<std::wstring>* custom_game_exes = nullptr;
        std::set<DWORD>* inspected_pids = nullptr;
        std::optional<DetectedGameProcess> found_game;
        DWORD self_pid = 0;
    } ctx;
    ctx.custom_game_exes = &custom_game_exes;
    ctx.inspected_pids = &inspected_pids;
    ctx.self_pid = my_pid;

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* c = reinterpret_cast<BackgroundSearchContext*>(lParam);

        if (!IsWindowVisible(hwnd)) {
            return TRUE;
        }

        if (GetWindow(hwnd, GW_OWNER) != nullptr) {
            return TRUE;
        }

        bool valid_size = false;
        if (IsIconic(hwnd)) {
            valid_size = true;
        } else {
            RECT rc{};
            if (GetClientRect(hwnd, &rc)) {
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                if (width >= 640 && height >= 480) {
                    valid_size = true;
                }
            }
            if (!valid_size && GetWindowRect(hwnd, &rc)) {
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                if (width >= 640 && height >= 480) {
                    valid_size = true;
                }
            }
        }

        if (!valid_size) {
            return TRUE;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0 || pid == 4 || pid == c->self_pid) {
            return TRUE;
        }

        if (c->inspected_pids->find(pid) != c->inspected_pids->end()) {
            return TRUE;
        }
        c->inspected_pids->insert(pid);

        auto inspected = InspectProcess(pid, hwnd);
        if (inspected.has_value()) {
            bool in_custom = false;
            if (c->custom_game_exes) {
                std::wstring proc_name = ToLower(inspected->exe_name);
                for (const auto& custom_exe : *(c->custom_game_exes)) {
                    if (proc_name == ToLower(ExtractFileName(custom_exe))) {
                        in_custom = true;
                        break;
                    }
                }
            }

            if (inspected->api != RenderApi::Unknown && (!inspected->driver_dll_loaded.empty() || in_custom)) {
                c->found_game = inspected;
                return FALSE; // Detener la enumeración, juego encontrado
            }
        }

        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.found_game.has_value()) {
        return ctx.found_game;
    }

    // 3. If custom_game_exes is provided, search running processes via snapshot
    if (!custom_game_exes.empty()) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(PROCESSENTRY32W);

            if (Process32FirstW(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4) {
                        continue;
                    }

                    std::wstring proc_name = ToLower(pe.szExeFile);
                    for (const auto& custom_exe : custom_game_exes) {
                        if (proc_name == ToLower(ExtractFileName(custom_exe))) {
                            auto inspected = InspectProcess(pe.th32ProcessID, nullptr);
                            if (inspected.has_value()) {
                                CloseHandle(hSnap);
                                return inspected;
                            }

                            // If InspectProcess returned nullopt (e.g. anti-cheat protected modules or still initializing),
                            // construct a DetectedGameProcess based on explicit custom match
                            DetectedGameProcess custom_match;
                            custom_match.pid = pe.th32ProcessID;
                            custom_match.exe_name = pe.szExeFile;

                            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                            if (hProc) {
                                wchar_t path_buffer[MAX_PATH * 4] = {0};
                                DWORD size = static_cast<DWORD>(std::size(path_buffer));
                                if (QueryFullProcessImageNameW(hProc, 0, path_buffer, &size)) {
                                    custom_match.full_path.assign(path_buffer, size);
                                }
                                CloseHandle(hProc);
                            }

                            CloseHandle(hSnap);
                            return custom_match;
                        }
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
    }

    return std::nullopt;
}

std::vector<uint32_t> ProcessDetector::FindBackgroundNoisyProcesses(uint32_t active_game_pid) {
    std::vector<uint32_t> noisy_pids;

    static const wchar_t* const kNoisyExes[] = {
        // Browsers
        L"chrome.exe",
        L"msedge.exe",
        L"firefox.exe",
        L"brave.exe",
        L"opera.exe",
        L"operagx.exe",
        L"vivaldi.exe",
        L"chromium.exe",

        // Communication / Chat
        L"discord.exe",
        L"discordcanary.exe",
        L"discordptb.exe",
        L"slack.exe",
        L"teams.exe",
        L"ms-teams.exe",
        L"skype.exe",
        L"telegram.exe",
        L"whatsapp.exe",

        // Game Launchers and store helpers
        L"steamwebhelper.exe",
        L"epicgameslauncher.exe",
        L"battlenet.exe",
        L"origin.exe",
        L"eadesktop.exe",
        L"ubisoftconnect.exe",
        L"upc.exe",
        L"galaxyclient.exe",
        L"riotclientservices.exe",

        // Streaming / Overlays / Media
        L"obs64.exe",
        L"obs32.exe",
        L"streamlabs obs.exe",
        L"overwolf.exe",
        L"overwolfbrowser.exe",
        L"spotify.exe",

        // Cloud Sync
        L"onedrive.exe",
        L"googledrivefs.exe",
        L"dropbox.exe"
    };

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        return noisy_pids;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4 || pe.th32ProcessID == active_game_pid) {
                continue;
            }

            std::wstring exe_lower = ToLower(pe.szExeFile);
            for (const auto* noisy : kNoisyExes) {
                if (exe_lower == noisy) {
                    noisy_pids.push_back(pe.th32ProcessID);
                    break;
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return noisy_pids;
}

} // namespace vegaflow
