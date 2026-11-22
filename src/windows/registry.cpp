#include "windows/registry.hpp"
#include "windows/change_journal.hpp"
#include "windows/unique_handle.hpp"
#include "windows/reg_restore_scope.hpp"

#include <cstring>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <tlhelp32.h>

namespace vegaflow {

namespace {

constexpr wchar_t kRegBackupRoot[] = L"SOFTWARE\\VegaFlow\\RegBackup";

int HiveId(RegHive hive) {
    return (hive == RegHive::LocalMachine) ? 0 : 1;
}

HKEY HkeyFromId(int id) {
    return (id == 0) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

HKEY HkeyFromHive(RegHive hive) {
    return (hive == RegHive::LocalMachine) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

std::wstring ToLowerW(std::wstring_view str) {
    std::wstring result;
    result.reserve(str.size());
    for (wchar_t ch : str) {
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

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
    return std::string(hive == 0 ? "HKLM\\" : "HKCU\\") +
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
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && data.size() >= sizeof(wchar_t)) {
        std::wstring ws(reinterpret_cast<const wchar_t*>(data.data()), data.size() / sizeof(wchar_t));
        while (!ws.empty() && ws.back() == L'\0') ws.pop_back();
        return WideToUtf8Reg(ws);
    }
    return "0x" + EncodeHex(data);
}

std::string BuildManualRegHint(int hive, const std::wstring& subkey, const wchar_t* name,
                               bool present, DWORD type, const std::vector<BYTE>& data) {
    const std::string key_arg = std::string(hive == 0 ? "HKLM\\" : "HKCU\\") + WideToUtf8Reg(subkey);
    const std::string val_arg = WideToUtf8Reg(name ? name : L"");
    if (!present) {
        return "reg delete \"" + key_arg + "\" /v \"" + val_arg + "\" /f";
    }
    if (type == REG_DWORD && data.size() >= sizeof(DWORD)) {
        DWORD v = 0;
        std::memcpy(&v, data.data(), sizeof(DWORD));
        return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_DWORD /d " + std::to_string(v) + " /f";
    }
    if ((type == REG_SZ || type == REG_EXPAND_SZ) && data.size() >= sizeof(wchar_t)) {
        return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_SZ /d \"" +
               FormatRegPreview(present, type, data) + "\" /f";
    }
    return "reg add \"" + key_arg + "\" /v \"" + val_arg + "\" /t REG_BINARY /d " + EncodeHex(data) + " /f";
}

std::wstring RecordNameFor(int hive, const std::wstring& subkey, const wchar_t* name) {
    return RegBackupRecordName(hive, subkey, name ? name : L"");
}

bool RecordExists(int hive, const std::wstring& record) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(HkeyFromId(hive), (std::wstring(kRegBackupRoot) + L"\\" + record).c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(h);
    return true;
}

// A record an older build wrote under the std::hash name, if there is one: it holds the true original.
std::wstring ExistingLegacyRecord(int hive, const std::wstring& subkey, const wchar_t* name) {
    const std::wstring legacy = LegacyRegBackupRecordName(hive, subkey, name ? name : L"");
    return RecordExists(hive, legacy) ? legacy : std::wstring();
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

// True when the original is saved (now or earlier); false when it could not be saved, so the caller must not write.
bool RegBackupValue(int hive, const std::wstring& subkey, HKEY hKey, const wchar_t* name) {
    if (!ExistingLegacyRecord(hive, subkey, name).empty()) return true; // first backup already taken by an older build
    const std::wstring path = std::wstring(kRegBackupRoot) + L"\\" + RecordNameFor(hive, subkey, name);
    HKEY hb = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HkeyFromId(hive), path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_READ | KEY_WRITE, nullptr, &hb, &disposition) != ERROR_SUCCESS) {
        return false;
    }
    if (disposition == REG_OPENED_EXISTING_KEY) {
        RegCloseKey(hb);
        return true;
    }

    DWORD type = 0, size = 0;
    std::vector<BYTE> data;
    bool present = false;
    const LSTATUS probe = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
    if (probe == ERROR_SUCCESS) {
        data.resize(size);
        if (RegQueryValueExW(hKey, name, nullptr, &type, data.data(), &size) != ERROR_SUCCESS) {
            RegCloseKey(hb);
            RegDeleteKeyW(HkeyFromId(hive), path.c_str());
            return false;
        }
        data.resize(size);
        present = true;
    } else if (probe != ERROR_FILE_NOT_FOUND) {
        RegCloseKey(hb);
        RegDeleteKeyW(HkeyFromId(hive), path.c_str());
        return false;
    }

    // A record missing any field cannot restore the original, so every write must succeed.
    const bool fields_saved = RegWriteString(hb, L"subkey", subkey) && RegWriteString(hb, L"name", name) &&
                              RegWriteDword(hb, L"hive", static_cast<DWORD>(hive)) &&
                              RegWriteDword(hb, L"present", present ? 1 : 0) && RegWriteDword(hb, L"type", type);
    const BYTE empty = 0;
    const LSTATUS saved = fields_saved ? RegSetValueExW(hb, L"data", 0, REG_BINARY, data.empty() ? &empty : data.data(), static_cast<DWORD>(data.size()))
                                       : ERROR_WRITE_FAULT;
    RegCloseKey(hb);
    if (saved != ERROR_SUCCESS) {
        RegDeleteKeyW(HkeyFromId(hive), path.c_str());
        return false;
    }
    return true;
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

bool RegRestoreRecord(int hive_of_backup, const std::wstring& record) {
    const std::wstring path = std::wstring(kRegBackupRoot) + L"\\" + record;
    HKEY hb = nullptr;
    if (RegOpenKeyExW(HkeyFromId(hive_of_backup), path.c_str(), 0, KEY_READ, &hb) != ERROR_SUCCESS) return false;

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
    // The record is data from a user-writable key: refuse a target VegaFlow never edits or one in the other hive.
    if (!IsAllowedRestoreTarget(static_cast<int>(hive), hive_of_backup, subkey)) return false;

    HKEY target = nullptr;
    const REGSAM want = KEY_WRITE | KEY_READ;
    LSTATUS open = present ? RegCreateKeyExW(HkeyFromId(static_cast<int>(hive)), subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                             want, nullptr, &target, nullptr)
                           : RegOpenKeyExW(HkeyFromId(static_cast<int>(hive)), subkey.c_str(), 0, want, &target);
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
    } else if (!present && open == ERROR_FILE_NOT_FOUND) {
        restored = true;  // the key is gone, so is the value; any other error (access denied) leaves it pending
    }
    if (restored) {
        RegDeleteKeyW(HkeyFromId(hive_of_backup), path.c_str());
        JournalMarkRegRestored(static_cast<int>(hive), subkey, name.c_str());
    }
    return restored;
}

void QueryPerExeRegString(const wchar_t* subkey, const std::wstring& value_name, bool& out_present, std::wstring& out_val) {
    out_present = false;
    out_val.clear();
    HKEY h = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &h) != ERROR_SUCCESS) return;
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(h, value_name.c_str(), nullptr, &type, nullptr, &bytes) == ERROR_SUCCESS && type == REG_SZ) {
        std::wstring buf(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (RegQueryValueExW(h, value_name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &bytes) == ERROR_SUCCESS) {
            out_val.assign(buf.c_str());
            out_present = true;
        }
    }
    RegCloseKey(h);
}

} // namespace

