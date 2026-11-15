#pragma once
// Windows Registry adapter implementing RegistryPort (including SOFTWARE\VegaFlow\RegBackup,
// ChangeJournal integration, MemCompression, and shader-cache directory inspection).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ports/registry.hpp"

#include <string>
#include <utility>
#include <vector>

namespace vegaflow {

// A REG_SZ or REG_EXPAND_SZ value (not expanded), up to 1023 characters. False if the key or value is missing.
bool QueryRegString(HKEY root, const wchar_t* sub_key, const wchar_t* value_name, std::wstring& out);

class WindowsRegistryPort final : public RegistryPort {
public:
    bool ReadValue(RegHive hive, const std::wstring& subkey, const std::wstring& name, RegValue& out) override;

    bool WriteValue(
        RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        const RegValue& val,
        bool backup_and_journal = true,
        const std::string& reason = {},
        const std::string& after_preview = {}) override;

    bool DeleteValue(
        RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        bool mark_journal_reverted = true) override;

    bool RestoreBackedUpValue(RegHive hive, const std::wstring& subkey, const std::wstring& name) override;
    bool RestoreAllBackups(size_t* out_reverted = nullptr, size_t* out_total = nullptr) override;

    std::vector<std::wstring> EnumSubkeys(RegHive hive, const std::wstring& subkey) override;
    std::vector<std::pair<std::wstring, std::wstring>> EnumStringValues(RegHive hive, const std::wstring& subkey) override;
    bool KeyExists(RegHive hive, const std::wstring& subkey) override;

    bool IsMemoryCompressionEnabled() override;
    bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false) override;
    std::vector<ShaderCacheReport> InspectShaderCaches() override;

    uint64_t BeginDxgiPreferenceChange(
        const std::wstring& exe_path,
        const std::wstring& after_pref,
        const std::string& reason) override;
    void EndDxgiPreferenceChange(uint64_t entry_id, bool succeeded) override;
    void MarkDxgiPreferenceReverted(const std::wstring& exe_path) override;
};

} // namespace vegaflow
