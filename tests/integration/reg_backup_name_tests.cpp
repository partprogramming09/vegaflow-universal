// Integration test: a RegBackup record written under the older (std::hash) name is still found, so a later write does not
// back up the value VegaFlow had already changed, and restoring puts back the true original.

#include <windows.h>
#include <sddl.h>

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

// A record for a value that did NOT exist before VegaFlow wrote it (present = 0).
bool WriteAbsentRecord(const std::wstring& path, const std::wstring& subkey, const wchar_t* name) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const std::wstring nm = name;
    const DWORD hive = 1, present = 0, type = REG_NONE;
    RegSetValueExW(h, L"subkey", 0, REG_SZ, reinterpret_cast<const BYTE*>(subkey.c_str()), static_cast<DWORD>((subkey.size() + 1) * 2));
    RegSetValueExW(h, L"name", 0, REG_SZ, reinterpret_cast<const BYTE*>(nm.c_str()), static_cast<DWORD>((nm.size() + 1) * 2));
    RegSetValueExW(h, L"hive", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&hive), sizeof(hive));
    RegSetValueExW(h, L"present", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&present), sizeof(present));
    RegSetValueExW(h, L"type", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&type), sizeof(type));
    RegSetValueExW(h, L"data", 0, REG_BINARY, nullptr, 0);
    RegCloseKey(h);
    return true;
}

// Sets the key's DACL from an SDDL string (the owner can always change it back).
bool SetKeyDacl(const std::wstring& subkey, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    HKEY h = nullptr;
    bool ok = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, WRITE_DAC, &h) == ERROR_SUCCESS) {
        ok = RegSetKeySecurity(h, DACL_SECURITY_INFORMATION, sd) == ERROR_SUCCESS;
        RegCloseKey(h);
    }
    LocalFree(sd);
    return ok;
}

} // namespace

// F1.1: restoring a value that did not exist needs the key to be gone or the value deleted. "Access denied" on the key
// means the value may still be there, so the record and the journal must stay pending.
void Test121() {
    const std::wstring denied_key = std::wstring(kSubkey) + L"\\VfTest121Denied";
    const std::wstring missing_key = std::wstring(kSubkey) + L"\\VfTest121Missing";
    const wchar_t* name = L"VegaFlowTest121";
    const std::wstring rec_denied = std::wstring(kBackupRoot) + vegaflow::RegBackupRecordName(1, denied_key, name);
    const std::wstring rec_missing = std::wstring(kBackupRoot) + vegaflow::RegBackupRecordName(1, missing_key, name);

    HKEY k = nullptr;
    const bool made = RegCreateKeyExW(HKEY_CURRENT_USER, denied_key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS;
    if (k) RegCloseKey(k);
    const bool locked = made && SetKeyDacl(denied_key, L"D:(D;;KW;;;WD)(A;;KA;;;WD)");
    const bool planted = WriteAbsentRecord(rec_denied, denied_key, name) && WriteAbsentRecord(rec_missing, missing_key, name);

    vegaflow::WindowsRegistryPort reg;
    const bool denied_restored = reg.RestoreBackedUpValue(vegaflow::RegHive::CurrentUser, denied_key, name);
    const bool denied_record_kept = KeyExists(rec_denied);
    const bool missing_restored = reg.RestoreBackedUpValue(vegaflow::RegHive::CurrentUser, missing_key, name);
    const bool missing_record_gone = !KeyExists(rec_missing);
    const bool repeat_restored = reg.RestoreBackedUpValue(vegaflow::RegHive::CurrentUser, missing_key, name);

    SetKeyDacl(denied_key, L"D:(A;;KA;;;WD)");
    RegDeleteKeyW(HKEY_CURRENT_USER, denied_key.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, rec_denied.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, rec_missing.c_str());

    RecordResult(made && locked && planted && !denied_restored && denied_record_kept && missing_restored && missing_record_gone && !repeat_restored,
                 "Test 121: restoring an absent value without access to its key stays pending",
                 std::string("locked: ") + (locked ? "yes" : "no") + ", denied restored: " + (denied_restored ? "yes" : "no") +
                     ", record kept: " + (denied_record_kept ? "yes" : "no") + ", missing key restored: " + (missing_restored ? "yes" : "no") +
                     ", repeat: " + (repeat_restored ? "yes" : "no"));
}

void Test103() {
    const std::wstring legacy = std::wstring(kBackupRoot) + vegaflow::LegacyRegBackupRecordName(1, kSubkey, kValue);
    const std::wstring current = std::wstring(kBackupRoot) + vegaflow::RegBackupRecordName(1, kSubkey, kValue);
    const bool names_differ = legacy != current;

    // An older build backed up 7 and changed the value to 9.
    SetDword(kSubkey, kValue, 9);
    const bool planted = WriteLegacyRecord(legacy, 7);

    vegaflow::WindowsRegistryPort reg;
    const vegaflow::RegValue v = vegaflow::RegValue::MakeDword(11);
    const bool wrote = reg.WriteValue(vegaflow::RegHive::CurrentUser, kSubkey, kValue, v, true, "Test 103");
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
                 "Test 103: a RegBackup record under the older name is still found and restored",
                 std::string("names differ: ") + (names_differ ? "yes" : "no") + ", second backup avoided: " +
                     (no_second_backup ? "yes" : "no") + ", restored: " + (restored ? "yes" : "no") +
                     ", value after restore: " + std::to_string(after) + " (7 = original)");
}

} // namespace vftest
