#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow {

using namespace graphics_internal;

bool MmcssTweaks::SetGamingProfileOptimized(bool optimize, bool dry_run) {
    if (dry_run) {
        return true;
    }

    // Giving it back is never a write: with VegaFlow's copy the user's values return, without one they were never touched.
    if (!optimize) {
        reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMmcssProfilePath, L"SystemResponsiveness");
        for (const wchar_t* n : kMmcssGamesCleanupValues) {
            reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMmcssGamesPath, n);
        }
        return true;
    }

    const bool profile_ok = reg_.WriteValue(
        RegHive::LocalMachine,
        kMmcssProfilePath,
        L"SystemResponsiveness",
        RegValue::MakeDword(10u),
        true,
        "MMCSS SystemResponsiveness gaming",
        "10");
    const bool s2 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Priority",
        RegValue::MakeDword(6u), true, "MMCSS Tasks\\Games gaming", "6");
    const bool s3 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Scheduling Category",
        RegValue::MakeString(L"High"), true, "MMCSS Tasks\\Games gaming", "High");
    const bool s4 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"SFIO Priority",
        RegValue::MakeString(L"High"), true, "MMCSS Tasks\\Games gaming", "High");
    return profile_ok && s2 && s3 && s4;
}

bool MmcssTweaks::ApplyTaskTweaks(bool dry_run) {
    if (dry_run) {
        (void)reg_.KeyExists(RegHive::LocalMachine, kMmcssProfilePath);
        (void)reg_.KeyExists(RegHive::LocalMachine, kMmcssGamesPath);
        return true;
    }

    if (!reg_.WriteValue(
            RegHive::LocalMachine,
            kMmcssProfilePath,
            L"SystemResponsiveness",
            RegValue::MakeDword(10u),
            true,
            "MMCSS Task tweaks",
            "10")) {
        return false;
    }

    const bool s1 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Scheduling Category",
        RegValue::MakeString(L"High"), true, "MMCSS Task tweaks", "High");
    const bool s2 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"SFIO Priority",
        RegValue::MakeString(L"High"), true, "MMCSS Task tweaks", "High");
    const bool s3 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Background Only",
        RegValue::MakeString(L"False"), true, "MMCSS Task tweaks", "False");
    const bool s4 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Clock Rate",
        RegValue::MakeDword(10000u), true, "MMCSS Task tweaks", "10000");
    const bool s5 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Priority",
        RegValue::MakeDword(6u), true, "MMCSS Task tweaks", "6");

    return s1 && s2 && s3 && s4 && s5;
}

bool MmcssTweaks::RestoreTaskTweaks(bool dry_run) {
    if (dry_run) {
        return true;
    }

    const bool profile_ok = reg_.WriteValue(
        RegHive::LocalMachine,
        kMmcssProfilePath,
        L"SystemResponsiveness",
        RegValue::MakeDword(20u),
        false);
    if (!profile_ok) {
        return false;
    }

    const bool s1 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Scheduling Category",
        RegValue::MakeString(L"Medium"), false);
    const bool s2 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"SFIO Priority",
        RegValue::MakeString(L"Normal"), false);
    const bool s3 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Background Only",
        RegValue::MakeString(L"True"), false);
    const bool s4 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Clock Rate",
        RegValue::MakeDword(10000u), false);
    const bool s5 = reg_.WriteValue(
        RegHive::LocalMachine, kMmcssGamesPath, L"Priority",
        RegValue::MakeDword(2u), false);

    reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMmcssProfilePath, L"SystemResponsiveness");
    for (const wchar_t* n : kMmcssGamesCleanupValues) {
        reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMmcssGamesPath, n);
    }

    return s1 && s2 && s3 && s4 && s5;
}

bool MmcssTweaks::QueryTaskTweaks(std::string& scheduling_category, std::string& sfio_priority, uint32_t& clock_rate) {
    scheduling_category = "Medium";
    sfio_priority = "Normal";
    clock_rate = 10000u;

    if (!reg_.KeyExists(RegHive::LocalMachine, kMmcssGamesPath)) {
        return false;
    }

    RegValue val;
    if (reg_.ReadValue(RegHive::LocalMachine, kMmcssGamesPath, L"Scheduling Category", val) &&
        val.type == RegValueType::String) {
        scheduling_category = WideToUtf8(val.str);
    }
    if (reg_.ReadValue(RegHive::LocalMachine, kMmcssGamesPath, L"SFIO Priority", val) &&
        val.type == RegValueType::String) {
        sfio_priority = WideToUtf8(val.str);
    }
    if (reg_.ReadValue(RegHive::LocalMachine, kMmcssGamesPath, L"Clock Rate", val) &&
        val.type == RegValueType::Dword) {
        clock_rate = val.dword;
    }

    return true;
}

bool MmcssTweaks::QueryGamingProfileOptimized() {
    RegValue resp;
    if (!reg_.ReadValue(RegHive::LocalMachine, kMmcssProfilePath, L"SystemResponsiveness", resp) ||
        resp.type != RegValueType::Dword || resp.dword > 10u) {
        return false;
    }

    RegValue prio;
    if (!reg_.ReadValue(RegHive::LocalMachine, kMmcssGamesPath, L"Priority", prio) ||
        prio.type != RegValueType::Dword || prio.dword != 6u) {
        return false;
    }

    RegValue sched;
    if (!reg_.ReadValue(RegHive::LocalMachine, kMmcssGamesPath, L"Scheduling Category", sched) ||
        sched.type != RegValueType::String || ToLower(sched.str) != L"high") {
        return false;
    }

    return true;
}

} // namespace vegaflow
