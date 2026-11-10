#pragma once
// A FileSystem that only records what it was asked to do. Nothing reaches the disk.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ports/file_system.hpp"

namespace vftest {

class FakeFileSystem final : public vegaflow::FileSystem {
public:
    bool allow_game_dir = true;            // answer of CanModifyGameDir
    bool fail_replace = false;             // Replace fails and changes nothing
    std::map<std::wstring, std::string> files; // what Read returns and Write stores
    std::set<std::wstring> unreadable;         // exist, but Read fails
    std::vector<std::wstring> writes, copies, removes, replaces;

    bool Read(const std::wstring& path, std::string& out_content) override {
        if (unreadable.count(path)) return false;
        auto it = files.find(path);
        if (it == files.end()) return false;
        out_content = it->second;
        return true;
    }
    bool Write(const std::wstring& path, const std::string& content, bool anticheat_protected = false) override {
        if (anticheat_protected) return false;
        writes.push_back(path);
        files[path] = content;
        return true;
    }
    bool Copy(const std::wstring& src, const std::wstring& dst, bool, bool anticheat_protected = false) override {
        if (anticheat_protected) return false;
        copies.push_back(src + L" -> " + dst);
        return true;
    }
    bool Remove(const std::wstring& path, bool anticheat_protected = false) override {
        if (anticheat_protected) return false;
        removes.push_back(path);
        files.erase(path);
        return true;
    }
    bool Replace(const std::wstring& path, const std::string& content) override {
        if (fail_replace) return false;
        replaces.push_back(path);
        files[path] = content;
        return true;
    }
    bool Exists(const std::wstring& path) override { return files.count(path) || unreadable.count(path); }
    bool CanModifyGameDir(const std::filesystem::path&) override { return allow_game_dir; }
    bool HasAntiCheatSignature(const std::wstring&) override { return false; }
    bool CanWrite(const std::wstring&) override { return true; }
    void AllowReading(const std::wstring& dir) override { readable_dirs.push_back(dir); }
    std::vector<std::wstring> readable_dirs;

    size_t Changes() const { return writes.size() + copies.size() + removes.size() + replaces.size(); }
};

} // namespace vftest
