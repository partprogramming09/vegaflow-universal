#pragma once
// Port: persistent change journal written atomically before and after every system/game change,
// plus system undo and restore-point capabilities. Only standard library types here.

#include <cstdint>
#include <string>
#include <vector>

namespace vegaflow {

enum class JournalTargetKind {
    RegistryValue = 0,
    PowerPlan = 1,
    MemoryCompression = 2,
    ProcessState = 3,
    DxgiGamePreference = 4,
    GameConfigFile = 5,
    SystemRestorePoint = 6,
    AuditEvent = 7,
};

enum class JournalEntryStatus {
    Pending = 0,   // Written to disk BEFORE applying the change
    Committed = 1, // Marked AFTER the change succeeded
    Reverted = 2,  // Cleanly undone
    Failed = 3,    // Apply or revert failed
};

struct JournalEntry {
    uint64_t id = 0;
    std::string timestamp;
    JournalTargetKind kind = JournalTargetKind::AuditEvent;
    JournalEntryStatus status = JournalEntryStatus::Pending;
    std::string target;
    std::string reason;
    std::string before_value;
    std::string after_value;
    std::string manual_fix_hint;

    // RegistryValue payload:
    int hive = 0; // 0 = HKLM, 1 = HKCU
    std::wstring subkey;
    std::wstring value_name;
    bool orig_present = false;
    uint32_t orig_type = 0;
    std::string orig_data_hex;

    // PowerPlan payload:
    std::string scheme_guid;
    std::vector<uint32_t> ac_values;
    std::vector<uint32_t> dc_values;

    // MemoryCompression payload:
    bool orig_mem_compression_enabled = true;

    // ProcessState payload:
    uint32_t pid = 0;
    uint64_t process_creation_time = 0;
    std::string process_role; // "game" or "bg"
    uint32_t orig_priority_class = 0;

    // DxgiGamePreference payload:
    std::wstring exe_path;
    bool ugp_orig_present = false;
    std::wstring ugp_orig_value;
    bool layers_orig_present = false;
    std::wstring layers_orig_value;

    // GameConfigFile payload:
    std::wstring file_path;
    std::wstring backup_path;
};

struct JournalHeader {
    uint32_t version = 1;
    bool active_session = false;
    uint32_t consecutive_unclean_exits = 0;
    bool restore_point_prompted = false;
    bool corrupt = false;
    std::string corrupt_reason;
};

struct StartupJournalEvaluation {
    bool corrupt = false;
    std::string corrupt_reason;
    bool previous_unclean_exit = false;
    uint32_t effective_unclean_streak = 0;
    bool safe_mode = false;
    size_t unclosed_or_unreverted_count = 0;
    std::vector<JournalEntry> pending_recovery_entries;
};

struct RestoreItemFailure {
    uint64_t entry_id = 0;
    std::string target;
    std::string reason;
    std::string manual_fix_hint;
};

struct RestoreReport {
    size_t total_count = 0;
    size_t reverted_count = 0;
    size_t failed_count = 0;
    bool all_succeeded = true;
    std::string summary_message;
    std::vector<RestoreItemFailure> failures;
};

class ChangeJournal {
public:
    virtual ~ChangeJournal() = default;

    // Loads header and entries from backing store. Never blocks startup; sets Header().corrupt on invalid data.
    virtual bool Load() = 0;

    // Marks a session started on disk (persists incremented unclean streak if previous exit was unclean).
    virtual bool BeginSession() = 0;

    // Marks the active session cleanly closed and resets consecutive_unclean_exits = 0.
    virtual bool EndSessionClean() = 0;

    // Clears active_session and consecutive_unclean_exits after recovery or explicit reset.
    virtual bool ResetRecoveryState() = 0;

    // Records whether the user was already prompted to create a Windows Restore Point.
    virtual bool SetRestorePointPrompted(bool prompted) = 0;

    // Writes a Pending entry atomically to disk BEFORE applying a change. Returns assigned id (0 on write failure).
    virtual uint64_t RecordPending(JournalEntry entry) = 0;

    // Updates an entry's status atomically on disk AFTER the operation completes.
    virtual bool MarkCommitted(uint64_t id) = 0;
    virtual bool MarkReverted(uint64_t id) = 0;
    virtual bool MarkFailed(uint64_t id, const std::string& failure_note = "") = 0;

    // Marks any active Pending/Committed entries of `kind` and `target` as Reverted.
    virtual bool MarkMatchingReverted(JournalTargetKind kind, const std::string& target) = 0;

    // Appends an informational audit event (status Committed, non-revertible).
    virtual uint64_t RecordAuditEvent(const std::string& target, const std::string& reason, const std::string& details) = 0;

    virtual const JournalHeader& Header() const = 0;
    virtual bool DetectedPreviousUncleanExit() const = 0;
    virtual uint32_t EffectiveUncleanStreak() const = 0;
    virtual bool IsSafeModeTriggered() const = 0;
    virtual const std::vector<JournalEntry>& Entries() const = 0;
    // Moves on every change to the entries or header; readers compare it to skip rebuilding a view of Entries().
    virtual uint64_t Revision() const = 0;
};

class SystemUndoPort {
public:
    virtual ~SystemUndoPort() = default;

    virtual bool RevertEntry(const JournalEntry& entry, std::string* out_error = nullptr) = 0;
    virtual bool CreateRestorePoint(const std::string& description, std::string* out_message = nullptr) = 0;
};

} // namespace vegaflow
