#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include "detect/hardware_profiler.hpp"
#include "optimize/graphics/graphics_optimizer.hpp"
#include "optimize/memory_manager.hpp"
#include "optimize/cpu_orchestrator.hpp"
#include "app/app_state.hpp"
#include "app/cli.hpp"
#include "session/config.hpp"
#include "session/restore.hpp"
#include "app/daemon_loop.hpp"
#include "app/gui_mode.hpp"
#include "app/monitor.hpp"
#include "session/reports.hpp"
#include "session/text_convert.hpp"
#include "windows/change_journal.hpp"
#include "windows/config_location.hpp"
#include "windows/safe_file_system.hpp"

using namespace vegaflow::app;

namespace {

LONG WINAPI VegaFlowCrashHandler(EXCEPTION_POINTERS* /*ep*/) {
    vegaflow::SetActiveChangeJournal(nullptr);
    vegaflow::GraphicsOptimizer::RestoreDefaultTimer();
    return EXCEPTION_EXECUTE_HANDLER;
}

bool HasInteractiveConsole() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (!hIn || hIn == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    return GetFileType(hIn) == FILE_TYPE_CHAR && GetConsoleMode(hIn, &mode) != 0;
}

bool TryReadPromptAnswer(const std::string& prompt, std::string& out_answer) {
    out_answer.clear();
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (!hIn || hIn == INVALID_HANDLE_VALUE) return false;
    const DWORD type = GetFileType(hIn);
    if (type == FILE_TYPE_CHAR) {
        DWORD mode = 0;
        if (!GetConsoleMode(hIn, &mode)) return false;
        std::cout << prompt << std::flush;
        return static_cast<bool>(std::getline(std::cin, out_answer));
    }
    if (type == FILE_TYPE_PIPE) {
        DWORD avail = 0;
        if (!PeekNamedPipe(hIn, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) {
            return false;
        }
        std::cout << prompt << std::flush;
        return static_cast<bool>(std::getline(std::cin, out_answer));
    }
    return false;
}

bool IsAffirmative(std::string s, bool empty_means_yes) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.empty()) return empty_means_yes;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (s == "s" || s == "si" || s == "sí" || s == "y" || s == "yes");
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
    }

    if (opts.dry_run_flag && !opts.monitor_flag) {
        ExecuteDryRun(config);
        return 0;
    }

    if (!opts.once_flag) {
        g_single_instance_mutex = CreateMutexW(nullptr, FALSE, L"Local\\VegaFlowUniversal_SingleInstance");
        if (g_single_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND hExisting = FindWindowW(L"VegaFlowUniversalUxtuClass", nullptr);
            if (hExisting) {
                PostMessageW(hExisting, WM_COMMAND, 2001 /* ID_TRAY_RESTORE */, 0);
                SetForegroundWindow(hExisting);
            }
            std::cout << "[INFO] VegaFlow Universal is already running. Focusing existing instance.\n";
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
                GraphicsOptimizer::RestoreAllRegistryBackups();
                journal_warning = rep.summary_message;
                journal_eval = vegaflow::session::EvaluateStartupJournal(journal);
            } else if (journal_eval.safe_mode) {
                journal_warning = "Modo Seguro activo tras 2+ cierres anormales seguidos. Sin cambios aplicados.";
            } else {
                journal_warning = rec_notice;
            }
        } else {
            std::string ans;
            if (TryReadPromptAnswer("¿Devolver los cambios pendientes a como estaban? [S/n]: ", ans) &&
                IsAffirmative(ans, true)) {
                auto rep = vegaflow::session::RestoreAllRecordedChanges(journal, system_undo, opts.dry_run_flag);
                GraphicsOptimizer::RestoreAllRegistryBackups();
                std::cout << "[Restore] " << rep.summary_message << "\n";
                journal_eval = vegaflow::session::EvaluateStartupJournal(journal);
            } else {
                std::cout << "[INFO] Puedes recuperarlos luego con --restore-registry.\n";
            }
        }
    }

    const bool safe_mode = (!journal_eval.corrupt && journal_eval.safe_mode);
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
            } else if (!opts.once_flag && HasInteractiveConsole()) {
                std::string ans;
                if (TryReadPromptAnswer("¿Crear un punto de restauración de Windows antes del primer cambio del registro? [s/N]: ", ans)) {
                    std::string rp_msg;
                    vegaflow::session::ExecuteRestorePointOffer(
                        journal, system_undo, IsAffirmative(ans, false),
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

    if (!safe_mode && priv.is_elevated && boot_policy.apply_uma_vram_reg_override && !boot_policy.block_driver_tweaks) {
        GraphicsOptimizer::ApplyAmdVramReportingOverride(boot_policy.safe_vram_pool_mb, opts.dry_run_flag);
    }

    MemoryPurgeConfig mem_cfg = config.memory;
    mem_cfg.is_uma_architecture = (hw.primary_gpu.mem_arch == MemoryArchitecture::UMA_Integrated);
    MemoryManager mem_mgr(mem_cfg);
    CpuOrchestrator cpu_orchestrator;

    if (opts.monitor_flag) {
        RunMonitorDashboard(config, hw, cpu_orchestrator, mem_mgr, opts.once_flag);
        if (g_single_instance_mutex) {
            CloseHandle(g_single_instance_mutex);
            g_single_instance_mutex = nullptr;
        }
        return 0;
    }

    if (opts.gui_flag) {
        return RunGuiMode(config, config_store, hw, cpu_orchestrator, mem_mgr, safe_mode, journal_warning);
    }

    return RunDaemon(config, hw, cpu_orchestrator, mem_mgr, priv, opts.once_flag, safe_mode);
}
