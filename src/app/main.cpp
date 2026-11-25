#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "app/app_state.hpp"
#include "app/cli.hpp"
#include "session/config.hpp"
#include "session/restore.hpp"
#include "session/startup_prompts.hpp"
#include "app/daemon_loop.hpp"
#include "app/gui_mode.hpp"
#include "app/monitor.hpp"
#include "session/reports.hpp"
#include "ui/app_identity.hpp"
#include "ports/text_convert.hpp"
#include "windows/change_journal.hpp"
#include "windows/command_line.hpp"
#include "windows/console_prompt.hpp"
#include "windows/config_location.hpp"
#include "windows/power.hpp"
#include "windows/registry.hpp"
#include "windows/safe_file_system.hpp"

using namespace vegaflow::app;

namespace {

LONG WINAPI VegaFlowCrashHandler(EXCEPTION_POINTERS* /*ep*/) {
    vegaflow::SetActiveChangeJournal(nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}

// A startup question: nullopt when nobody can answer (hidden console, pipe, file) or nobody did in time, so an unattended start never waits.
std::optional<std::string> AskPerson(const std::string& prompt, bool once_run) {
    if (!vegaflow::session::CanAskPerson(vegaflow::ClassifyStdin(), once_run)) return std::nullopt;
    vegaflow::ConsoleKeyInput keys;
    auto answer = vegaflow::session::ReadLineWithDeadline(keys, vegaflow::SteadyClock::Instance(), std::cout, prompt, vegaflow::session::kStartupPromptTimeoutMs);
    if (!answer) std::cout << "[INFO] Sin respuesta a tiempo: no se cambia nada.\n";
    return answer;
}

struct JournalSessionGuard {
    vegaflow::ChangeJournal* journal = nullptr;
    bool end_clean = false;
    ~JournalSessionGuard() {
        vegaflow::SetActiveChangeJournal(nullptr);
        if (journal && end_clean) {
            journal->EndSessionClean();
        }
    }
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    SetUnhandledExceptionFilter(VegaFlowCrashHandler);
    SetConsoleOutputCP(CP_UTF8);

    // The CRT hands argv over in the ANSI code page; the parser and Utf8ToWide expect UTF-8.
    std::vector<std::string> utf8_args = vegaflow::Utf8Args(GetCommandLineW());
    std::vector<char*> utf8_argv;
    for (auto& a : utf8_args) utf8_argv.push_back(a.data());
    if (!utf8_argv.empty()) {
        argc = static_cast<int>(utf8_argv.size());
        argv = utf8_argv.data();
    }

    CliOptions opts;
    if (const auto exit_code = ParseCommandLine(argc, argv, opts)) return *exit_code;
    if (const auto exit_code = RunOneShotCommand(opts, argc, argv)) return *exit_code;

    // One path, next to the executable; it is only read when no account without administrator rights could write it.
    vegaflow::SafeFileSystem config_fs;
    const std::wstring config_path = vegaflow::ConfigFilePath();
    ConfigStore config_store(config_fs, config_path, vegaflow::ConfigLocationTrusted(config_path));
    AppConfig config = config_store.Load();
    if (const std::string warning = config_store.Report().Warning(); !warning.empty()) {
        std::cerr << "[WARN] " << warning << "\n";
        LogNoticeToSessionLog(warning); // F3.3: a hidden daemon has no console
    }

    // --dry-run wins over every mode: the monitor would otherwise run the real cycle.
    if (opts.dry_run_flag) {
        ExecuteDryRun(config);
        return 0;
    }

    if (!opts.once_flag) {
        g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
        if (g_single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExisting = FindWindowW(vegaflow::ui::kWindowClass, nullptr);
            if (hExisting) {
                PostMessageW(hExisting, RegisterWindowMessageW(vegaflow::ui::kRestoreWindowMessage), 0, 0);
                SetForegroundWindow(hExisting);
                std::cout << "[INFO] VegaFlow Universal is already running. Focusing existing instance.\n";
            } else {
                std::cout << "[INFO] VegaFlow Universal is already running in the background (no window to show).\n";
            }
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
            return 0;
        }
    }

    using namespace vegaflow;
    PrivilegeStatus priv = PrivilegeManager::EnableRequiredPrivileges(false);
    SystemHardwareProfile hw = HardwareProfiler::DetectHardware(opts.gui_flag);
    ApuOptimizationPolicy boot_policy = HardwareProfiler::BuildOptimizationPolicy(hw);

    SteadyClock journal_clock;
    FileChangeJournal journal(config_fs, journal_clock, JournalFilePath());
    WindowsSystemUndo system_undo(config_fs);
    journal.Load();
    StartupJournalEvaluation journal_eval = vegaflow::session::EvaluateStartupJournal(journal);
    std::string journal_warning;

    if (journal_eval.corrupt) {
        journal_warning = "Diario de cambios corrupto (" + journal_eval.corrupt_reason +
                          "). VegaFlow arrancó igualmente; usa --restore-registry para devolver valores.";
        std::cerr << "[WARN] " << journal_warning << "\n";
    } else if (journal_eval.previous_unclean_exit || journal_eval.unclosed_or_unreverted_count > 0) {
        std::string rec_notice = "Se detectó un cierre anterior no limpio (" +
                                 std::to_string(journal_eval.unclosed_or_unreverted_count) +
                                 " cambios sin cerrar en el diario).";
        std::cout << "[WARN] " << rec_notice << "\n";
        if (opts.gui_flag) {
            std::wstring body = Utf8ToWide(
                rec_notice +
                (journal_eval.safe_mode ? "\n\nModo Seguro activado tras 2+ cierres anormales." : "") +
                "\n\n¿Deseas devolver todos esos cambios a como estaban antes ahora?");
            int btn = MessageBoxW(nullptr, body.c_str(), L"VegaFlow Universal — Recuperación", MB_YESNO | MB_ICONWARNING);
            if (btn == IDYES) {
                auto rep = vegaflow::session::RestoreAllRecordedChanges(journal, system_undo, opts.dry_run_flag);
                WindowsRegistryPort().RestoreAllBackups();
                journal_warning = rep.summary_message;
                journal_eval = vegaflow::session::EvaluateStartupJournal(journal);
            } else if (journal_eval.safe_mode) {
                journal_warning = "Modo Seguro activo tras 2+ cierres anormales seguidos. Sin cambios aplicados.";
            } else {
                journal_warning = rec_notice;
            }
        } else {
            if (vegaflow::session::ShouldRevertPending(
                    AskPerson("¿Devolver los cambios pendientes a como estaban? [S/n] (30 s): ", opts.once_flag))) {
                auto rep = vegaflow::session::RestoreAllRecordedChanges(journal, system_undo, opts.dry_run_flag);
                WindowsRegistryPort().RestoreAllBackups();
                std::cout << "[Restore] " << rep.summary_message << "\n";
                journal_eval = vegaflow::session::EvaluateStartupJournal(journal);
            } else {
                std::cout << "[INFO] Puedes recuperarlos luego con --restore-registry.\n";
            }
        }
    }

    const bool safe_mode = opts.observe_flag || (!journal_eval.corrupt && journal_eval.safe_mode);
    const std::string safe_reason = opts.observe_flag ? "pedido con --observe: solo observar"
                                                      : "se detectaron 2 o más cierres anormales seguidos";
    JournalSessionGuard session_guard;
    if (!journal_eval.corrupt) {
        if (!safe_mode && !opts.dry_run_flag && vegaflow::session::ShouldOfferRestorePoint(journal)) {
            if (opts.gui_flag) {
                int rp_btn = MessageBoxW(
                    nullptr,
                    L"¿Quieres crear un punto de restauración de Windows antes de que VegaFlow realice el primer cambio en el registro?",
                    L"VegaFlow Universal — Punto de restauración opcional",
                    MB_YESNO | MB_ICONQUESTION);
                std::string rp_msg;
                vegaflow::session::ExecuteRestorePointOffer(
                    journal, system_undo, rp_btn == IDYES,
                    "VegaFlow Universal - Antes del primer cambio del registro", &rp_msg);
                if (!rp_msg.empty() && journal_warning.empty()) {
                    journal_warning = rp_msg;
                }
            } else if (!opts.once_flag) {
                // No answer leaves the journal unmarked: the next attended start offers it again.
                if (const auto accepted = vegaflow::session::RestorePointAnswer(AskPerson(
                        "¿Crear un punto de restauración de Windows antes del primer cambio del registro? [s/N] (30 s): ", opts.once_flag))) {
                    std::string rp_msg;
                    vegaflow::session::ExecuteRestorePointOffer(
                        journal, system_undo, *accepted,
                        "VegaFlow Universal - Antes del primer cambio del registro", &rp_msg);
                    std::cout << "[RestorePoint] " << rp_msg << "\n";
                }
            }
        }

        journal.BeginSession();
        SetActiveChangeJournal(&journal);
        session_guard.journal = &journal;
        session_guard.end_clean = true;
    }

    WindowsRegistryPort boot_reg;
    WindowsPowerPort boot_power;
    GraphicsOptimizer boot_opt(hw, boot_policy, boot_reg, &boot_power);
    // E2.2: opt-in in config.json (auto_vram_reporting); unmeasured, so never on its own.
    if (!safe_mode && priv.is_elevated && config.auto_vram_reporting && boot_policy.apply_uma_vram_reg_override && boot_opt.AmdDriver()) {
        boot_opt.AmdDriver()->ApplyVramReportingOverride(boot_policy.safe_vram_pool_mb, opts.dry_run_flag);
    }

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    mem_mgr.SetTargetLuid(hw.primary_gpu.luid);
    CpuOrchestrator cpu_orchestrator;

    if (opts.monitor_flag) {
        RunMonitorDashboard(config, hw, cpu_orchestrator, mem_mgr, opts.once_flag, safe_mode, safe_reason);
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        return 0;
    }

    if (opts.gui_flag) {
        return RunGuiMode(config, config_store, hw, cpu_orchestrator, mem_mgr, safe_mode, journal_warning);
    }

    return RunDaemon(config, hw, cpu_orchestrator, mem_mgr, priv, opts.once_flag, safe_mode, safe_reason);
}
