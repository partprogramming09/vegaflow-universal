#include "windows/change_journal.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <sstream>

#include "windows/config_location.hpp"
#include "windows/unique_handle.hpp"

namespace vegaflow {

namespace {

std::atomic<ChangeJournal*> g_active_journal{nullptr};

constexpr char kMagicHeader[] = "VFJOURNAL\t1";
constexpr size_t kMaxHistoryEntries = 200;

std::string WideToUtf8Local(const std::wstring& w) {
    if (w.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::string EscapeField(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case ';': out += "\\s"; break;
            case '=': out += "\\e"; break;
            default: out.push_back(static_cast<char>(c)); break;
        }
    }
    return out;
}

bool UnescapeField(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\') {
            if (i + 1 >= in.size()) return false;
            const char nxt = in[++i];
            switch (nxt) {
                case '\\': out.push_back('\\'); break;
                case 't': out.push_back('\t'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 's': out.push_back(';'); break;
                case 'e': out.push_back('='); break;
                default: return false;
            }
        } else {
            out.push_back(in[i]);
        }
    }
    return true;
}

std::vector<std::string> SplitTab(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

bool ParseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long long val = std::strtoull(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;
    out = static_cast<uint64_t>(val);
    return true;
}

bool ParseU32(const std::string& s, uint32_t& out) {
    uint64_t v = 0;
    if (!ParseU64(s, v) || v > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool ParseBool01(const std::string& s, bool& out) {
    if (s == "0") { out = false; return true; }
    if (s == "1") { out = true; return true; }
    return false;
}

const char* KindToStr(JournalTargetKind k) {
    switch (k) {
        case JournalTargetKind::RegistryValue: return "REG";
        case JournalTargetKind::PowerPlan: return "PWR";
        case JournalTargetKind::MemoryCompression: return "MMCOMP";
        case JournalTargetKind::ProcessState: return "PROC";
        case JournalTargetKind::DxgiGamePreference: return "DXGI";
        case JournalTargetKind::GameConfigFile: return "GAMEINI";
        case JournalTargetKind::SystemRestorePoint: return "RESTOREPT";
        case JournalTargetKind::AuditEvent: return "AUDIT";
    }
    return "AUDIT";
}

bool StrToKind(const std::string& s, JournalTargetKind& out) {
    if (s == "REG") { out = JournalTargetKind::RegistryValue; return true; }
    if (s == "PWR") { out = JournalTargetKind::PowerPlan; return true; }
    if (s == "MMCOMP") { out = JournalTargetKind::MemoryCompression; return true; }
    if (s == "PROC") { out = JournalTargetKind::ProcessState; return true; }
    if (s == "DXGI") { out = JournalTargetKind::DxgiGamePreference; return true; }
    if (s == "GAMEINI") { out = JournalTargetKind::GameConfigFile; return true; }
    if (s == "RESTOREPT") { out = JournalTargetKind::SystemRestorePoint; return true; }
    if (s == "AUDIT") { out = JournalTargetKind::AuditEvent; return true; }
    return false;
}

const char* StatusToStr(JournalEntryStatus st) {
    switch (st) {
        case JournalEntryStatus::Pending: return "PENDING";
        case JournalEntryStatus::Committed: return "COMMITTED";
        case JournalEntryStatus::Reverted: return "REVERTED";
        case JournalEntryStatus::Failed: return "FAILED";
    }
    return "PENDING";
}

bool StrToStatus(const std::string& s, JournalEntryStatus& out) {
    if (s == "PENDING") { out = JournalEntryStatus::Pending; return true; }
    if (s == "COMMITTED") { out = JournalEntryStatus::Committed; return true; }
    if (s == "REVERTED") { out = JournalEntryStatus::Reverted; return true; }
    if (s == "FAILED") { out = JournalEntryStatus::Failed; return true; }
    return false;
}

std::string JoinU32List(const std::vector<uint32_t>& vals) {
    std::string out;
    for (size_t i = 0; i < vals.size(); ++i) {
        if (i > 0) out.push_back(',');
        out += std::to_string(vals[i]);
    }
    return out;
}

bool ParseU32List(const std::string& s, std::vector<uint32_t>& out) {
    out.clear();
    if (s.empty()) return true;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(',', start);
        std::string token = (pos == std::string::npos) ? s.substr(start) : s.substr(start, pos - start);
        uint32_t v = 0;
        if (!ParseU32(token, v)) return false;
        out.push_back(v);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return true;
}

std::string EncodeExtra(const JournalEntry& e) {
    auto kv = [](const char* k, const std::string& v) {
        return std::string(k) + "=" + EscapeField(v);
    };
    switch (e.kind) {
        case JournalTargetKind::RegistryValue:
            return kv("hive", std::to_string(e.hive)) + ";" +
                   kv("subkey", WideToUtf8Local(e.subkey)) + ";" +
                   kv("name", WideToUtf8Local(e.value_name)) + ";" +
                   kv("present", e.orig_present ? "1" : "0") + ";" +
                   kv("type", std::to_string(e.orig_type)) + ";" +
                   kv("data", e.orig_data_hex);
        case JournalTargetKind::PowerPlan:
            return kv("scheme", e.scheme_guid) + ";" +
                   kv("ac", JoinU32List(e.ac_values)) + ";" +
                   kv("dc", JoinU32List(e.dc_values));
        case JournalTargetKind::MemoryCompression:
            return kv("enabled", e.orig_mem_compression_enabled ? "1" : "0");
        case JournalTargetKind::ProcessState:
            return kv("pid", std::to_string(e.pid)) + ";" +
                   kv("ctime", std::to_string(e.process_creation_time)) + ";" +
                   kv("role", e.process_role) + ";" +
                   kv("prio", std::to_string(e.orig_priority_class));
        case JournalTargetKind::DxgiGamePreference:
            return kv("exe", WideToUtf8Local(e.exe_path)) + ";" +
                   kv("ugp_p", e.ugp_orig_present ? "1" : "0") + ";" +
                   kv("ugp_v", WideToUtf8Local(e.ugp_orig_value)) + ";" +
                   kv("lay_p", e.layers_orig_present ? "1" : "0") + ";" +
                   kv("lay_v", WideToUtf8Local(e.layers_orig_value));
        case JournalTargetKind::GameConfigFile:
            return kv("file", WideToUtf8Local(e.file_path)) + ";" +
                   kv("bak", WideToUtf8Local(e.backup_path));
        case JournalTargetKind::SystemRestorePoint:
        case JournalTargetKind::AuditEvent:
            return "";
    }
    return "";
}

bool DecodeExtra(const std::string& extra, JournalEntry& e) {
    if (extra.empty()) return true;
    size_t start = 0;
    while (start <= extra.size()) {
        size_t semi = extra.find(';', start);
        std::string item = (semi == std::string::npos) ? extra.substr(start) : extra.substr(start, semi - start);
        if (!item.empty()) {
            size_t eq = item.find('=');
            if (eq == std::string::npos) return false;
            std::string key = item.substr(0, eq);
            std::string raw_val = item.substr(eq + 1);
            std::string val;
            if (!UnescapeField(raw_val, val)) return false;

            if (key == "hive") {
                uint32_t h = 0;
                if (!ParseU32(val, h)) return false;
                e.hive = static_cast<int>(h);
            } else if (key == "subkey") {
                e.subkey = Utf8ToWideLocal(val);
            } else if (key == "name") {
                e.value_name = Utf8ToWideLocal(val);
            } else if (key == "present") {
                if (!ParseBool01(val, e.orig_present)) return false;
            } else if (key == "type") {
                if (!ParseU32(val, e.orig_type)) return false;
            } else if (key == "data") {
                e.orig_data_hex = val;
            } else if (key == "scheme") {
                e.scheme_guid = val;
            } else if (key == "ac") {
                if (!ParseU32List(val, e.ac_values)) return false;
            } else if (key == "dc") {
                if (!ParseU32List(val, e.dc_values)) return false;
            } else if (key == "enabled") {
                if (!ParseBool01(val, e.orig_mem_compression_enabled)) return false;
            } else if (key == "pid") {
                if (!ParseU32(val, e.pid)) return false;
            } else if (key == "ctime") {
                if (!ParseU64(val, e.process_creation_time)) return false;
            } else if (key == "role") {
                e.process_role = val;
            } else if (key == "prio") {
                if (!ParseU32(val, e.orig_priority_class)) return false;
            } else if (key == "exe") {
                e.exe_path = Utf8ToWideLocal(val);
            } else if (key == "ugp_p") {
                if (!ParseBool01(val, e.ugp_orig_present)) return false;
            } else if (key == "ugp_v") {
                e.ugp_orig_value = Utf8ToWideLocal(val);
            } else if (key == "lay_p") {
                if (!ParseBool01(val, e.layers_orig_present)) return false;
            } else if (key == "lay_v") {
                e.layers_orig_value = Utf8ToWideLocal(val);
            } else if (key == "file") {
                e.file_path = Utf8ToWideLocal(val);
            } else if (key == "bak") {
                e.backup_path = Utf8ToWideLocal(val);
            }
        }
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return true;
}

bool DecodeHexBytes(const std::string& hex, std::vector<BYTE>& out) {
    out.clear();
    if (hex.empty()) return true;
    if (hex.size() % 2 != 0) return false;
    out.reserve(hex.size() / 2);
    auto nibble = [](char c, BYTE& v) -> bool {
        if (c >= '0' && c <= '9') { v = static_cast<BYTE>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { v = static_cast<BYTE>(10 + (c - 'a')); return true; }
        if (c >= 'A' && c <= 'F') { v = static_cast<BYTE>(10 + (c - 'A')); return true; }
        return false;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        BYTE hi = 0, lo = 0;
        if (!nibble(hex[i], hi) || !nibble(hex[i + 1], lo)) return false;
        out.push_back(static_cast<BYTE>((hi << 4) | lo));
    }
    return true;
}

std::wstring ToLowerW(std::wstring_view str) {
    std::wstring out;
    out.reserve(str.size());
    for (wchar_t ch : str) out.push_back(static_cast<wchar_t>(std::towlower(ch)));
    return out;
}

std::wstring RegBackupRecordNameLocal(int hive, const std::wstring& subkey, const wchar_t* name) {
    const std::wstring id = std::to_wstring(hive) + L"|" + ToLowerW(subkey) + L"|" + ToLowerW(name ? name : L"");
    return std::to_wstring(std::hash<std::wstring>{}(id));
}

bool RevertRegistryEntry(const JournalEntry& e, std::string* out_err) {
    HKEY root = (e.hive == 0) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    if (e.subkey.empty() || e.value_name.empty()) {
        if (out_err) *out_err = "Entrada de registro incompleta en el diario";
        return false;
    }
    std::vector<BYTE> data;
    if (e.orig_present && !DecodeHexBytes(e.orig_data_hex, data)) {
        if (out_err) *out_err = "Datos hexadecimales inválidos en la entrada de registro";
        return false;
    }

    HKEY hTarget = nullptr;
    const REGSAM want = KEY_WRITE | KEY_READ;
    LSTATUS st = e.orig_present
        ? RegCreateKeyExW(root, e.subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, want, nullptr, &hTarget, nullptr)
        : RegOpenKeyExW(root, e.subkey.c_str(), 0, want, &hTarget);
    bool ok = false;
    if (st == ERROR_SUCCESS) {
        if (e.orig_present) {
            const BYTE empty = 0;
            st = RegSetValueExW(hTarget, e.value_name.c_str(), 0, e.orig_type,
                                data.empty() ? &empty : data.data(), static_cast<DWORD>(data.size()));
            ok = (st == ERROR_SUCCESS);
        } else {
            st = RegDeleteValueW(hTarget, e.value_name.c_str());
            ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
        }
        RegCloseKey(hTarget);
    } else if (!e.orig_present && st == ERROR_FILE_NOT_FOUND) {
        ok = true; // Subkey absent and value was originally absent
    }

    if (!ok) {
        if (out_err) *out_err = "No se pudo escribir/borrar la clave en el registro (código Windows " + std::to_string(st) + ")";
        return false;
    }

    // Remove matching legacy RegBackup key if present so both stay consistent.
    const std::wstring bak_path = std::wstring(L"SOFTWARE\\VegaFlow\\RegBackup\\") +
                                  RegBackupRecordNameLocal(e.hive, e.subkey, e.value_name.c_str());
    RegDeleteKeyW(root, bak_path.c_str());
    return true;
}

bool RevertPowerPlanEntry(const JournalEntry& e, std::string* out_err) {
    if (e.ac_values.size() != 7 || e.dc_values.size() != 7 || e.scheme_guid.empty()) {
        if (out_err) *out_err = "Datos del plan de energía incompletos en el diario";
        return false;
    }
    GUID scheme{};
    const std::wstring wguid = Utf8ToWideLocal(e.scheme_guid);
    if (FAILED(CLSIDFromString(wguid.c_str(), &scheme))) {
        if (out_err) *out_err = "GUID del plan de energía inválido";
        return false;
    }

    HMODULE hPowr = LoadLibraryW(L"powrprof.dll");
    if (!hPowr) {
        if (out_err) *out_err = "No se pudo cargar powrprof.dll";
        return false;
    }
    typedef DWORD(WINAPI* pfnPowerSetActiveScheme)(HKEY, const GUID*);
    typedef DWORD(WINAPI* pfnPowerWriteACValueIndex)(HKEY, const GUID*, const GUID*, const GUID*, DWORD);
    typedef DWORD(WINAPI* pfnPowerWriteDCValueIndex)(HKEY, const GUID*, const GUID*, const GUID*, DWORD);

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pSet = reinterpret_cast<pfnPowerSetActiveScheme>(GetProcAddress(hPowr, "PowerSetActiveScheme"));
    auto pWriteAc = reinterpret_cast<pfnPowerWriteACValueIndex>(GetProcAddress(hPowr, "PowerWriteACValueIndex"));
    auto pWriteDc = reinterpret_cast<pfnPowerWriteDCValueIndex>(GetProcAddress(hPowr, "PowerWriteDCValueIndex"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (!pSet || !pWriteAc) {
        FreeLibrary(hPowr);
        if (out_err) *out_err = "Faltan funciones en powrprof.dll";
        return false;
    }

    static const GUID GUID_PCIEXPRESS_SETTINGS_SUBGROUP = {0x501a4d13, 0x42af, 0x4429, {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
    static const GUID GUID_PCIEXPRESS_ASPM = {0xee12f906, 0xd277, 0x404b, {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
    static const GUID GUID_PROCESSOR_SETTINGS_SUBGROUP = {0x54533251, 0x82be, 0x4824, {0x96, 0xc1, 0x47, 0xb6, 0x0b, 0x74, 0x0d, 0x00}};
    static const GUID GUID_PROCESSOR_PERF_BOOST_MODE = {0xbe337238, 0x0d82, 0x4146, {0xa9, 0x60, 0x4f, 0x37, 0x49, 0xd4, 0x70, 0xc7}};
    static const GUID GUID_PROCESSOR_CORE_PARKING_MIN_CORES = {0x0cc5b647, 0xc1df, 0x4637, {0x89, 0x1a, 0xde, 0xc3, 0x5c, 0x31, 0x85, 0x83}};
    static const GUID GUID_PROCESSOR_FREQUENCY_MAX = {0x75b64e17, 0x1334, 0x44c1, {0x8c, 0xbe, 0xec, 0x61, 0x99, 0xea, 0xe7, 0x25}};
    static const GUID GUID_PROCESSOR_FREQUENCY_MAX_ALT = {0x75b0ae3f, 0xbce0, 0x45a7, {0x8c, 0x89, 0xc9, 0x61, 0x1c, 0x25, 0xe1, 0x00}};
    static const GUID GUID_PROCESSOR_THROTTLE_MAXIMUM = {0xbc5038f7, 0x23e0, 0x4960, {0x96, 0xda, 0x33, 0xab, 0xaf, 0x59, 0x35, 0xec}};
    static const GUID GUID_PROCESSOR_ENERGY_PERF_PREFERENCE = {0x36687f9e, 0xe3a5, 0x4dbf, {0xb1, 0xdc, 0x15, 0xeb, 0x38, 0x1c, 0x68, 0x63}};

    struct Pair { const GUID* sub; const GUID* set; };
    const Pair pairs[7] = {
        {&GUID_PCIEXPRESS_SETTINGS_SUBGROUP, &GUID_PCIEXPRESS_ASPM},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_PERF_BOOST_MODE},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_CORE_PARKING_MIN_CORES},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_FREQUENCY_MAX_ALT},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_THROTTLE_MAXIMUM},
        {&GUID_PROCESSOR_SETTINGS_SUBGROUP, &GUID_PROCESSOR_ENERGY_PERF_PREFERENCE},
    };

    for (size_t i = 0; i < 7; ++i) {
        pWriteAc(nullptr, &scheme, pairs[i].sub, pairs[i].set, e.ac_values[i]);
        if (pWriteDc) pWriteDc(nullptr, &scheme, pairs[i].sub, pairs[i].set, e.dc_values[i]);
    }
    const DWORD res = pSet(nullptr, &scheme);
    FreeLibrary(hPowr);
    if (res != ERROR_SUCCESS) {
        if (out_err) *out_err = "PowerSetActiveScheme devolvió error " + std::to_string(res);
        return false;
    }
    return true;
}

bool RevertMemoryCompressionEntry(const JournalEntry& e, std::string* out_err) {
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -WindowStyle Hidden -Command \"";
    cmd += e.orig_mem_compression_enabled ? L"Enable-MMAgent -MemoryCompression\"" : L"Disable-MMAgent -MemoryCompression\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (out_err) *out_err = "No se pudo lanzar PowerShell para restaurar MemoryCompression";
        return false;
    }
    const DWORD wait_res = WaitForSingleObject(pi.hProcess, 5000);
    DWORD exit_code = 1;
    if (wait_res == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &exit_code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (wait_res != WAIT_OBJECT_0 || exit_code != 0) {
        if (out_err) *out_err = "MMAgent devolvió error al restaurar MemoryCompression";
        return false;
    }
    return true;
}

bool RevertProcessStateEntry(const JournalEntry& e, std::string* out_err) {
    if (e.pid == 0) return true;
    UniqueHandle hProc(OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid));
    if (!hProc) {
        hProc.reset(OpenProcess(PROCESS_SET_LIMITED_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid));
    }
    if (!hProc) {
        // Check if process is dead -> OS already released all per-process state.
        UniqueHandle hQuery(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid));
        if (!hQuery) return true;
        DWORD code = 0;
        if (GetExitCodeProcess(hQuery.get(), &code) && code != STILL_ACTIVE) return true;
        if (out_err) *out_err = "Sin permiso para restaurar el proceso PID " + std::to_string(e.pid);
        return false;
    }

    if (e.process_creation_time != 0) {
        FILETIME c{}, x{}, k{}, u{};
        if (GetProcessTimes(hProc.get(), &c, &x, &k, &u)) {
            const uint64_t actual = (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
            if (actual != e.process_creation_time) {
                return true; // PID was reused by another process; original process already exited.
            }
        }
    }

    SetProcessDefaultCpuSets(hProc.get(), nullptr, 0);

    if (e.process_role == "game") {
        SetPriorityClass(hProc.get(), e.orig_priority_class != 0 ? e.orig_priority_class : NORMAL_PRIORITY_CLASS);
    }

    PROCESS_POWER_THROTTLING_STATE pwr{};
    pwr.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    pwr.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    pwr.StateMask = 0;
    SetProcessInformation(hProc.get(), ProcessPowerThrottling, &pwr, sizeof(pwr));

    MEMORY_PRIORITY_INFORMATION mem{};
    mem.MemoryPriority = 5; // MEMORY_PRIORITY_NORMAL
    SetProcessInformation(hProc.get(), ProcessMemoryPriority, &mem, sizeof(mem));

    typedef LONG(NTAPI* PFN_NtSetInformationProcess)(HANDLE, ULONG, PVOID, ULONG);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    static auto pfnNtSetInfo = reinterpret_cast<PFN_NtSetInformationProcess>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (pfnNtSetInfo) {
        ULONG io_normal = 2; // IO_PRIORITY_HINT IoPriorityNormal (1 is Low)
        pfnNtSetInfo(hProc.get(), 33 /* ProcessIoPriorityInformationClass */, &io_normal, sizeof(io_normal));
    }
    return true;
}

bool RevertDxgiEntry(const JournalEntry& e, std::string* out_err) {
    if (e.exe_path.empty()) return true;
    auto restore_str = [&](const wchar_t* subkey, bool present, const std::wstring& val) -> bool {
        HKEY h = nullptr;
        const LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, subkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                                           KEY_SET_VALUE | KEY_READ, nullptr, &h, nullptr);
        if (st != ERROR_SUCCESS) return !present;
        LSTATUS op = ERROR_SUCCESS;
        if (present) {
            op = RegSetValueExW(h, e.exe_path.c_str(), 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(val.c_str()),
                                static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
        } else {
            op = RegDeleteValueW(h, e.exe_path.c_str());
            if (op == ERROR_FILE_NOT_FOUND) op = ERROR_SUCCESS;
        }
        RegCloseKey(h);
        return op == ERROR_SUCCESS;
    };

    const bool ok1 = restore_str(L"Software\\Microsoft\\DirectX\\UserGpuPreferences", e.ugp_orig_present, e.ugp_orig_value);
    const bool ok2 = restore_str(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers",
                                 e.layers_orig_present, e.layers_orig_value);
    if (!ok1 || !ok2) {
        if (out_err) *out_err = "No se pudo restaurar UserGpuPreferences o AppCompatFlags\\Layers";
        return false;
    }
    return true;
}

bool RevertGameConfigEntry(FileSystem& fs, const JournalEntry& e, std::string* out_err) {
    if (e.file_path.empty() || e.backup_path.empty()) return true;
    if (!fs.Exists(e.backup_path)) {
        // Backup already restored or removed manually.
        return true;
    }
    std::string bak_bytes;
    if (!fs.Read(e.backup_path, bak_bytes)) {
        if (out_err) *out_err = "No se pudo leer la copia de seguridad del juego";
        return false;
    }
    if (bak_bytes.empty() || bak_bytes.find("; VegaFlow Empty Backup") != std::string::npos) {
        if (fs.Exists(e.file_path) && !fs.Remove(e.file_path)) {
            if (out_err) *out_err = "No se pudo borrar el archivo creado por VegaFlow";
            return false;
        }
        fs.Remove(e.backup_path);
        return true;
    }
    if (!fs.Copy(e.backup_path, e.file_path, true)) {
        if (out_err) *out_err = "No se pudo restaurar el archivo del juego desde .vegaflow.bak";
        return false;
    }
    std::string restored;
    if (!fs.Read(e.file_path, restored) || restored != bak_bytes) {
        if (out_err) *out_err = "La verificación byte a byte tras copiar falló; se conserva .vegaflow.bak";
        return false;
    }
    fs.Remove(e.backup_path);
    return true;
}

#ifndef BEGIN_SYSTEM_CHANGE
#define BEGIN_SYSTEM_CHANGE 100
#endif
#ifndef END_SYSTEM_CHANGE
#define END_SYSTEM_CHANGE 101
#endif
#ifndef APPLICATION_INSTALL
#define APPLICATION_INSTALL 0
#endif
#ifndef MAX_DESC
#define MAX_DESC 64
#endif
#ifndef MAX_DESC_W
#define MAX_DESC_W 256
#endif

#pragma pack(push, 1)
struct VfRestorePointInfoW {
    DWORD dwEventType;
    DWORD dwRestorePtType;
    INT64 llSequenceNumber;
    WCHAR szDescription[MAX_DESC_W];
};

struct VfStateMgrStatus {
    DWORD nStatus;
    INT64 llSequenceNumber;
};
#pragma pack(pop)

} // namespace

std::wstring JournalFilePath() {
    const std::wstring dir = ExecutableDirectory();
    return dir.empty() ? L"" : (std::filesystem::path(dir) / L"config" / L"change_journal.vfj").wstring();
}

void SetActiveChangeJournal(ChangeJournal* journal) {
    g_active_journal.store(journal, std::memory_order_release);
}

ChangeJournal* GetActiveChangeJournal() {
    return g_active_journal.load(std::memory_order_acquire);
}

uint64_t QueryProcessCreationTime(uint32_t pid) {
    if (pid == 0) return 0;
    UniqueHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return 0;
    FILETIME c{}, x{}, k{}, u{};
    if (!GetProcessTimes(h.get(), &c, &x, &k, &u)) return 0;
    return (static_cast<uint64_t>(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}

FileChangeJournal::FileChangeJournal(FileSystem& fs, Clock& clock, std::wstring path)
    : fs_(fs), clock_(clock), path_(std::move(path)) {}

std::string FileChangeJournal::FormatTimestampLocked() const {
    const std::time_t tt = std::chrono::system_clock::to_time_t(clock_.WallNow());
    std::tm tm_local{};
#ifdef _WIN32
    localtime_s(&tm_local, &tt);
#else
    localtime_r(&tt, &tm_local);
#endif
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday,
                  tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec);
    return buf;
}

void FileChangeJournal::PruneHistoryLocked() {
    if (entries_.size() <= kMaxHistoryEntries) return;
    auto is_prunable = [](const JournalEntry& e) {
        return e.status == JournalEntryStatus::Reverted ||
               e.kind == JournalTargetKind::AuditEvent ||
               e.kind == JournalTargetKind::SystemRestorePoint;
    };
    size_t completed_count = 0;
    for (const auto& e : entries_) {
        if (is_prunable(e)) completed_count++;
    }
    if (completed_count <= kMaxHistoryEntries) return;
    size_t skip_completed = completed_count - kMaxHistoryEntries;
    std::vector<JournalEntry> kept;
    kept.reserve(entries_.size() - skip_completed);
    for (auto& e : entries_) {
        if (is_prunable(e) && skip_completed > 0) {
            skip_completed--;
            continue;
        }
        kept.push_back(std::move(e));
    }
    entries_ = std::move(kept);
}

bool FileChangeJournal::PersistLocked() {
    if (path_.empty()) return false;
    PruneHistoryLocked();
    std::ostringstream os;
    os << kMagicHeader << "\n";
    os << "SESSION\tactive=" << (header_.active_session ? 1 : 0)
       << "\tunclean_streak=" << header_.consecutive_unclean_exits
       << "\trestore_point_prompted=" << (header_.restore_point_prompted ? 1 : 0) << "\n";
    for (const auto& e : entries_) {
        os << "ENTRY\t"
           << e.id << "\t"
           << EscapeField(e.timestamp) << "\t"
           << KindToStr(e.kind) << "\t"
           << StatusToStr(e.status) << "\t"
           << EscapeField(e.target) << "\t"
           << EscapeField(e.reason) << "\t"
           << EscapeField(e.before_value) << "\t"
           << EscapeField(e.after_value) << "\t"
           << EscapeField(e.manual_fix_hint) << "\t"
           << EncodeExtra(e) << "\n";
    }
    return fs_.Replace(path_, os.str());
}

bool FileChangeJournal::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    header_ = JournalHeader{};
    previous_unclean_exit_ = false;
    effective_unclean_streak_ = 0;
    safe_mode_ = false;
    next_id_ = 1;
    entries_.clear();

    if (path_.empty()) {
        header_.corrupt = true;
        header_.corrupt_reason = "Ruta del diario vacía";
        return false;
    }

    std::string raw;
    if (!fs_.Read(path_, raw)) {
        if (fs_.Exists(path_)) {
            header_.corrupt = true;
            header_.corrupt_reason = "No se pudo leer el archivo del diario de cambios";
            return false;
        }
        // Missing file -> fresh empty journal.
        return true;
    }

    if (raw.empty()) {
        return true;
    }

    std::istringstream is(raw);
    std::string line;
    if (!std::getline(is, line)) {
        header_.corrupt = true;
        header_.corrupt_reason = "Archivo del diario vacío o ilegible";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kMagicHeader) {
        header_.corrupt = true;
        header_.corrupt_reason = "Cabecera del diario desconocida o dañada";
        return false;
    }

    bool saw_session = false;
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        auto cols = SplitTab(line);
        if (cols[0] == "SESSION") {
            if (saw_session || cols.size() < 4) {
                header_.corrupt = true;
                header_.corrupt_reason = "Línea SESSION repetida o incompleta en el diario";
                entries_.clear();
                return false;
            }
            auto read_kv = [](const std::string& token, const char* prefix, std::string& val) -> bool {
                const size_t plen = std::strlen(prefix);
                if (token.size() <= plen || token.compare(0, plen, prefix) != 0) return false;
                val = token.substr(plen);
                return true;
            };
            std::string v1, v2, v3;
            if (!read_kv(cols[1], "active=", v1) || !ParseBool01(v1, header_.active_session) ||
                !read_kv(cols[2], "unclean_streak=", v2) || !ParseU32(v2, header_.consecutive_unclean_exits) ||
                !read_kv(cols[3], "restore_point_prompted=", v3) || !ParseBool01(v3, header_.restore_point_prompted)) {
                header_.corrupt = true;
                header_.corrupt_reason = "Valores SESSION corruptos en el diario";
                entries_.clear();
                return false;
            }
            saw_session = true;
        } else if (cols[0] == "ENTRY") {
            if (cols.size() != 11) {
                header_.corrupt = true;
                header_.corrupt_reason = "Entrada malformada en el diario de cambios";
                entries_.clear();
                return false;
            }
            JournalEntry e;
            if (!ParseU64(cols[1], e.id) ||
                !UnescapeField(cols[2], e.timestamp) ||
                !StrToKind(cols[3], e.kind) ||
                !StrToStatus(cols[4], e.status) ||
                !UnescapeField(cols[5], e.target) ||
                !UnescapeField(cols[6], e.reason) ||
                !UnescapeField(cols[7], e.before_value) ||
                !UnescapeField(cols[8], e.after_value) ||
                !UnescapeField(cols[9], e.manual_fix_hint) ||
                !DecodeExtra(cols[10], e)) {
                header_.corrupt = true;
                header_.corrupt_reason = "Campos corruptos en una entrada del diario";
                entries_.clear();
                return false;
            }
            if (e.id >= next_id_) next_id_ = e.id + 1;
            entries_.push_back(std::move(e));
        } else {
            header_.corrupt = true;
            header_.corrupt_reason = "Registro desconocido en el diario de cambios";
            entries_.clear();
            return false;
        }
    }

    if (!saw_session) {
        header_.corrupt = true;
        header_.corrupt_reason = "Falta la línea SESSION en el diario de cambios";
        entries_.clear();
        return false;
    }

    if (header_.active_session) {
        previous_unclean_exit_ = true;
        effective_unclean_streak_ = header_.consecutive_unclean_exits + 1;
    } else {
        effective_unclean_streak_ = header_.consecutive_unclean_exits;
    }
    safe_mode_ = (effective_unclean_streak_ >= 2);
    return true;
}

bool FileChangeJournal::BeginSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return false;
    header_.active_session = true;
    header_.consecutive_unclean_exits = effective_unclean_streak_;
    return PersistLocked();
}

bool FileChangeJournal::EndSessionClean() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return false;
    header_.active_session = false;
    header_.consecutive_unclean_exits = 0;
    previous_unclean_exit_ = false;
    effective_unclean_streak_ = 0;
    safe_mode_ = false;
    return PersistLocked();
}

bool FileChangeJournal::ResetRecoveryState() {
    std::lock_guard<std::mutex> lock(mutex_);
    header_.corrupt = false;
    header_.corrupt_reason.clear();
    header_.active_session = false;
    header_.consecutive_unclean_exits = 0;
    previous_unclean_exit_ = false;
    effective_unclean_streak_ = 0;
    safe_mode_ = false;
    return PersistLocked();
}

bool FileChangeJournal::SetRestorePointPrompted(bool prompted) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return false;
    header_.restore_point_prompted = prompted;
    return PersistLocked();
}

uint64_t FileChangeJournal::RecordPending(JournalEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return 0;
    entry.id = next_id_++;
    if (entry.timestamp.empty()) entry.timestamp = FormatTimestampLocked();
    entry.status = JournalEntryStatus::Pending;
    entries_.push_back(std::move(entry));
    const uint64_t id = entries_.back().id;
    if (!PersistLocked()) {
        entries_.pop_back();
        return 0;
    }
    return id;
}

bool FileChangeJournal::MarkCommitted(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id == 0 || header_.corrupt) return false;
    for (auto& e : entries_) {
        if (e.id == id) {
            e.status = JournalEntryStatus::Committed;
            return PersistLocked();
        }
    }
    return false;
}

bool FileChangeJournal::MarkReverted(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id == 0 || header_.corrupt) return false;
    for (auto& e : entries_) {
        if (e.id == id) {
            e.status = JournalEntryStatus::Reverted;
            return PersistLocked();
        }
    }
    return false;
}

