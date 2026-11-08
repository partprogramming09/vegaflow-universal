#include "optimize/graphics/graphics_internal.hpp"

namespace vegaflow::graphics_internal {

// ---- Registry value backup --------------------------------------------------
// Before changing a value, its original state (or its absence) is saved under SOFTWARE\VegaFlow\RegBackup of the
// same hive; the first backup is never overwritten, so a shutdown without restoring does not lose the original value.

namespace {

constexpr wchar_t kRegBackupRoot[] = L"SOFTWARE\\VegaFlow\\RegBackup";

HKEY HiveFromId(int id) { return id == kHiveLocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

/// Stable name of a value's backup record.
std::wstring RegBackupRecordName(int hive, const std::wstring& subkey, const wchar_t* name) {
    const std::wstring id = std::to_wstring(hive) + L"|" + ToLower(subkey) + L"|" + ToLower(name);
    return std::to_wstring(std::hash<std::wstring>{}(id));
}

bool RegWriteString(HKEY h, const wchar_t* name, const std::wstring& v) {
    return RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                          static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool RegWriteDword(HKEY h, const wchar_t* name, DWORD v) {
    return RegSetValueExW(h, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v)) == ERROR_SUCCESS;
}

bool RegReadString(HKEY h, const wchar_t* name, std::wstring& out) {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(h, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) return false;
    std::wstring buf(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
    if (RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(buf.data()), &size) != ERROR_SUCCESS) return false;
    out.assign(buf.c_str());
    return true;
}

bool RegReadDword(HKEY h, const wchar_t* name, DWORD& out) {
    DWORD type = 0, size = sizeof(out);
    return RegQueryValueExW(h, name, nullptr, &type, reinterpret_cast<BYTE*>(&out), &size) == ERROR_SUCCESS && type == REG_DWORD;
}


} // namespace

std::wstring ToLower(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t ch : str) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

/// Saves value `name` of `hKey` (opened at `subkey` of the given hive) if there is no backup yet.
void RegBackupValue(int hive, const std::wstring& subkey, HKEY hKey, const wchar_t* name) {
    const std::wstring path = std::wstring(kRegBackupRoot) + L"\\" + RegBackupRecordName(hive, subkey, name);
    HKEY hb = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HiveFromId(hive), path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_READ | KEY_WRITE, nullptr, &hb, &disposition) != ERROR_SUCCESS) {
        return;
    }
    if (disposition == REG_OPENED_EXISTING_KEY) {
        RegCloseKey(hb);
        return;
    }

    DWORD type = 0, size = 0;
    std::vector<BYTE> data;
    bool present = false;
    const LSTATUS probe = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
    if (probe == ERROR_SUCCESS) {
        data.resize(size);
        if (RegQueryValueExW(hKey, name, nullptr, &type, data.data(), &size) != ERROR_SUCCESS) {
            // If the existing value cannot be read it is not recorded as absent: the backup is discarded.
            RegCloseKey(hb);
            RegDeleteKeyW(HiveFromId(hive), path.c_str());
            return;
        }
        data.resize(size);
        present = true;
    } else if (probe != ERROR_FILE_NOT_FOUND) {
        RegCloseKey(hb);
        RegDeleteKeyW(HiveFromId(hive), path.c_str());
        return;
    }

    RegWriteString(hb, L"subkey", subkey);
    RegWriteString(hb, L"name", name);
    RegWriteDword(hb, L"hive", static_cast<DWORD>(hive));
    RegWriteDword(hb, L"present", present ? 1 : 0);
    RegWriteDword(hb, L"type", type);
    const BYTE empty = 0;
    RegSetValueExW(hb, L"data", 0, REG_BINARY, data.empty() ? &empty : data.data(), static_cast<DWORD>(data.size()));
    RegCloseKey(hb);
}

namespace {

/// Returns a value to its original state according to backup record `record` and deletes that record.
bool RegRestoreRecord(int hive_of_backup, const std::wstring& record) {
    const std::wstring path = std::wstring(kRegBackupRoot) + L"\\" + record;
    HKEY hb = nullptr;
    if (RegOpenKeyExW(HiveFromId(hive_of_backup), path.c_str(), 0, KEY_READ, &hb) != ERROR_SUCCESS) return false;

    std::wstring subkey, name;
    DWORD hive = 0, present = 0, type = 0;
    std::vector<BYTE> data;
    bool ok = RegReadString(hb, L"subkey", subkey) && RegReadString(hb, L"name", name) &&
              RegReadDword(hb, L"hive", hive) && RegReadDword(hb, L"present", present) && RegReadDword(hb, L"type", type);
    if (ok) {
        DWORD dtype = 0, size = 0;
        if (RegQueryValueExW(hb, L"data", nullptr, &dtype, nullptr, &size) == ERROR_SUCCESS) {
            data.resize(size);
            ok = RegQueryValueExW(hb, L"data", nullptr, &dtype, data.data(), &size) == ERROR_SUCCESS;
            data.resize(size);
        }
    }
    RegCloseKey(hb);
    if (!ok) return false;

    HKEY target = nullptr;
    const REGSAM want = KEY_WRITE | KEY_READ;
    LSTATUS open = present ? RegCreateKeyExW(HiveFromId(static_cast<int>(hive)), subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                             want, nullptr, &target, nullptr)
                           : RegOpenKeyExW(HiveFromId(static_cast<int>(hive)), subkey.c_str(), 0, want, &target);
    bool restored = false;
    if (open == ERROR_SUCCESS) {
        if (present) {
            const BYTE empty = 0;
            restored = RegSetValueExW(target, name.c_str(), 0, type, data.empty() ? &empty : data.data(),
                                      static_cast<DWORD>(data.size())) == ERROR_SUCCESS;
        } else {
            const LSTATUS del = RegDeleteValueW(target, name.c_str());
            restored = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
        }
        RegCloseKey(target);
    } else if (!present) {
        restored = true; // the key no longer exists, so the absent value is already as it was
    }
    if (restored) RegDeleteKeyW(HiveFromId(hive_of_backup), path.c_str());
    return restored;
}

} // namespace

/// Restores one specific value from its backup; false if there was no backup or it could not be restored.
bool RegRestoreValue(int hive, const std::wstring& subkey, const wchar_t* name) {
    return RegRestoreRecord(hive, RegBackupRecordName(hive, subkey, name));
}

} // namespace vegaflow::graphics_internal

namespace vegaflow {

using namespace graphics_internal;

bool GraphicsOptimizer::RestoreAllRegistryBackups() {
    bool all_ok = true;
    for (int hive : { kHiveLocalMachine, kHiveCurrentUser }) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HiveFromId(hive), kRegBackupRoot, 0, KEY_READ, &root) != ERROR_SUCCESS) continue;
        std::vector<std::wstring> records;
        wchar_t buf[256];
        for (DWORD i = 0;; ++i) {
            DWORD len = 256;
            if (RegEnumKeyExW(root, i, buf, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            records.emplace_back(buf, len);
        }
        RegCloseKey(root);
        for (const auto& r : records) {
            if (!RegRestoreRecord(hive, r)) all_ok = false;
        }
    }
    return all_ok;
}

} // namespace vegaflow
