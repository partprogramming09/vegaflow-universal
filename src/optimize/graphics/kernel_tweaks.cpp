#include "optimize/graphics/graphics_internal.hpp"

#include <cstdio>

namespace vegaflow {

using namespace graphics_internal;

namespace {

constexpr wchar_t kPriorityControlPath[] = L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl";
constexpr wchar_t kMemoryManagementPath[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management";

} // namespace

bool KernelSchedulerTweaks::SetMemoryCompressionEnabled(bool enable, bool dry_run) {
    return reg_.SetMemoryCompressionEnabled(enable, dry_run);
}

bool KernelSchedulerTweaks::RevertMemoryCompressionChange(bool dry_run) {
    return reg_.RevertMemoryCompressionChange(dry_run);
}

bool KernelSchedulerTweaks::IsMemoryCompressionEnabled() {
    return reg_.IsMemoryCompressionEnabled();
}

bool KernelSchedulerTweaks::SetDynamicPrioritySeparation(uint32_t value, bool dry_run, bool user_off) {
    if (dry_run) {
        return true;
    }

    // Giving back never writes the factory value: with VegaFlow's copy the user's value returns, without one it was never
    // touched. Only the user's own "off" in Ajustes writes it, after its copy.
    if (value == 0x02u && user_off) {
        return UserOffValue(reg_, RegHive::LocalMachine, kPriorityControlPath, L"Win32PrioritySeparation", RegValue::MakeDword(0x02u),
                            "Prioridad de quantum CPU stock", "0x2");
    }
    if (value == 0x02u) {
        reg_.RestoreBackedUpValue(RegHive::LocalMachine, kPriorityControlPath, L"Win32PrioritySeparation");
        return true;
    }
    char hexbuf[16]{};
    std::snprintf(hexbuf, sizeof(hexbuf), "0x%x", value);

    const bool set_ok = reg_.WriteValue(
        RegHive::LocalMachine,
        kPriorityControlPath,
        L"Win32PrioritySeparation",
        RegValue::MakeDword(value),
        true,
        "Prioridad de quantum CPU en juego",
        hexbuf);
    return set_ok;
}

uint32_t KernelSchedulerTweaks::QueryDynamicPrioritySeparation() {
    RegValue val;
    if (reg_.ReadValue(RegHive::LocalMachine, kPriorityControlPath, L"Win32PrioritySeparation", val) &&
        val.type == RegValueType::Dword) {
        return val.dword;
    }
    return 2u;
}

bool KernelSchedulerTweaks::SetKernelMemoryPagingShield(bool enable, bool dry_run, bool user_off) {
    if (dry_run) {
        return true;
    }

    // Giving it back is never a write: with VegaFlow's copy the user's values return, without one they were never touched.
    if (!enable && user_off) {
        constexpr auto LM = RegHive::LocalMachine;
        const bool a = UserOffValue(reg_, LM, kMemoryManagementPath, L"DisablePagingExecutive", RegValue::MakeDword(0u), "Blindaje de memoria stock", "0");
        const bool b = UserOffValue(reg_, LM, kMemoryManagementPath, L"LargeSystemCache", RegValue::MakeDword(0u), "Caché de sistema stock", "0");
        return a && b;
    }
    if (!enable) {
        reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMemoryManagementPath, L"DisablePagingExecutive");
        reg_.RestoreBackedUpValue(RegHive::LocalMachine, kMemoryManagementPath, L"LargeSystemCache");
        return true;
    }

    const uint32_t disable_paging = 1u;
    const uint32_t large_cache = 0u;

    const bool st1 = reg_.WriteValue(
        RegHive::LocalMachine,
        kMemoryManagementPath,
        L"DisablePagingExecutive",
        RegValue::MakeDword(disable_paging),
        true,
        "Blindaje de memoria del kernel",
        "1");

    const bool st2 = reg_.WriteValue(
        RegHive::LocalMachine,
        kMemoryManagementPath,
        L"LargeSystemCache",
        RegValue::MakeDword(large_cache),
        true,
        "Evitar desplazamiento por cach\u00e9",
        "0");

    return st1 && st2;
}

bool KernelSchedulerTweaks::QueryKernelMemoryPagingShield() {
    RegValue val;
    if (reg_.ReadValue(RegHive::LocalMachine, kMemoryManagementPath, L"DisablePagingExecutive", val) &&
        val.type == RegValueType::Dword) {
        return val.dword == 1u;
    }
    return false;
}

} // namespace vegaflow
