#pragma once
// In-memory fake implementations of RegistryPort and PowerPort for unit tests.

#include "ports/power.hpp"
#include "ports/registry.hpp"

#include <algorithm>
#include <cwctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace vftest {

struct FakeRegOp {
    vegaflow::RegHive hive;
    std::wstring subkey;
    std::wstring name;
};

class FakeRegistryPort final : public vegaflow::RegistryPort {
public:
    static std::wstring Lower(std::wstring s) {
        for (wchar_t& ch : s) {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }
        return s;
    }

    using Key = std::tuple<int, std::wstring, std::wstring>;

    static Key MakeKey(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name) {
        return { static_cast<int>(hive), Lower(subkey), Lower(name) };
    }

    std::map<Key, vegaflow::RegValue> values;
    std::map<Key, std::optional<vegaflow::RegValue>> backups;
    std::set<std::pair<int, std::wstring>> extra_keys;
    std::vector<FakeRegOp> reads;
    std::vector<FakeRegOp> writes;
    std::vector<FakeRegOp> deletes;
    bool memory_compression_enabled = true;
    std::vector<vegaflow::ShaderCacheReport> shader_caches;

    void SeedValue(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name, const vegaflow::RegValue& val) {
        values[MakeKey(hive, subkey, name)] = val;
        extra_keys.insert({ static_cast<int>(hive), Lower(subkey) });
    }

    void SeedLegacyBackup(
        vegaflow::RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        std::optional<vegaflow::RegValue> original_value) {
        backups[MakeKey(hive, subkey, name)] = std::move(original_value);
    }

    bool HasValue(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name) const {
        return values.find(MakeKey(hive, subkey, name)) != values.end();
    }

    bool HasBackup(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name) const {
        return backups.find(MakeKey(hive, subkey, name)) != backups.end();
    }

    bool TouchedName(const std::wstring& needle_lower) const {
        auto check = [&](const std::vector<FakeRegOp>& ops) {
            for (const auto& op : ops) {
                if (Lower(op.name) == needle_lower) return true;
            }
            return false;
        };
        return check(reads) || check(writes) || check(deletes);
    }

    bool TouchedSubkeyContaining(const std::wstring& substr_lower) const {
        auto check = [&](const std::vector<FakeRegOp>& ops) {
            for (const auto& op : ops) {
                if (Lower(op.subkey).find(substr_lower) != std::wstring::npos) return true;
            }
            return false;
        };
        return check(reads) || check(writes) || check(deletes);
    }

    bool ReadValue(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name, vegaflow::RegValue& out) override {
        reads.push_back({ hive, subkey, name });
        auto it = values.find(MakeKey(hive, subkey, name));
        if (it == values.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    bool WriteValue(
        vegaflow::RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        const vegaflow::RegValue& val,
        bool backup_and_journal = true,
        const std::string& = {},
        const std::string& = {}) override {
        writes.push_back({ hive, subkey, name });
        Key k = MakeKey(hive, subkey, name);
        if (backup_and_journal && backups.find(k) == backups.end()) {
            auto cur = values.find(k);
            backups[k] = (cur != values.end()) ? std::optional<vegaflow::RegValue>(cur->second) : std::nullopt;
        }
        values[k] = val;
        extra_keys.insert({ static_cast<int>(hive), Lower(subkey) });
        return true;
    }

    bool DeleteValue(
        vegaflow::RegHive hive,
        const std::wstring& subkey,
        const std::wstring& name,
        bool = true) override {
        deletes.push_back({ hive, subkey, name });
        return values.erase(MakeKey(hive, subkey, name)) > 0;
    }

    bool RestoreBackedUpValue(vegaflow::RegHive hive, const std::wstring& subkey, const std::wstring& name) override {
        Key k = MakeKey(hive, subkey, name);
        auto it = backups.find(k);
        if (it == backups.end()) {
            return false;
        }
        if (it->second.has_value()) {
            values[k] = *it->second;
        } else {
            values.erase(k);
        }
        backups.erase(it);
        return true;
    }

    bool RestoreAllBackups(size_t* out_reverted = nullptr, size_t* out_total = nullptr) override {
        const size_t total = backups.size();
        for (const auto& [k, orig] : backups) {
            if (orig.has_value()) {
                values[k] = *orig;
            } else {
                values.erase(k);
            }
        }
        backups.clear();
        if (out_reverted) *out_reverted = total;
        if (out_total) *out_total = total;
        return true;
    }

    std::vector<std::wstring> EnumSubkeys(vegaflow::RegHive hive, const std::wstring& subkey) override {
        const std::wstring prefix = Lower(subkey) + L"\\";
        std::set<std::wstring> children;
        for (const auto& [h, sk] : extra_keys) {
            if (h == static_cast<int>(hive) && sk.rfind(prefix, 0) == 0) {
                std::wstring rest = sk.substr(prefix.size());
                size_t slash = rest.find(L'\\');
                children.insert(slash == std::wstring::npos ? rest : rest.substr(0, slash));
            }
        }
        return { children.begin(), children.end() };
    }

    std::vector<std::pair<std::wstring, std::wstring>> EnumStringValues(vegaflow::RegHive hive, const std::wstring& subkey) override {
        const std::wstring target = Lower(subkey);
        std::vector<std::pair<std::wstring, std::wstring>> out;
        for (const auto& [k, v] : values) {
            if (std::get<0>(k) == static_cast<int>(hive) &&
                std::get<1>(k) == target &&
                v.type == vegaflow::RegValueType::String) {
                out.emplace_back(std::get<2>(k), v.str);
            }
        }
        return out;
    }

    bool KeyExists(vegaflow::RegHive hive, const std::wstring& subkey) override {
        const std::wstring target = Lower(subkey);
        if (extra_keys.count({ static_cast<int>(hive), target }) > 0) return true;
        for (const auto& [k, _] : values) {
            if (std::get<0>(k) == static_cast<int>(hive) && std::get<1>(k) == target) {
                return true;
            }
        }
        return false;
    }

    bool IsMemoryCompressionEnabled() override { return memory_compression_enabled; }

    bool SetMemoryCompressionEnabled(bool enable, bool dry_run = false) override {
        if (!dry_run) memory_compression_enabled = enable;
        return true;
    }

    std::vector<vegaflow::ShaderCacheReport> InspectShaderCaches() override { return shader_caches; }
};

class FakePowerPort final : public vegaflow::PowerPort {
public:
    std::string scheme_guid = "381b4222-f694-41f0-9685-ff5bb260df2e";
    vegaflow::ApuPowerPlanValues current{ { 1, 2, 50, 0, 0, 100, 50 }, { 1, 2, 50, 0, 0, 100, 50 } };
    int read_count = 0;
    int apply_count = 0;
    int query_count = 0;

    bool ReadActiveApuPlan(std::string& out_scheme_guid, vegaflow::ApuPowerPlanValues& out_values) override {
        ++read_count;
        out_scheme_guid = scheme_guid;
        out_values = current;
        return true;
    }

    bool ApplyActiveApuPlan(const vegaflow::ApuPowerPlanValues& values) override {
        ++apply_count;
        current = values;
        return true;
    }

    bool QueryApuPowerOptimized() override {
        ++query_count;
        return current.ac.size() >= 7 && current.ac[0] == 0;
    }
};

} // namespace vftest