bool QueryRegString(HKEY root, const wchar_t* sub_key, const wchar_t* value_name, std::wstring& out) {
    UniqueRegKey key;
    if (RegOpenKeyExW(root, sub_key, 0, KEY_READ, key.put()) != ERROR_SUCCESS) return false;
    wchar_t buf[1024]{};
    DWORD type = 0;
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegQueryValueExW(key.get(), value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS) return false;
    if (type != REG_SZ && type != REG_EXPAND_SZ) return false;
    out = buf;
    return true;
}

bool WindowsRegistryPort::ReadValue(RegHive hive, const std::wstring& subkey, const std::wstring& name, RegValue& out) {
    out = RegValue{};
    UniqueRegKey key;
    if (RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_READ, key.put()) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = 0;
    if (RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        return false;
    }

    if (type == REG_DWORD) {
        DWORD dw = 0;
        DWORD dw_size = sizeof(dw);
        if (RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&dw), &dw_size) != ERROR_SUCCESS) {
            return false;
        }
        out.type = RegValueType::Dword;
        out.dword = dw;
        return true;
    }

    if (type == REG_QWORD) {
        uint64_t qw = 0;
        DWORD qw_size = sizeof(qw);
        if (RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(&qw), &qw_size) != ERROR_SUCCESS) {
            return false;
        }
        out.type = RegValueType::Qword;
        out.qword = qw;
        return true;
    }

    if (type == REG_SZ || type == REG_EXPAND_SZ) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 2, L'\0');
        DWORD buf_bytes = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        if (RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &buf_bytes) != ERROR_SUCCESS) {
            return false;
        }
        out.type = RegValueType::String;
        out.str.assign(buf.data());
        return true;
    }

    std::vector<uint8_t> bytes(size);
    if (size > 0) {
        DWORD read_bytes = size;
        if (RegQueryValueExW(key.get(), name.c_str(), nullptr, &type, bytes.data(), &read_bytes) != ERROR_SUCCESS) {
            return false;
        }
        bytes.resize(read_bytes);
    }
    out.type = RegValueType::Binary;
    out.binary = std::move(bytes);
    return true;
}

