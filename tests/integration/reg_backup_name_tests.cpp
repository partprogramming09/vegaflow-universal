// Integration test: a RegBackup record written under the older (std::hash) name is still found, so a later write does not
// back up the value VegaFlow had already changed, and restoring puts back the true original.

#include <windows.h>

#include <string>

#include "windows/reg_restore_scope.hpp"
#include "windows/registry.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

constexpr wchar_t kSubkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR";
constexpr wchar_t kValue[] = L"VegaFlowTest95";
constexpr wchar_t kBackupRoot[] = L"SOFTWARE\\VegaFlow\\RegBackup\\";

bool SetDword(const wchar_t* subkey, const wchar_t* name, DWORD v) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const bool ok = RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) == ERROR_SUCCESS;
    RegCloseKey(h);
    return ok;
}

DWORD ReadDword(const wchar_t* subkey, const wchar_t* name) {
    DWORD v = 0xFFFFFFFF, size = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, subkey, name, RRF_RT_REG_DWORD, nullptr, &v, &size);
    return v;
}

bool KeyExists(const std::wstring& path) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    RegCloseKey(h);
    return true;
}

// The record exactly as older builds wrote it (see RegBackupValue in src/windows/registry.cpp).
bool WriteLegacyRecord(const std::wstring& path, DWORD original) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const std::wstring subkey = kSubkey, name = kValue;
    const DWORD hive = 1, present = 1, type = REG_DWORD;
    RegSetValueExW(h, L"subkey", 0, REG_SZ, reinterpret_cast<const BYTE*>(subkey.c_str()), static_cast<DWORD>((subkey.size() + 1) * 2));
    RegSetValueExW(h, L"name", 0, REG_SZ, reinterpret_cast<const BYTE*>(name.c_str()), static_cast<DWORD>((name.size() + 1) * 2));
    RegSetValueExW(h, L"hive", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&hive), sizeof(hive));
    RegSetValueExW(h, L"present", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&present), sizeof(present));
    RegSetValueExW(h, L"type", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&type), sizeof(type));
    RegSetValueExW(h, L"data", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&original), sizeof(original));
    RegCloseKey(h);
    return true;
}

} // namespace

void Test95() {
    const std::wstring legacy = std::wstring(kBackupRoot) + vegaflow::LegacyRegBackupRecordName(1, kSubkey, kValue);
    const std::wstring current = std::wstring(kBackupRoot) + vegaflow::RegBackupRecordName(1, kSubkey, kValue);
    const bool names_differ = legacy != current;

    // An older build backed up 7 and changed the value to 9.
    SetDword(kSubkey, kValue, 9);
    const bool planted = WriteLegacyRecord(legacy, 7);

    vegaflow::WindowsRegistryPort reg;
    const vegaflow::RegValue v = vegaflow::RegValue::MakeDword(11);
    const bool wrote = reg.WriteValue(vegaflow::RegHive::CurrentUser, kSubkey, kValue, v, true, "Test 95");
    const bool no_second_backup = !KeyExists(current);
    const bool restored = reg.RestoreBackedUpValue(vegaflow::RegHive::CurrentUser, kSubkey, kValue);
    const DWORD after = ReadDword(kSubkey, kValue);

    // Leave nothing behind.
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSubkey, 0, KEY_WRITE, &h) == ERROR_SUCCESS) {
        RegDeleteValueW(h, kValue);
        RegCloseKey(h);
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, legacy.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, current.c_str());

    RecordResult(names_differ && planted && wrote && no_second_backup && restored && after == 7,
                 "Test 95: a RegBackup record under the older name is still found and restored",
                 std::string("names differ: ") + (names_differ ? "yes" : "no") + ", second backup avoided: " +
                     (no_second_backup ? "yes" : "no") + ", restored: " + (restored ? "yes" : "no") +
                     ", value after restore: " + std::to_string(after) + " (7 = original)");
}

} // namespace vftest