bool FileChangeJournal::MarkFailed(uint64_t id, const std::string& failure_note) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id == 0 || header_.corrupt) return false;
    for (auto& e : entries_) {
        if (e.id == id) {
            e.status = JournalEntryStatus::Failed;
            if (!failure_note.empty()) e.after_value = failure_note;
            return PersistLocked();
        }
    }
    return false;
}

bool FileChangeJournal::MarkMatchingReverted(JournalTargetKind kind, const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return false;
    bool changed = false;
    for (auto& e : entries_) {
        if (e.kind == kind && e.target == target &&
            (e.status == JournalEntryStatus::Pending || e.status == JournalEntryStatus::Committed)) {
            e.status = JournalEntryStatus::Reverted;
            changed = true;
        }
    }
    return changed ? PersistLocked() : true;
}

uint64_t FileChangeJournal::RecordAuditEvent(const std::string& target, const std::string& reason, const std::string& details) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (header_.corrupt) return 0;
    JournalEntry e;
    e.id = next_id_++;
    e.timestamp = FormatTimestampLocked();
    e.kind = JournalTargetKind::AuditEvent;
    e.status = JournalEntryStatus::Committed;
    e.target = target;
    e.reason = reason;
    e.after_value = details;
    entries_.push_back(std::move(e));
    const uint64_t id = entries_.back().id;
    PersistLocked();
    return id;
}