bool WindowsRegistryPort::WriteValue(
    RegHive hive,
    const std::wstring& subkey,
    const std::wstring& name,
    const RegValue& val,
    bool backup_and_journal,
    const std::string& reason,
    const std::string& after_preview) {

    if (val.type == RegValueType::None) return false;

    UniqueRegKey key;
    LSTATUS st = RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_READ | KEY_SET_VALUE, key.put());
    if (st == ERROR_FILE_NOT_FOUND) {
        st = RegCreateKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                             KEY_READ | KEY_WRITE, nullptr, key.put(), nullptr);
    }
    if (st != ERROR_SUCCESS || !key) {
        return false;
    }

    const int hid = HiveId(hive);
    uint64_t jid = 0;
    if (backup_and_journal) {
        // No copy, no write: the user's value must never be lost.
        if (!RegBackupValue(hid, subkey, key.get(), name.c_str())) return false;
        jid = JournalBeginRegChange(hid, subkey, key.get(), name.c_str(), after_preview, reason);
    }

    LSTATUS set_status = ERROR_INVALID_PARAMETER;
    switch (val.type) {
        case RegValueType::Dword: {
            DWORD dw = val.dword;
            set_status = RegSetValueExW(key.get(), name.c_str(), 0, REG_DWORD,
                                        reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
            break;
        }
        case RegValueType::Qword: {
            uint64_t qw = val.qword;
            set_status = RegSetValueExW(key.get(), name.c_str(), 0, REG_QWORD,
                                        reinterpret_cast<const BYTE*>(&qw), sizeof(qw));
            break;
        }
        case RegValueType::String: {
            set_status = RegSetValueExW(key.get(), name.c_str(), 0, REG_SZ,
                                        reinterpret_cast<const BYTE*>(val.str.c_str()),
                                        static_cast<DWORD>((val.str.size() + 1) * sizeof(wchar_t)));
            break;
        }
        case RegValueType::Binary: {
            const BYTE empty = 0;
            set_status = RegSetValueExW(key.get(), name.c_str(), 0, REG_BINARY,
                                        val.binary.empty() ? &empty : val.binary.data(),
                                        static_cast<DWORD>(val.binary.size()));
            break;
        }
        case RegValueType::None:
        default:
            break;
    }

    if (backup_and_journal) {
        JournalEndRegChange(jid, set_status == ERROR_SUCCESS);
    }
    return (set_status == ERROR_SUCCESS);
}

