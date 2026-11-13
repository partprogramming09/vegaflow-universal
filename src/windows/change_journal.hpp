#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ports/change_journal.hpp"
#include "ports/clock.hpp"
#include "ports/file_system.hpp"

namespace vegaflow {

// Returns the journal path next to the executable: <exe_dir>\config\change_journal.vfj.
std::wstring JournalFilePath();

// Process-wide active journal hook (used by GraphicsOptimizer / CpuOrchestrator / SteamEngineOptimizer
// when a daemon, CLI, or GUI session attaches one; nullptr during standalone unit tests).
void SetActiveChangeJournal(ChangeJournal* journal);
ChangeJournal* GetActiveChangeJournal();

// File-backed ChangeJournal storing atomic snapshots via FileSystem::Replace.
class FileChangeJournal final : public ChangeJournal {
public:
    FileChangeJournal(FileSystem& fs, Clock& clock, std::wstring path);

    bool Load() override;
    bool BeginSession() override;
    bool EndSessionClean() override;
    bool ResetRecoveryState() override;
    bool SetRestorePointPrompted(bool prompted) override;

    uint64_t RecordPending(JournalEntry entry) override;
    bool MarkCommitted(uint64_t id) override;
    bool MarkReverted(uint64_t id) override;
    bool MarkFailed(uint64_t id, const std::string& failure_note = "") override;
    bool MarkMatchingReverted(JournalTargetKind kind, const std::string& target) override;
    uint64_t RecordAuditEvent(const std::string& target, const std::string& reason, const std::string& details) override;

    const JournalHeader& Header() const override;
    bool DetectedPreviousUncleanExit() const override;
    uint32_t EffectiveUncleanStreak() const override;
    bool IsSafeModeTriggered() const override;
    const std::vector<JournalEntry>& Entries() const override;

    // Thread-safe copy of entries for UI polling.
    std::vector<JournalEntry> SnapshotEntries() const;

private:
    bool PersistLocked();
    std::string FormatTimestampLocked() const;
    void PruneHistoryLocked();

    FileSystem& fs_;
    Clock& clock_;
    std::wstring path_;
    mutable std::mutex mutex_;
    JournalHeader header_{};
    bool previous_unclean_exit_ = false;
    uint32_t effective_unclean_streak_ = 0;
    bool safe_mode_ = false;
    uint64_t next_id_ = 1;
    std::vector<JournalEntry> entries_;
};

// Real Windows undo and System Restore Point adapter.
class WindowsSystemUndo final : public SystemUndoPort {
public:
    explicit WindowsSystemUndo(FileSystem& fs) : fs_(fs) {}

    bool RevertEntry(const JournalEntry& entry, std::string* out_error = nullptr) override;
    bool CreateRestorePoint(const std::string& description, std::string* out_message = nullptr) override;

private:
    FileSystem& fs_;
};

// Helper to query current process creation time in 100ns units (returns 0 if inaccessible).
uint64_t QueryProcessCreationTime(uint32_t pid);

} // namespace vegaflow
