#include "optimize/cpu_orchestrator.hpp"

#include "bridge/rust_bridge.h"
#include "windows/change_journal.hpp"
#include "windows/process_control.hpp"

#include <algorithm>
#include <string>

namespace vegaflow {

namespace {

constexpr uint64_t kMinSampleMs = 1000;

std::vector<uint32_t> UlongToU32(const std::vector<ULONG>& v) {
    std::vector<uint32_t> out;
    out.reserve(v.size());
    for (ULONG x : v) out.push_back(static_cast<uint32_t>(x));
    return out;
}

} // namespace

CpuOrchestrator::CpuOrchestrator()
    : owned_pc_(std::make_unique<WindowsProcessControl>()), pc_(owned_pc_.get()) {}

CpuOrchestrator::CpuOrchestrator(ProcessControl& process_control)
    : owned_pc_(nullptr), pc_(&process_control) {}

CpuOrchestrator::~CpuOrchestrator() {
    RestoreAll(false);
}

void CpuOrchestrator::RestoreTrackedGame() {
    if (last_optimized_game_pid_ == 0 || !pc_) {
        return;
    }
    // A process that exited has nothing left to give back; a live one that refuses keeps its journal entry pending.
    const bool alive = last_optimized_game_creation_time_ == 0 ||
        pc_->VerifyProcessIdentity(last_optimized_game_pid_, last_optimized_game_creation_time_);
    const bool given_back = !alive || pc_->RestoreGameProcess(
        last_optimized_game_pid_,
        last_optimized_game_creation_time_,
        last_game_orig_priority_class_);

    if (ChangeJournal* journal = given_back ? GetActiveChangeJournal() : nullptr) {
        journal->MarkMatchingReverted(
            JournalTargetKind::ProcessState,
            "PID:" + std::to_string(last_optimized_game_pid_));
    }

    last_optimized_game_pid_ = 0;
    last_optimized_game_creation_time_ = 0;
    last_game_orig_priority_class_ = NORMAL_PRIORITY_CLASS;
    last_presenter_tid_ = 0;
    last_game_had_full_access_ = false;
    last_power_throttling_disabled_ = false;
    last_io_priority_elevated_ = false;
    last_memory_priority_elevated_ = false;
    last_game_cpu_sets_applied_ = false;
    last_presenter_boosted_ = false;
}

bool CpuOrchestrator::UpdateBackgroundLoad(const std::vector<BackgroundProcessTarget>& targets, uint32_t game_pid) {
    if (!pc_) return false;
    const uint64_t now_ms = pc_->TickCountMs();
    const uint64_t elapsed_ms = now_ms - bg_prev_tick_ms_;
    if (bg_prev_tick_ms_ != 0 && elapsed_ms < kMinSampleMs) {
        return false;
    }

    std::unordered_map<uint32_t, std::pair<uint64_t, uint64_t>> current;
    uint64_t delta_100ns = 0;
    for (const auto& t : targets) {
        if (t.pid == 0 || t.pid == game_pid || t.action != ProcessAction::Throttle) continue;
        uint64_t total = 0;
        if (pc_->QueryCpuTime100ns(t.pid, t.creation_time, total)) {
            current[t.pid] = {t.creation_time, total};
            auto it = bg_cpu_prev_.find(t.pid);
            if (it != bg_cpu_prev_.end() &&
                (t.creation_time == 0 || it->second.first == 0 || it->second.first == t.creation_time) &&
                total >= it->second.second) {
                delta_100ns += total - it->second.second;
            }
        }
    }

    const bool had_baseline = bg_prev_tick_ms_ != 0;
    bg_cpu_prev_ = std::move(current);
    bg_prev_tick_ms_ = now_ms;
    if (!had_baseline || elapsed_ms == 0) {
        return false;
    }

    const double logical_cpus = static_cast<double>(delta_100ns) / (static_cast<double>(elapsed_ms) * 10000.0);
    const bool before = bg_confine_active_;
    bg_confine_active_ = vf_rust_eval_bg_confinement(logical_cpus, bg_confine_active_);
    return before != bg_confine_active_;
}

void CpuOrchestrator::SetModifiedBackgroundCpuSets(const std::vector<uint32_t>& sets) {
    if (!pc_) return;
    for (const auto& entry : modified_background_) {
        if (entry.action != ProcessAction::Throttle) continue;
        pc_->UpdateBackgroundCpuSets(entry.pid, entry.creation_time, sets);
    }
}

CpuOptimizationResult CpuOrchestrator::ApplyOptimizations(
    uint32_t game_pid,
    uint32_t presenter_thread_id,
    bool is_dx12_or_vulkan,
    const std::vector<ULONG>& game_cpu_sets,
    const std::vector<ULONG>& background_cpu_sets,
    const std::vector<uint32_t>& background_pids,
    bool dry_run) {

    std::vector<BackgroundProcessTarget> targets;
    targets.reserve(background_pids.size());
    for (uint32_t pid : background_pids) {
        BackgroundProcessTarget t{};
        t.pid = pid;
        t.creation_time = pc_ ? pc_->QueryCreationTime(pid) : 0;
        t.action = ProcessAction::Throttle;
        targets.push_back(std::move(t));
    }
    const uint64_t game_ct = (game_pid != 0 && pc_) ? pc_->QueryCreationTime(game_pid) : 0;
    return ApplyOptimizations(
        game_pid,
        game_ct,
        presenter_thread_id,
        is_dx12_or_vulkan,
        game_cpu_sets,
        background_cpu_sets,
        targets,
        dry_run);
}

CpuOptimizationResult CpuOrchestrator::ApplyOptimizations(
    uint32_t game_pid,
    uint64_t game_creation_time,
    uint32_t presenter_thread_id,
    bool is_dx12_or_vulkan,
    const std::vector<ULONG>& game_cpu_sets,
    const std::vector<ULONG>& background_cpu_sets,
    const std::vector<BackgroundProcessTarget>& background_targets,
    bool dry_run) {

    CpuOptimizationResult result{};
    if (!pc_) return result;

    const std::vector<uint32_t> game_sets_u32 = UlongToU32(game_cpu_sets);
    const std::vector<uint32_t> bg_sets_u32 = UlongToU32(background_cpu_sets);

    // If switching games (or same PID reused with a different creation timestamp), restore the old game first.
    if (!dry_run && last_optimized_game_pid_ != 0) {
        const bool pid_changed = (game_pid == 0 || game_pid != last_optimized_game_pid_);
        const bool ct_changed = (game_creation_time != 0 && last_optimized_game_creation_time_ != 0 &&
                                 game_creation_time != last_optimized_game_creation_time_);
        if (pid_changed || ct_changed) {
            RestoreTrackedGame();
        } else if (last_optimized_game_creation_time_ != 0 &&
                   !pc_->VerifyProcessIdentity(last_optimized_game_pid_, last_optimized_game_creation_time_)) {
            // Original game exited or PID recycled; drop tracking without touching the new owner.
            last_optimized_game_pid_ = 0;
            last_optimized_game_creation_time_ = 0;
            last_presenter_tid_ = 0;
        }
    }

    if (game_pid != 0) {
        const uint64_t eff_game_ct = (game_creation_time != 0) ? game_creation_time : pc_->QueryCreationTime(game_pid);
        if (!dry_run && game_pid == last_optimized_game_pid_ &&
            (eff_game_ct == 0 || last_optimized_game_creation_time_ == 0 || eff_game_ct == last_optimized_game_creation_time_)) {
            result.game_handle_full_access = last_game_had_full_access_;
            result.power_throttling_disabled = last_power_throttling_disabled_;
            result.io_priority_elevated = last_io_priority_elevated_;
            result.memory_priority_elevated = last_memory_priority_elevated_;
            result.game_cpu_sets_applied = last_game_cpu_sets_applied_;
        } else {
            uint64_t j_game = 0;
            if (!dry_run) {
                if (ChangeJournal* journal = GetActiveChangeJournal()) {
                    JournalEntry je;
                    je.kind = JournalTargetKind::ProcessState;
                    je.target = "PID:" + std::to_string(game_pid);
                    je.reason = "Game process optimization";
                    je.before_value = "Normal priority / default CPU sets";
                    je.after_value = is_dx12_or_vulkan ? "ABOVE_NORMAL + CPU sets" : "HIGH + CPU sets";
                    je.manual_fix_hint = "Cierra o reinicia el juego si quedó con afinidad/prioridad fijada (PID " + std::to_string(game_pid) + ")";
                    je.pid = game_pid;
                    je.process_creation_time = eff_game_ct;
                    je.process_role = "game";
                    j_game = journal->RecordPending(std::move(je));
                }
            }

            GameBoostOutcome boost{};
            uint32_t orig_prio = NORMAL_PRIORITY_CLASS;
            pc_->BoostGameProcess(game_pid, eff_game_ct, is_dx12_or_vulkan, game_sets_u32, boost, orig_prio, dry_run);

            result.game_handle_full_access = boost.full_access;
            result.power_throttling_disabled = boost.power_throttling_disabled;
            result.io_priority_elevated = boost.io_priority_elevated;
            result.memory_priority_elevated = boost.memory_priority_elevated;
            result.game_cpu_sets_applied = boost.cpu_sets_applied;

            if (!dry_run) {
                if (j_game != 0) {
                    if (ChangeJournal* journal = GetActiveChangeJournal()) {
                        if (boost.full_access) journal->MarkCommitted(j_game);
                        else journal->MarkReverted(j_game);
                    }
                }
                if (boost.full_access) {
                    last_optimized_game_pid_ = game_pid;
                    last_optimized_game_creation_time_ = eff_game_ct;
                    last_game_orig_priority_class_ = orig_prio;
                    last_game_had_full_access_ = result.game_handle_full_access;
                    last_power_throttling_disabled_ = result.power_throttling_disabled;
                    last_io_priority_elevated_ = result.io_priority_elevated;
                    last_memory_priority_elevated_ = result.memory_priority_elevated;
                    last_game_cpu_sets_applied_ = result.game_cpu_sets_applied;
                }
            }
        }
    }

    // 2. Presenter thread boost for low-level APIs
    if (is_dx12_or_vulkan && presenter_thread_id != 0) {
        if (dry_run) {
            result.presenter_thread_boosted = pc_->BoostPresenterThread(presenter_thread_id, true);
        } else if (presenter_thread_id == last_presenter_tid_) {
            result.presenter_thread_boosted = last_presenter_boosted_;
        } else {
            result.presenter_thread_boosted = pc_->BoostPresenterThread(presenter_thread_id, false);
            last_presenter_tid_ = presenter_thread_id;
            last_presenter_boosted_ = result.presenter_thread_boosted;
        }
    }

    // Prune dead or PID-recycled background entries before evaluating current targets.
    if (!dry_run && !modified_background_.empty()) {
        modified_background_.erase(
            std::remove_if(modified_background_.begin(), modified_background_.end(),
                [&](const TrackedBackgroundProcess& entry) {
                    if (!pc_->VerifyProcessIdentity(entry.pid, entry.creation_time)) {
                        if (ChangeJournal* journal = GetActiveChangeJournal()) {
                            journal->MarkMatchingReverted(JournalTargetKind::ProcessState, "PID:" + std::to_string(entry.pid));
                        }
                        return true;
                    }
                    // Also drop if this PID became the active game.
                    if (game_pid != 0 && entry.pid == game_pid) {
                        const bool given_back = pc_->RestoreBackgroundProcess(entry.pid, entry.creation_time, entry.action);
                        if (ChangeJournal* journal = given_back ? GetActiveChangeJournal() : nullptr) {
                            journal->MarkMatchingReverted(JournalTargetKind::ProcessState, "PID:" + std::to_string(entry.pid));
                        }
                        return true;
                    }
                    return false;
                }),
            modified_background_.end());
    }

    // Forget refusals of processes that exited or whose pid was recycled.
    refused_background_.erase(
        std::remove_if(refused_background_.begin(), refused_background_.end(),
            [&](const TrackedBackgroundProcess& e) { return !pc_->VerifyProcessIdentity(e.pid, e.creation_time); }),
        refused_background_.end());

    // 3. Background classification enforcement (DoNotTouch is skipped, EcoOnly gets EcoQoS, Throttle gets full confinement)
    if (!background_targets.empty()) {
        static const std::vector<uint32_t> no_sets;
        if (!dry_run && !bg_sets_u32.empty() && UpdateBackgroundLoad(background_targets, game_pid)) {
            SetModifiedBackgroundCpuSets(bg_confine_active_ ? bg_sets_u32 : no_sets);
        }
        const std::vector<uint32_t>& bg_sets = (dry_run || bg_confine_active_) ? bg_sets_u32 : no_sets;
        const uint32_t current_pid = pc_->CurrentPid();

        for (const auto& target : background_targets) {
            if (target.pid == 0 || target.pid == game_pid || target.pid == current_pid) {
                continue;
            }
            const uint64_t eff_ct = (target.creation_time != 0) ? target.creation_time : pc_->QueryCreationTime(target.pid);

            auto existing = std::find_if(modified_background_.begin(), modified_background_.end(),
                [&](const TrackedBackgroundProcess& e) {
                    return e.pid == target.pid &&
                           (eff_ct == 0 || e.creation_time == 0 || e.creation_time == eff_ct);
                });

            if (target.action == ProcessAction::DoNotTouch || target.action == ProcessAction::BoostAsGame) {
                if (!dry_run && existing != modified_background_.end()) {
                    const bool given_back = pc_->RestoreBackgroundProcess(existing->pid, existing->creation_time, existing->action);
                    if (ChangeJournal* journal = given_back ? GetActiveChangeJournal() : nullptr) {
                        journal->MarkMatchingReverted(JournalTargetKind::ProcessState, "PID:" + std::to_string(existing->pid));
                    }
                    modified_background_.erase(existing);
                }
                continue;
            }

            if (existing != modified_background_.end() && existing->action == target.action) {
                result.background_processes_throttled++;
                if (target.action == ProcessAction::Throttle) {
                    result.background_io_deprioritized = true;
                    result.background_memory_deprioritized = true;
                    if (!bg_sets.empty()) {
                        result.background_processes_isolated++;
                    }
                }
                continue;
            }

            if (!dry_run && existing != modified_background_.end() && existing->action != target.action) {
                // Policy for this exe changed (e.g. Throttle -> EcoOnly): restore previous state first cleanly.
                pc_->RestoreBackgroundProcess(existing->pid, existing->creation_time, existing->action);
                modified_background_.erase(existing);
            }

            const auto refused = std::find_if(refused_background_.begin(), refused_background_.end(),
                [&](const TrackedBackgroundProcess& e) {
                    return e.pid == target.pid && e.creation_time == eff_ct && e.action == target.action;
                });
            if (!dry_run && refused != refused_background_.end()) {
                continue;
            }

            uint64_t j_bg = 0;
            if (!dry_run) {
                if (ChangeJournal* journal = GetActiveChangeJournal()) {
                    JournalEntry je;
                    je.kind = JournalTargetKind::ProcessState;
                    je.target = "PID:" + std::to_string(target.pid);
                    je.reason = (target.action == ProcessAction::EcoOnly)
                        ? "Background EcoQoS only"
                        : "Background process confinement (EcoQoS/IO/Mem/CPUSet)";
                    je.before_value = "Normal priority / default CPU sets";
                    je.after_value = (target.action == ProcessAction::EcoOnly) ? "EcoQoS" : "EcoQoS + LowMem + VeryLowIO";
                    je.manual_fix_hint = "Reinicia el proceso PID " + std::to_string(target.pid) + " si quedó en EcoQoS";
                    je.pid = target.pid;
                    je.process_creation_time = eff_ct;
                    je.process_role = "bg";
                    j_bg = journal->RecordPending(std::move(je));
                }
            }

            const BackgroundApplyOutcome out = pc_->ApplyBackgroundPolicy(
                target.pid,
                eff_ct,
                target.action,
                bg_sets,
                dry_run);

            if (out.power_throttling_applied) result.background_processes_throttled++;
            if (out.memory_deprioritized) result.background_memory_deprioritized = true;
            if (out.io_deprioritized) result.background_io_deprioritized = true;
            if (out.cpu_sets_applied) result.background_processes_isolated++;

            if (!dry_run) {
                if (out.modified) {
                    modified_background_.push_back({target.pid, eff_ct, target.action});
                } else if (eff_ct != 0) {
                    refused_background_.push_back({target.pid, eff_ct, target.action});
                }
                if (j_bg != 0) {
                    if (ChangeJournal* journal = GetActiveChangeJournal()) {
                        if (out.modified) journal->MarkCommitted(j_bg);
                        else journal->MarkReverted(j_bg);
                    }
                }
            }
        }
    }

    return result;
}

void CpuOrchestrator::RestoreAll(bool dry_run) {
    if (dry_run || !pc_) {
        return;
    }

    RestoreTrackedGame();

    for (const auto& entry : modified_background_) {
        const bool alive = entry.creation_time == 0 || pc_->VerifyProcessIdentity(entry.pid, entry.creation_time);
        const bool given_back = !alive || pc_->RestoreBackgroundProcess(entry.pid, entry.creation_time, entry.action);
        if (ChangeJournal* journal = given_back ? GetActiveChangeJournal() : nullptr) {
            journal->MarkMatchingReverted(JournalTargetKind::ProcessState, "PID:" + std::to_string(entry.pid));
        }
    }
    modified_background_.clear();
    bg_cpu_prev_.clear();
    bg_prev_tick_ms_ = 0;
    bg_confine_active_ = false;
}

} // namespace vegaflow