const JournalHeader& FileChangeJournal::Header() const { return header_; }
bool FileChangeJournal::DetectedPreviousUncleanExit() const { return previous_unclean_exit_; }
uint32_t FileChangeJournal::EffectiveUncleanStreak() const { return effective_unclean_streak_; }
bool FileChangeJournal::IsSafeModeTriggered() const { return safe_mode_; }
const std::vector<JournalEntry>& FileChangeJournal::Entries() const { return entries_; }

std::vector<JournalEntry> FileChangeJournal::SnapshotEntries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

bool WindowsSystemUndo::RevertEntry(const JournalEntry& entry, std::string* out_error) {
    switch (entry.kind) {
        case JournalTargetKind::RegistryValue:
            return RevertRegistryEntry(entry, out_error);
        case JournalTargetKind::PowerPlan:
            return RevertPowerPlanEntry(entry, out_error);
        case JournalTargetKind::MemoryCompression:
            return RevertMemoryCompressionEntry(entry, out_error);
        case JournalTargetKind::ProcessState:
            return RevertProcessStateEntry(entry, out_error);
        case JournalTargetKind::DxgiGamePreference:
            return RevertDxgiEntry(entry, out_error);
        case JournalTargetKind::GameConfigFile:
            return RevertGameConfigEntry(fs_, entry, out_error);
        case JournalTargetKind::SystemRestorePoint:
        case JournalTargetKind::AuditEvent:
            return true;
    }
    return true;
}