bool WindowsRegistryPort::DeleteValue(
    RegHive hive,
    const std::wstring& subkey,
    const std::wstring& name,
    bool mark_journal_reverted) {

    UniqueRegKey key;
    LSTATUS st = RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_SET_VALUE, key.put());
    if (st == ERROR_FILE_NOT_FOUND) {
        if (mark_journal_reverted) {
            JournalMarkRegRestored(HiveId(hive), subkey, name.c_str());
        }
        return true;
    }
    if (st != ERROR_SUCCESS || !key) {
        return false;
    }
    const LSTATUS del = RegDeleteValueW(key.get(), name.c_str());
    const bool ok = (del == ERROR_SUCCESS || del == ERROR_FILE_NOT_FOUND);
    if (ok && mark_journal_reverted) {
        JournalMarkRegRestored(HiveId(hive), subkey, name.c_str());
    }
    return ok;
}

bool WindowsRegistryPort::RestoreBackedUpValue(RegHive hive, const std::wstring& subkey, const std::wstring& name) {
    const int hid = HiveId(hive);
    const std::wstring legacy = ExistingLegacyRecord(hid, subkey, name.c_str());
    const bool ok = RegRestoreRecord(hid, legacy.empty() ? RecordNameFor(hid, subkey, name.c_str()) : legacy);
    if (ok) {
        JournalMarkRegRestored(hid, subkey, name.c_str());
    }
    return ok;
}

bool WindowsRegistryPort::RestoreAllBackups(size_t* out_reverted, size_t* out_total) {
    bool all_ok = true;
    size_t reverted = 0;
    size_t total = 0;
    for (int hive : { 0, 1 }) {
        HKEY root = nullptr;
        if (RegOpenKeyExW(HkeyFromId(hive), kRegBackupRoot, 0, KEY_READ, &root) != ERROR_SUCCESS) continue;
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

std::vector<std::wstring> WindowsRegistryPort::EnumSubkeys(RegHive hive, const std::wstring& subkey) {
    std::vector<std::wstring> out;
    UniqueRegKey key;
    if (RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_READ, key.put()) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(key.get(), i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.emplace_back(name, len);
    }
    return out;
}

std::vector<std::pair<std::wstring, std::wstring>> WindowsRegistryPort::EnumStringValues(RegHive hive, const std::wstring& subkey) {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    UniqueRegKey key;
    if (RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_READ, key.put()) != ERROR_SUCCESS) {
        return out;
    }
    wchar_t val_name[1024];
    wchar_t val_data[1024];
    for (DWORD index = 0;; ++index) {
        DWORD name_len = 1024;
        DWORD val_type = 0;
        DWORD data_bytes = sizeof(val_data);
        const LSTATUS st = RegEnumValueW(key.get(), index, val_name, &name_len, nullptr, &val_type,
                                         reinterpret_cast<LPBYTE>(val_data), &data_bytes);
        if (st == ERROR_MORE_DATA) {
            continue; // a name or value too long for the buffer is skipped; the rest are still listed
        }
        if (st != ERROR_SUCCESS) {
            break;
        }
        if (val_type == REG_SZ || val_type == REG_EXPAND_SZ) {
            std::wstring data(val_data, data_bytes / sizeof(wchar_t));
            while (!data.empty() && data.back() == L'\0') data.pop_back();
            out.emplace_back(std::wstring(val_name, name_len), std::move(data));
        }
    }
    return out;
}

bool WindowsRegistryPort::KeyExists(RegHive hive, const std::wstring& subkey) {
    UniqueRegKey key;
    return RegOpenKeyExW(HkeyFromHive(hive), subkey.c_str(), 0, KEY_READ, key.put()) == ERROR_SUCCESS;
}

bool WindowsRegistryPort::IsMemoryCompressionEnabled() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    bool enabled = false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            std::wstring proc_name = ToLowerW(pe.szExeFile);
            if (proc_name.find(L"memcompression") != std::wstring::npos) {
                enabled = true;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return enabled;
}

namespace {

constexpr char kMemCompressionTarget[] = "MMAgent\\MemoryCompression";

bool RunMmAgent(bool enable);

// Pending/Committed MemoryCompression entries, oldest first.
std::vector<const JournalEntry*> OpenMemoryCompressionEntries(const ChangeJournal& journal) {
    std::vector<const JournalEntry*> open;
    for (const JournalEntry& e : journal.Entries()) {
        if (e.kind == JournalTargetKind::MemoryCompression &&
            (e.status == JournalEntryStatus::Pending || e.status == JournalEntryStatus::Committed)) {
            open.push_back(&e);
        }
    }
    return open;
}

} // namespace

bool WindowsRegistryPort::SetMemoryCompressionEnabled(bool enable, bool dry_run) {
    if (dry_run) {
        return true;
    }

    ChangeJournal* journal = GetActiveChangeJournal();
    uint64_t jid = 0;
    bool had_open_entry = false;
    bool oldest_orig_enabled = true;
    if (journal) {
        const bool was_enabled = IsMemoryCompressionEnabled();
        const auto open = OpenMemoryCompressionEntries(*journal);
        had_open_entry = !open.empty();
        if (had_open_entry) oldest_orig_enabled = open.front()->orig_mem_compression_enabled;
        // Turning it on is journaled too (unless it only undoes an open "off" entry), so it can be undone.
        if (!enable || (!was_enabled && !had_open_entry)) {
            JournalEntry e;
            e.kind = JournalTargetKind::MemoryCompression;
            e.target = kMemCompressionTarget;
            e.reason = enable ? "Activar compresión de memoria" : "Desactivar compresión de memoria";
            e.before_value = was_enabled ? "Activada" : "Desactivada";
            e.after_value = enable ? "Activada" : "Desactivada";
            e.manual_fix_hint = enable ? "powershell -Command \"Disable-MMAgent -MemoryCompression\""
                                       : "powershell -Command \"Enable-MMAgent -MemoryCompression\"";
            e.orig_mem_compression_enabled = was_enabled;
            jid = journal->RecordPending(std::move(e));
        }
    }

    const bool ok = RunMmAgent(enable);
    if (journal) {
        if (had_open_entry && jid == 0) {
            // Enabling with entries open: close them only if this reaches the original state; else they keep it for the revert.
            if (ok && EnablingClosesMemoryCompressionEntries(enable, oldest_orig_enabled)) {
                journal->MarkMatchingReverted(JournalTargetKind::MemoryCompression, kMemCompressionTarget);
            }
        } else if (jid != 0) {
            if (ok) journal->MarkCommitted(jid);
            else journal->MarkFailed(jid, "MMAgent devolvió error");
        }
    }
    return ok;
}

// Puts compression back to what the oldest open journal entry saw. No open entry: VegaFlow changed nothing, so it stays as it is.
bool WindowsRegistryPort::RevertMemoryCompressionChange(bool dry_run) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal) return true;
    const auto open = OpenMemoryCompressionEntries(*journal);
    if (open.empty() || dry_run) return true;
    const bool original = open.front()->orig_mem_compression_enabled;
    if (!RunMmAgent(original)) return false;
    journal->MarkMatchingReverted(JournalTargetKind::MemoryCompression, kMemCompressionTarget);
    return true;
}

