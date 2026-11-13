#include "optimize/graphics/graphics_internal.hpp"
#include "windows/change_journal.hpp"

#include <cstdio>

namespace vegaflow::graphics_internal {

// ---- Registry value backup --------------------------------------------------
// Before changing a value, its original state (or its absence) is saved under SOFTWARE\VegaFlow\RegBackup of the
// same hive; the first backup is never overwritten, so a shutdown without restoring does not lose the original value.

namespace {

constexpr wchar_t kRegBackupRoot[] = L"SOFTWARE\\VegaFlow\\RegBackup";

HKEY HiveFromId(int id) { return id == kHiveLocalMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }

std::string WideToUtf8Reg(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string EncodeHex(const std::vector<BYTE>& bytes) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (BYTE b : bytes) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

std::string RegTargetName(int hive, const std::wstring& subkey, const wchar_t* name) {
    return std::string(hive == kHiveLocalMachine ? "HKLM\\" : "HKCU\\") +
           WideToUtf8Reg(subkey) + "\\" + WideToUtf8Reg(name ? name : L"");
}

std::string FormatRegPreview(bool present, DWORD type, const std::vector<BYTE>& data) {
    if (!present) return "(ausente)";
    if (type == REG_DWORD && data.size() >= sizeof(DWORD)) {
        DWORD v = 0;
        std::memcpy(&v, data.data(), sizeof(DWORD));
        return std::to_string(v);
    }
    if (type == REG_QWORD && data.size() >= sizeof(uint64_t)) {
        uint64_t v = 0;
        std::memcpy(&v, data.data(), sizeof(uint64_t));
        return std::to_string(v);
    }
    if (type == REG_SZ && data.size() >= sizeof(wchar_t)) {
        std::wstring ws(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
        while (!ws.empty() && ws.back() == L'\0') ws.pop_back();
        return WideToUtf8Reg(ws);
    }
    return "0x" + EncodeHex(data);
}

std::string BuildManualRegHint(int hive, const std::wstring& subkey, const wchar_t* name,
                               bool present, DWORD type, const std::vector<BYTE>& data) {
    const std::string key_arg = std::string(hive == kHiveLocalMachine ? "HKLM\\" : "HKCU\\") + WideToUtf8Reg(subkey);
    const std::string val_arg = WideToUtf8Reg(name ? name : L"");
    if (!present) {
        return "reg delete \"" + key_arg + "\" /v \"" + val_arg + "\" /f";
    }
    if (type == REG_DWORD && data.size() >= sizeof(DWORD)) {
        DWORD v = 0;
        std::memcpy(&v, data.data(), sizeof(DWORD));
        return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_DWORD /d " + std::to_string(v) + " /f";
    }
    if (type == REG_SZ && data.size() >= sizeof(wchar_t)) {
        return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_SZ /d \"" +
               FormatRegPreview(present, type, data) + "\" /f";
    }
    return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_BINARY /d " + EncodeHex(data) + " /f";
}

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

uint64_t JournalBeginRegChange(int hive, const std::wstring& subkey, HKEY hKey, const wchar_t* name,
                               const std::string& after_value, const std::string& reason) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal || !hKey || !name) return 0;

    DWORD type = 0, size = 0;
    std::vector<BYTE> data;
    bool present = false;
    const LSTATUS probe = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
    if (probe == ERROR_SUCCESS) {
        data.resize(size);
        if (RegQueryValueExW(hKey, name, nullptr, &type, data.data(), &size) != ERROR_SUCCESS) {
            return 0;
        }
        data.resize(size);
        present = true;
    } else if (probe != ERROR_FILE_NOT_FOUND) {
        return 0;
    }

    JournalEntry e;
    e.kind = JournalTargetKind::RegistryValue;
    e.target = RegTargetName(hive, subkey, name);
    e.reason = reason;
    e.before_value = FormatRegPreview(present, type, data);
    e.after_value = after_value;
    e.manual_fix_hint = BuildManualRegHint(hive, subkey, name, present, type, data);
    e.hive = hive;
    e.subkey = subkey;
    e.value_name = name;
    e.orig_present = present;
    e.orig_type = type;
    e.orig_data_hex = EncodeHex(data);
    return journal->RecordPending(std::move(e));
}

void JournalEndRegChange(uint64_t entry_id, bool succeeded) {
    if (entry_id == 0) return;
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal) return;
    if (succeeded) {
        journal->MarkCommitted(entry_id);
    } else {
        journal->MarkFailed(entry_id, "Error al escribir en el registro");
    }
}

void JournalMarkRegRestored(int hive, const std::wstring& subkey, const wchar_t* name) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal || !name) return;
    journal->MarkMatchingReverted(JournalTargetKind::RegistryValue, RegTargetName(hive, subkey, name));
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
    if (restored) {
        RegDeleteKeyW(HiveFromId(hive_of_backup), path.c_str());
        JournalMarkRegRestored(static_cast<int>(hive), subkey, name.c_str());
    }
    return restored;
}

} // namespace

/// Restores one specific value from its backup; false if there was no backup or it could not be restored.
bool RegRestoreValue(int hive, const std::wstring& subkey, const wchar_t* name) {
    const bool ok = RegRestoreRecord(hive, RegBackupRecordName(hive, subkey, name));
    if (ok) JournalMarkRegRestored(hive, subkey, name);
    return ok;
}

} // namespace vegaflow::graphics_internal

namespace vegaflow {

using namespace graphics_internal;

bool GraphicsOptimizer::RestoreAllRegistryBackups(size_t* out_reverted, size_t* out_total) {
    bool all_ok = true;
    size_t reverted = 0;
    size_t total = 0;
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
        total += records.size();
        for (const auto& r : records) {
            if (RegRestoreRecord(hive, r)) {
                reverted++;
            } else {
                all_ok = false;
            }
        }
    }
    if (out_reverted) *out_reverted = reverted;
    if (out_total) *out_total = total;
    return all_ok;
}

} // namespace vegaflow
