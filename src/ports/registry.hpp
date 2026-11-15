#pragma once
// Pure C++ registry & shader-cache port for graphics/system tweaks (no <windows.h>).

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

enum class RegHive {
    LocalMachine = 0,
    CurrentUser = 1
};

enum class RegValueType {
    None = 0,
    Dword = 1,
    Qword = 2,
    String = 3,
    Binary = 4
};

struct RegValue {
    RegValueType type = RegValueType::None;
    uint32_t dword = 0;
    uint64_t qword = 0;
    std::wstring str;
    std::vector<uint8_t> binary;

    static RegValue MakeDword(uint32_t v) {
        RegValue r;
        r.type = RegValueType::Dword;
        r.dword = v;
        return r;
    }

    static RegValue MakeQword(uint64_t v) {
        RegValue r;
        r.type = RegValueType::Qword;
        r.qword = v;
        return r;
    }

    static RegValue MakeString(std::wstring s) {
        RegValue r;
        r.type = RegValueType::String;
        r.str = std::move(s);
        return r;
    }

    static RegValue MakeBinary(std::vector<uint8_t> b) {
        RegValue r;
        r.type = RegValueType::Binary;
        r.binary = std::move(b);
        return r;
    }
};

struct ShaderCacheReport {
    std::wstring path;
    uint64_t total_size_bytes = 0;
    uint32_t file_count = 0;
    bool exists = false;
};

class RegistryPort {
public:
    virtual ~RegistryPort() = default;

    virtual bool ReadValue(RegHive hive, const std::wstring& subkey, const std::wstring& name, RegValue& out) = 0;

    // Writes a registry value. When `backup_and_journal` is true, records the first-time backup in
    // SOFTWARE\VegaFlow\RegBackup and records/commits a ChangeJournal entry before/after writing.
    virtual bool WriteValue(
        RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        const RegValue& val,
        bool backup_and_journal = true,
        const std::string& reason = {},
        const std::string& after_preview = {}) = 0;

    virtual bool DeleteValue(
        RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        bool mark_journal_reverted = true) = 0;

    // Restores one specific value from its backup in SOFTWARE\VegaFlow\RegBackup and deletes that backup record.
    // Returns false if no backup existed or restoration failed.
    virtual bool RestoreBackedUpValue(RegHive hive, const std::wstring& subkey, const std::wstring& name) = 0;

    // Restores all pending backups under HKLM\SOFTWARE\VegaFlow\RegBackup and HKCU\SOFTWARE\VegaFlow\RegBackup.
    virtual bool RestoreAllBackups(size_t* out_reverted = nullptr, size_t* out_total = nullptr) = 0;

    virtual std::vector<std::wstring> EnumSubkeys(RegHive hive, const std::wstring& subkey) = 0;
    virtual std::vector<std::pair<std::wstring, std::wstring>> EnumStringValues(RegHive hive, const std::wstring& subkey) = 0;
    virtual bool KeyExists(RegHive hive, const std::wstring& subkey) = 0;

    virtual bool IsMemoryCompressionEnabled() = 0;
    virtual bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false) = 0;
    virtual std::vector<ShaderCacheReport> InspectShaderCaches() = 0;

    // Optional ChangeJournal hooks for per-exe DXGI UserGpuPreferences + AppCompatFlags\Layers changes.
    virtual uint64_t BeginDxgiPreferenceChange(
        const std::wstring& exe_path,
        const std::wstring& after_pref,
        const std::string& reason) {
        (void)exe_path;
        (void)after_pref;
        (void)reason;
        return 0;
    }

    virtual void EndDxgiPreferenceChange(uint64_t entry_id, bool succeeded) {
        (void)entry_id;
        (void)succeeded;
    }

    virtual void MarkDxgiPreferenceReverted(const std::wstring& exe_path) {
        (void)exe_path;
    }
};

} // namespace vegaflow