bool WindowsSystemUndo::CreateRestorePoint(const std::string& description, std::string* out_message) {
    HMODULE hSr = LoadLibraryW(L"srclient.dll");
    if (!hSr) {
        if (out_message) *out_message = "srclient.dll no disponible en este sistema.";
        return false;
    }
    typedef BOOL(WINAPI* pfnSRSetRestorePointW)(const VfRestorePointInfoW*, VfStateMgrStatus*);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto pfn = reinterpret_cast<pfnSRSetRestorePointW>(GetProcAddress(hSr, "SRSetRestorePointW"));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (!pfn) {
        FreeLibrary(hSr);
        if (out_message) *out_message = "SRSetRestorePointW no encontrado en srclient.dll.";
        return false;
    }

    VfRestorePointInfoW rp{};
    rp.dwEventType = BEGIN_SYSTEM_CHANGE;
    rp.dwRestorePtType = APPLICATION_INSTALL;
    rp.llSequenceNumber = 0;
    const std::wstring wdesc = Utf8ToWideLocal(description.empty() ? "VegaFlow Universal - Antes de cambios del registro" : description);
    wcsncpy_s(rp.szDescription, wdesc.c_str(), MAX_DESC_W - 1);

    VfStateMgrStatus status{};
    if (!pfn(&rp, &status)) {
        const DWORD err = status.nStatus != 0 ? status.nStatus : GetLastError();
        FreeLibrary(hSr);
        if (out_message) {
            if (err == ERROR_SERVICE_DISABLED) {
                *out_message = "La Protección del sistema de Windows está apagada; no se creó punto de restauración.";
            } else if (err == ERROR_ACCESS_DENIED) {
                *out_message = "Se necesitan permisos de administrador para crear un punto de restauración.";
            } else {
                *out_message = "Windows no creó el punto de restauración (código " + std::to_string(err) + ").";
            }
        }
        return false;
    }

    VfRestorePointInfoW rp_end{};
    rp_end.dwEventType = END_SYSTEM_CHANGE;
    rp_end.dwRestorePtType = APPLICATION_INSTALL;
    rp_end.llSequenceNumber = status.llSequenceNumber;
    pfn(&rp_end, &status);
    FreeLibrary(hSr);

    if (out_message) {
        *out_message = "Punto de restauración de Windows creado (secuencia #" +
                       std::to_string(status.llSequenceNumber) + ").";
    }
    return true;
}

} // namespace vegaflow
