#pragma once

#include <chrono>
#include <set>
#include <string>
#include <vector>

#include "ports/change_journal.hpp"
#include "ports/clock.hpp"

namespace vftest {

class FakeWallClock final : public vegaflow::Clock {
public:
    std::chrono::steady_clock::time_point now_steady{};
    std::chrono::system_clock::time_point now_wall =
        std::chrono::system_clock::from_time_t(1762905600); // 2025-11-12 00:00:00 UTC

    std::chrono::steady_clock::time_point Now() const override { return now_steady; }
    std::chrono::system_clock::time_point WallNow() const override { return now_wall; }
};

class FakeSystemUndo final : public vegaflow::SystemUndoPort {
public:
    std::set<std::string> fail_targets;
    std::vector<uint64_t> reverted_ids;
    std::vector<std::string> reverted_targets;
    bool restore_point_succeeds = true;
    int restore_point_calls = 0;

    bool RevertEntry(const vegaflow::JournalEntry& entry, std::string* out_error = nullptr) override {
        if (fail_targets.count(entry.target)) {
            if (out_error) *out_error = "Permiso denegado simulado";
            return false;
        }
        reverted_ids.push_back(entry.id);
        reverted_targets.push_back(entry.target);
        return true;
    }

    bool CreateRestorePoint(const std::string& /*description*/, std::string* out_message = nullptr) override {
        restore_point_calls++;
        if (out_message) {
            *out_message = restore_point_succeeds ? "Punto simulado creado." : "Proteccion desactivada.";
        }
        return restore_point_succeeds;
    }
};

} // namespace vftest
