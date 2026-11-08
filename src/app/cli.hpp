#pragma once
// Command line: option parsing and the one-shot commands that run and exit.

#include <optional>
#include <string>

namespace vegaflow::app {

struct CliOptions {
    bool status_flag = false;
    bool dry_run_flag = false;
    bool once_flag = false;
    bool elevate_flag = false;
    bool purge_now_flag = false;
    bool mpo_disable_flag = false;
    bool mpo_restore_flag = false;
    bool shader_cache_force_flag = false;
    bool shader_cache_default_flag = false;
    bool mem_compression_off_flag = false;
    bool mem_compression_on_flag = false;
    bool apu_gpu_priority_flag = false;
    bool apu_balanced_flag = false;
    bool vram_override_flag = false;
    bool vram_restore_flag = false;
    bool restore_registry_flag = false;
    bool fsr_validate_flag = false;
    bool monitor_flag = false;
    bool gui_flag = false;
    bool system_info_flag = false;
    std::string dxvk_install_path;
    std::string dxvk_remove_path;
};

/// Parses argv into `opts`. Returns an exit code if the process must exit right away (e.g. --help).
std::optional<int> ParseCommandLine(int argc, char* argv[], CliOptions& opts);

/// Runs the commands that do their work and exit (--system-info, --status, --purge-now, --mpo-*, ...).
/// Returns the exit code if one ran, or std::nullopt when the daemon must keep starting.
std::optional<int> RunOneShotCommand(const CliOptions& opts, int argc, char* argv[]);

} // namespace vegaflow::app
