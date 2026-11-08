#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool GraphicsOptimizer::SetMmcssGamingProfileOptimized(bool optimize, bool dry_run) {
    if (dry_run) {
        return true;
    }

    HKEY hProfile = nullptr;
    DWORD disposition = 0;
    LSTATUS stProfile = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_SET_VALUE,
        nullptr,
        &hProfile,
        &disposition
    );

    bool profile_ok = false;
    if (stProfile == ERROR_SUCCESS) {
        if (optimize) RegBackupValue(kHiveLocalMachine, kMmcssProfilePath, hProfile, L"SystemResponsiveness");
        DWORD sys_resp = optimize ? 10 : 20;
        LSTATUS s1 = RegSetValueExW(hProfile, L"SystemResponsiveness", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sys_resp), sizeof(sys_resp));
        profile_ok = (s1 == ERROR_SUCCESS);
        RegCloseKey(hProfile);
    }

    HKEY hGames = nullptr;
    LSTATUS stGames = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_SET_VALUE,
        nullptr,
        &hGames,
        &disposition
    );

    bool games_ok = false;
    if (stGames == ERROR_SUCCESS) {
        if (optimize) {
            for (const wchar_t* n : kMmcssGamesValues) RegBackupValue(kHiveLocalMachine, kMmcssGamesPath, hGames, n);
        }
        DWORD gpu_prio = optimize ? 8 : 2;
        DWORD prio = optimize ? 6 : 2;
        const wchar_t* sched_cat = optimize ? L"High" : L"Medium";
        const wchar_t* sfio_prio = optimize ? L"High" : L"Normal";

        LSTATUS s1 = RegSetValueExW(hGames, L"GPU Priority", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&gpu_prio), sizeof(gpu_prio));
        LSTATUS s2 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&prio), sizeof(prio));
        LSTATUS s3 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
        LSTATUS s4 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));

        games_ok = (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS && s4 == ERROR_SUCCESS);
        RegCloseKey(hGames);
    }

    if (!optimize) {
        RegRestoreValue(kHiveLocalMachine, kMmcssProfilePath, L"SystemResponsiveness");
        for (const wchar_t* n : kMmcssGamesValues) RegRestoreValue(kHiveLocalMachine, kMmcssGamesPath, n);
    }
    return profile_ok && games_ok;
}

bool GraphicsOptimizer::ApplyMmcssTaskTweaks(bool dryRun) {
    if (dryRun) {
        // Simulate access validation of Multimedia\SystemProfile and Tasks\Games without modifying the registry
        HKEY hProfile = nullptr;
        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
            0, KEY_READ, &hProfile) == ERROR_SUCCESS) {
            RegCloseKey(hProfile);
        }
        HKEY hGames = nullptr;
        if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
            0, KEY_READ, &hGames) == ERROR_SUCCESS) {
            RegCloseKey(hGames);
        }
        OutputDebugStringW(L"[VegaFlow] Dry-Run: ApplyMmcssTaskTweaks simulated successfully.\n");
        return true;
    }

    // 1. Multimedia\SystemProfile -> SystemResponsiveness = 10 (DWORD)
    HKEY hProfile = nullptr;
    LSTATUS stProfile = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0, KEY_READ | KEY_SET_VALUE, &hProfile
    );
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }
    RegBackupValue(kHiveLocalMachine, kMmcssProfilePath, hProfile, L"SystemResponsiveness");
    const DWORD sys_resp = 10;
    stProfile = RegSetValueExW(
        hProfile,
        L"SystemResponsiveness",
        0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&sys_resp),
        sizeof(sys_resp)
    );
    RegCloseKey(hProfile);
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }

    // 2. Tasks\Games -> Scheduling Category, SFIO Priority, Background Only, Clock Rate, GPU Priority, Priority
    HKEY hGames = nullptr;
    LSTATUS stGames = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_READ | KEY_SET_VALUE, &hGames
    );
    if (stGames != ERROR_SUCCESS) {
        return false;
    }

    for (const wchar_t* n : kMmcssGamesValues) RegBackupValue(kHiveLocalMachine, kMmcssGamesPath, hGames, n);
    const wchar_t* sched_cat = L"High";
    const wchar_t* sfio_prio = L"High";
    const wchar_t* bg_only = L"False";
    const DWORD clock_rate = 10000;
    const DWORD gpu_prio = 8;
    const DWORD prio = 6;

    LSTATUS s1 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
    LSTATUS s2 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));
    LSTATUS s3 = RegSetValueExW(hGames, L"Background Only", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(bg_only), static_cast<DWORD>((wcslen(bg_only) + 1) * sizeof(wchar_t)));
    LSTATUS s4 = RegSetValueExW(hGames, L"Clock Rate", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&clock_rate), sizeof(clock_rate));
    LSTATUS s5 = RegSetValueExW(hGames, L"GPU Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&gpu_prio), sizeof(gpu_prio));
    LSTATUS s6 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&prio), sizeof(prio));

    RegCloseKey(hGames);

    return (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS &&
            s4 == ERROR_SUCCESS && s5 == ERROR_SUCCESS && s6 == ERROR_SUCCESS);
}