namespace {

bool RunMmAgent(bool enable) {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"";
    cmd += enable ? L"Enable-MMAgent -MemoryCompression\"" : L"Disable-MMAgent -MemoryCompression\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buffer(cmd.begin(), cmd.end());
    cmd_buffer.push_back(L'\0');

    BOOL created = CreateProcessW(
        nullptr,
        cmd_buffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (!created) {
        return false;
    }

    HCURSOR hOldCursor = SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32650)));
    ULONGLONG start_tick = GetTickCount64();
    constexpr ULONGLONG kTimeoutMs = 5000;
    DWORD wait_res = WAIT_TIMEOUT;

    while (true) {
        ULONGLONG elapsed = GetTickCount64() - start_tick;
        if (elapsed >= kTimeoutMs) {
            wait_res = WAIT_TIMEOUT;
            break;
        }
        DWORD remaining = static_cast<DWORD>(kTimeoutMs - elapsed);
        DWORD mw = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, remaining, QS_ALLINPUT);
        if (mw == WAIT_OBJECT_0) {
            wait_res = WAIT_OBJECT_0;
            break;
        } else if (mw == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            wait_res = mw;
            break;
        }
    }
    if (hOldCursor) {
        SetCursor(hOldCursor);
    }
    DWORD exit_code = 1;
    if (wait_res == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return wait_res == WAIT_OBJECT_0 && exit_code == 0;
}

} // namespace

