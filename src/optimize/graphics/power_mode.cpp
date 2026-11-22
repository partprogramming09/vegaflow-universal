#include "optimize/graphics/graphics_internal.hpp"
#include "windows/change_journal.hpp"

namespace vegaflow {

namespace {

constexpr size_t kApuPowerSettingCount = 7;

} // namespace

bool PowerModeTweaks::SetApuPowerMode(ApuPowerMode mode, const ApuOptimizationPolicy& policy, bool dry_run) {
    if (dry_run) {
        return true;
    }

    uint32_t boost_val = 2;
    uint32_t cpu_max_freq = 0;
    uint32_t aspm_val = 1;
    uint32_t core_parking_val = 50;
    uint32_t cpu_throttle_max = 100;
    uint32_t epp_val = 50;

    if (mode == ApuPowerMode::GpuPrioritySustained) {
        boost_val = 4;        // Efficient Aggressive
        cpu_max_freq = policy.cpu_stapm_cap_mhz;
        epp_val = 25;         // Efficient high performance SMU
        aspm_val = 0;         // PCIe ASPM disabled
        core_parking_val = 50;
        cpu_throttle_max = 100;
    } else {
        boost_val = 2;        // Standard aggressive Boost
        cpu_max_freq = 0;
        epp_val = 50;         // Windows balanced EPP
        aspm_val = 1;         // PCIe ASPM enabled
        core_parking_val = 50;
        cpu_throttle_max = 100;
    }

    ApuPowerPlanValues target;
    target.ac = { aspm_val, boost_val, core_parking_val, cpu_max_freq, cpu_max_freq, cpu_throttle_max, epp_val };
    target.dc = target.ac;

    ApuPowerPlanValues to_apply = target;
    std::string apply_scheme;  // empty: the active plan
    ChangeJournal* journal = GetActiveChangeJournal();
    uint64_t jid = 0;

    {
        std::lock_guard<std::mutex> state_lock(mutex_);
        // Reads the active plan and, when it differs from `target`, keeps it as the original and journals it before any write.
        // Returns false when the plan cannot be read (nothing may be written then); `unchanged` is set when there is nothing to write.
        auto save_original = [&](const char* reason, const char* after, bool& unchanged) {
            std::string scheme_str;
            ApuPowerPlanValues current;
            unchanged = false;
            if (!power_.ReadActiveApuPlan(scheme_str, current) ||
                current.ac.size() != kApuPowerSettingCount ||
                current.dc.size() != kApuPowerSettingCount) {
                return false;
            }
            // A setting this plan lacks is neither written now nor restored later.
            for (size_t i = 0; i < kApuPowerSettingCount; ++i) {
                if (current.ac[i] == kApuPowerSettingAbsent) target.ac[i] = target.dc[i] = to_apply.ac[i] = to_apply.dc[i] = kApuPowerSettingAbsent;
            }
            if (current.ac == target.ac && current.dc == target.dc) {
                unchanged = true;
                return true;
            }
            prev_values_ = current;
            saved_scheme_ = scheme_str;
            saved_ = true;

            if (journal && !scheme_str.empty()) {
                JournalEntry e;
                e.kind = JournalTargetKind::PowerPlan;
                e.target = "Plan de energ\u00eda APU";
                e.reason = reason;
                e.before_value = "ASPM=" + std::to_string(current.ac[0]) +
                                 ",Boost=" + std::to_string(current.ac[1]) +
                                 ",EPP=" + std::to_string(current.ac[6]);
                e.after_value = after;
                e.manual_fix_hint =
                    "vegaflow_daemon.exe --apu-balanced (o restablecer plan en Opciones de energ\u00eda de Windows)";
                e.scheme_guid = scheme_str;
                e.ac_values = current.ac;
                e.dc_values = current.dc;
                jid = journal->RecordPending(std::move(e));
            }
            return true;
        };

        if (mode == ApuPowerMode::GpuPrioritySustained) {
            if (!saved_) {
                // Without the original there is nothing to give back later, so the profile is not applied.
                bool unchanged = false;
                if (!save_original("Perfil GPU-Priority (ASPM=0, Boost=4, EPP=25)", "ASPM=0,Boost=4,EPP=25", unchanged)) return false;
            }
        } else if (saved_) {
            to_apply = prev_values_;
            apply_scheme = saved_scheme_;
            saved_ = false;
        } else {
            bool from_journal = false;
            if (journal) {
                for (const auto& entry : journal->Entries()) {
                    if (entry.kind == JournalTargetKind::PowerPlan &&
                        (entry.status == JournalEntryStatus::Pending || entry.status == JournalEntryStatus::Committed) &&
                        entry.ac_values.size() == kApuPowerSettingCount &&
                        entry.dc_values.size() == kApuPowerSettingCount) {
                        to_apply.ac = entry.ac_values;
                        to_apply.dc = entry.dc_values;
                        apply_scheme = entry.scheme_guid;
                        from_journal = true;
                        break;
                    }
                }
            }
            if (!from_journal) {
                // No copy at all: the manual "Balanced" first journals the plan it finds, and writes nothing if it cannot read it.
                bool unchanged = false;
                if (!save_original("Perfil Balanceado (ASPM=1, Boost=2, EPP=50)", "ASPM=1,Boost=2,EPP=50", unchanged)) return false;
                if (unchanged) return true;
            }
        }
    }

    const bool ok = apply_scheme.empty() ? power_.ApplyActiveApuPlan(to_apply) : power_.ApplyApuPlan(apply_scheme, to_apply);
    {
        std::lock_guard<std::mutex> state_lock(mutex_);
        if (ok) {
            applied_ = (mode == ApuPowerMode::GpuPrioritySustained);
        }
    }
    if (journal) {
        if (jid != 0) {
            if (ok) {
                journal->MarkCommitted(jid);
            } else {
                journal->MarkFailed(jid, "PowerSetActiveScheme fall\u00f3");
            }
        } else if (mode == ApuPowerMode::Balanced && ok) {
            journal->MarkMatchingReverted(JournalTargetKind::PowerPlan, "Plan de energ\u00eda APU");
        }
    }
    return ok;
}

bool PowerModeTweaks::IsApuPowerModeApplied() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applied_;
}

bool PowerModeTweaks::SetApuLaptopPowerProfile(bool optimize, const ApuOptimizationPolicy& policy, bool dry_run) {
    return SetApuPowerMode(optimize ? ApuPowerMode::GpuPrioritySustained : ApuPowerMode::Balanced, policy, dry_run);
}

bool PowerModeTweaks::RestoreIfApplied(const ApuOptimizationPolicy& policy, bool dry_run) {
    if (!IsApuPowerModeApplied()) return false;
    return SetApuLaptopPowerProfile(false, policy, dry_run);
}

bool PowerModeTweaks::QueryApuPowerOptimized() {
    return power_.QueryApuPowerOptimized();
}

} // namespace vegaflow
