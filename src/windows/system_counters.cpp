#include "windows/system_counters.hpp"

#include <intrin.h>
#include <tlhelp32.h>

#include <cwctype>
#include <string>
#include <vector>

namespace vegaflow {

uint32_t CountEcoQoSProcesses() {
    uint32_t count = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID <= 4) continue;
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    PROCESS_POWER_THROTTLING_STATE throttling{};
                    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
                    if (GetProcessInformation(hProc, ProcessPowerThrottling, &throttling, sizeof(throttling))) {
                        if (throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) {
                            count++;
                        }
                    }
                    CloseHandle(hProc);
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return count;
}

uint32_t CountDirectFlipConfiguredGames() {
    uint32_t count = 0;
    HKEY hPref = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, KEY_READ, &hPref) == ERROR_SUCCESS) {
        DWORD index = 0;
        wchar_t val_name[MAX_PATH];
        DWORD val_name_len = MAX_PATH;
        DWORD val_type = 0;
        wchar_t val_data[512];
        DWORD val_data_len = sizeof(val_data);
        while (RegEnumValueW(hPref, index++, val_name, &val_name_len, nullptr, &val_type, reinterpret_cast<LPBYTE>(val_data), &val_data_len) == ERROR_SUCCESS) {
            if (val_type == REG_SZ) {
                std::wstring data_str(val_data);
                for (auto& c : data_str) c = static_cast<wchar_t>(std::towlower(c));
                if (data_str.find(L"swapeffectupgradeenable=1") != std::wstring::npos) {
                    count++;
                }
            }
            val_name_len = MAX_PATH;
            val_data_len = sizeof(val_data);
        }
        RegCloseKey(hPref);
    }
    return count;
}

uint32_t CountDimmsInSmbiosTable(const uint8_t* table, size_t len) {
    uint32_t populated = 0;
    const uint8_t* ptr = table;
    const uint8_t* end = table + len;
    while (ptr + 4 <= end) {
        const uint8_t type = ptr[0];
        const uint8_t length = ptr[1];
        if (length < 4 || type == 127 || ptr + length > end) break;
        if (type == 17 && length >= 0x0E) {
            const uint16_t size_val = static_cast<uint16_t>(ptr[0x0C] | (ptr[0x0D] << 8));
            if (size_val > 0 && size_val != 0xFFFF) populated++;
        }
        ptr += length;
        while (ptr + 1 < end && !(ptr[0] == 0 && ptr[1] == 0)) ptr++;
        ptr += 2;
    }
    return populated;
}

uint32_t CountPopulatedDimmSlots() {
    constexpr DWORD kRsmbSignature = 0x52534D42;
    const DWORD smbios_size = GetSystemFirmwareTable(kRsmbSignature, 0, nullptr, 0);
    if (smbios_size <= 8) return 0;
    std::vector<BYTE> buf(smbios_size);
    if (GetSystemFirmwareTable(kRsmbSignature, 0, buf.data(), smbios_size) != smbios_size) return 0;
    return CountDimmsInSmbiosTable(buf.data() + 8, smbios_size - 8);
}

bool IsWindowHandleValid(HWND hwnd) {
    return IsWindow(hwnd) != FALSE;
}

uint32_t WindowThreadId(HWND hwnd) {
    return static_cast<uint32_t>(GetWindowThreadProcessId(hwnd, nullptr));
}

uint64_t ReadL3CacheBytes() {
    uint64_t total_l3_bytes = 0;
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &len);
    if (len > 0) {
        std::vector<BYTE> buffer(len);
        if (GetLogicalProcessorInformationEx(RelationCache,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()),
                &len)) {
            const BYTE* ptr = buffer.data();
            const BYTE* end = ptr + len;
            while (ptr < end) {
                const auto info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
                if (info->Relationship == RelationCache && info->Cache.Level == 3) {
                    total_l3_bytes += info->Cache.CacheSize;
                }
                ptr += info->Size;
            }
        }
    }

    if (total_l3_bytes == 0) {
        int ext_info[4] = {0};
        __cpuid(ext_info, 0x80000006);
        total_l3_bytes = static_cast<uint64_t>(static_cast<unsigned int>(ext_info[3]) >> 18) * 512ULL * 1024ULL;
    }
    return total_l3_bytes;
}

} // namespace vegaflow