bool GraphicsOptimizer::RestoreMmcssTaskTweaks(bool dryRun) {
    if (dryRun) {
        OutputDebugStringW(L"[VegaFlow] Dry-Run: RestoreMmcssTaskTweaks simulated successfully.\n");
        return true;
    }

    // 1. Multimedia\SystemProfile -> SystemResponsiveness = 20 (standard Windows NT default)
    HKEY hProfile = nullptr;
    LSTATUS stProfile = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile",
        0, KEY_SET_VALUE, &hProfile
    );
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }
    const DWORD default_resp = 20;
    stProfile = RegSetValueExW(
        hProfile,
        L"SystemResponsiveness",
        0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&default_resp),
        sizeof(default_resp)
    );
    RegCloseKey(hProfile);
    if (stProfile != ERROR_SUCCESS) {
        return false;
    }

    // 2. Tasks\Games -> Scheduling Category = Medium, SFIO Priority = Normal, Background Only = True, Clock Rate = 10000, Priority = 2
    HKEY hGames = nullptr;
    LSTATUS stGames = RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_SET_VALUE, &hGames
    );
    if (stGames != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* sched_cat = L"Medium";
    const wchar_t* sfio_prio = L"Normal";
    const wchar_t* bg_only = L"True";
    const DWORD clock_rate = 10000;
    const DWORD prio = 2;

    LSTATUS s1 = RegSetValueExW(hGames, L"Scheduling Category", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sched_cat), static_cast<DWORD>((wcslen(sched_cat) + 1) * sizeof(wchar_t)));
    LSTATUS s2 = RegSetValueExW(hGames, L"SFIO Priority", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(sfio_prio), static_cast<DWORD>((wcslen(sfio_prio) + 1) * sizeof(wchar_t)));
    LSTATUS s3 = RegSetValueExW(hGames, L"Background Only", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(bg_only), static_cast<DWORD>((wcslen(bg_only) + 1) * sizeof(wchar_t)));
    LSTATUS s4 = RegSetValueExW(hGames, L"Clock Rate", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&clock_rate), sizeof(clock_rate));
    LSTATUS s5 = RegSetValueExW(hGames, L"Priority", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&prio), sizeof(prio));

    RegCloseKey(hGames);

    RegRestoreValue(kHiveLocalMachine, kMmcssProfilePath, L"SystemResponsiveness");
    for (const wchar_t* n : kMmcssGamesValues) RegRestoreValue(kHiveLocalMachine, kMmcssGamesPath, n);

    return (s1 == ERROR_SUCCESS && s2 == ERROR_SUCCESS && s3 == ERROR_SUCCESS &&
            s4 == ERROR_SUCCESS && s5 == ERROR_SUCCESS);
}

bool GraphicsOptimizer::QueryMmcssTaskTweaks(std::string& schedulingCategory, std::string& sfioPriority, uint32_t& clockRate) {
    schedulingCategory = "Medium";
    sfioPriority = "Normal";
    clockRate = 10000;

    HKEY hGames = nullptr;
    if (RegOpenKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games",
        0, KEY_READ, &hGames) != ERROR_SUCCESS) {
        return false;
    }

    auto ReadRegSz = [](HKEY hKey, const wchar_t* valName, std::string& outVal) {
        wchar_t buf[256] = {0};
        DWORD type = 0;
        DWORD size = sizeof(buf);
        if (RegQueryValueExW(hKey, valName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS && type == REG_SZ) {
            buf[(sizeof(buf) / sizeof(wchar_t)) - 1] = L'\0';
            int required = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
            if (required > 1) {
                std::vector<char> utf8(required);
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), required, nullptr, nullptr);
                outVal = utf8.data();
            } else if (required == 1) {
                outVal.clear();
            }
        }
    };

    ReadRegSz(hGames, L"Scheduling Category", schedulingCategory);
    ReadRegSz(hGames, L"SFIO Priority", sfioPriority);

    DWORD cr_val = 0;
    DWORD type = 0;
    DWORD size = sizeof(cr_val);
    if (RegQueryValueExW(hGames, L"Clock Rate", nullptr, &type, reinterpret_cast<LPBYTE>(&cr_val), &size) == ERROR_SUCCESS && type == REG_DWORD) {
        clockRate = cr_val;
    }

    RegCloseKey(hGames);
    return true;
}

} // namespace vegaflow