std::vector<ShaderCacheReport> WindowsRegistryPort::InspectShaderCaches() {
    std::vector<ShaderCacheReport> reports;

    wchar_t local_app_data[MAX_PATH] = {0};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return reports;
    }

    std::filesystem::path local_base(local_app_data);
    std::vector<std::filesystem::path> target_dirs = {
        local_base / L"D3DSCache",
        local_base / L"AMD" / L"DxCache",
        local_base / L"NVIDIA" / L"DXCache",
        local_base / L"AMD" / L"GLCache",
        local_base / L"NVIDIA" / L"GLCache",
        local_base / L"Intel" / L"ShaderCache"
    };

    for (const auto& dir : target_dirs) {
        ShaderCacheReport rep;
        rep.path = dir.wstring();
        rep.total_size_bytes = 0;
        rep.file_count = 0;

        std::error_code ec;
        if (std::filesystem::exists(dir, ec) && std::filesystem::is_directory(dir, ec)) {
            rep.exists = true;
            std::filesystem::recursive_directory_iterator it(
                dir,
                std::filesystem::directory_options::skip_permission_denied,
                ec
            );
            const std::filesystem::recursive_directory_iterator end;
            // increment(ec) instead of ++: a folder that disappears mid-scan must not throw out of the report.
            for (; !ec && it != end; it.increment(ec)) {
                const auto& entry = *it;
                std::error_code entry_ec;
                if (entry.is_regular_file(entry_ec)) {
                    rep.file_count++;
                    auto fsize = entry.file_size(entry_ec);
                    if (!entry_ec) {
                        rep.total_size_bytes += fsize;
                    }
                }
            }
        } else {
            rep.exists = false;
        }
        reports.push_back(rep);
    }

    return reports;
}

uint64_t WindowsRegistryPort::BeginDxgiPreferenceChange(
    const std::wstring& exe_path,
    const std::wstring& after_pref,
    const std::string& reason) {
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal || exe_path.empty()) return 0;

    JournalEntry e;
    e.kind = JournalTargetKind::DxgiGamePreference;
    e.target = "DXGI:" + WideToUtf8Reg(exe_path);
    e.reason = reason;
    e.exe_path = exe_path;
    QueryPerExeRegString(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", exe_path, e.ugp_orig_present, e.ugp_orig_value);
    QueryPerExeRegString(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers", exe_path,
                         e.layers_orig_present, e.layers_orig_value);
    e.before_value = e.ugp_orig_present ? WideToUtf8Reg(e.ugp_orig_value) : "(sin preferencia)";
    e.after_value = WideToUtf8Reg(after_pref);
    const std::string exe_utf8 = WideToUtf8Reg(exe_path);
    e.manual_fix_hint = e.ugp_orig_present
        ? ("reg add \"HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences\" /v \"" + exe_utf8 + "\" /t REG_SZ /d \"" +
           WideToUtf8Reg(e.ugp_orig_value) + "\" /f")
        : ("reg delete \"HKCU\\Software\\Microsoft\\DirectX\\UserGpuPreferences\" /v \"" + exe_utf8 + "\" /f");
    return journal->RecordPending(std::move(e));
}

void WindowsRegistryPort::EndDxgiPreferenceChange(uint64_t entry_id, bool succeeded) {
    if (entry_id == 0) return;
    ChangeJournal* journal = GetActiveChangeJournal();
    if (!journal) return;
    if (succeeded) journal->MarkCommitted(entry_id);
    else journal->MarkFailed(entry_id, "Error al escribir preferencia DXGI");
}

void WindowsRegistryPort::MarkDxgiPreferenceReverted(const std::wstring& exe_path) {
    if (ChangeJournal* journal = GetActiveChangeJournal()) {
        journal->MarkMatchingReverted(JournalTargetKind::DxgiGamePreference, "DXGI:" + WideToUtf8Reg(exe_path));
    }
}

} // namespace vegaflow
