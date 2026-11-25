# Modules: code layers

**In short:** each folder in `src/` is a layer, and a layer may only use the layers listed for it. Since phase R3, the use-case code does not call Windows itself: it asks `windows/` for the readings. For example, `--status` counts the processes in power-saving mode through `windows/`, so the `session/` code reads without any Windows detail. The full rule table is in the [index](./index.md#layer-table).

**Reader:** people

## Code layers (state after phase R3, 2026-11-14)

**The essentials:** each folder of `src/` may only include the folders below it. Since R3, the use cases in `session/` do not call Win32 functions: they ask `windows/` for those readings.

**Example.** `--status` shows how many processes have power-saving mode (EcoQoS) on. Before, `session/` walked the process list with Windows calls itself. Now `session/reports.cpp` calls `CountEcoQoSProcesses()`, and the one that talks to Windows is [`src/windows/system_counters.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/system_counters.cpp). That way the `session/` code reads without knowing anything about Win32.

![A stack of src/ folders, from app/ at the top to model/ at the bottom, with arrows for the main may-include relations; session/ holds the use cases and is highlighted.](../img/code-layers.png)

Terms: a **port** is a small interface (for example, "read and write the registry") that the tests can replace with a fake version; an **adapter** is the real version, which calls Windows.

### What R3 moved

| What moved in R3 | Where it went |
| --- | --- |
| Count of processes in EcoQoS and of games with Direct Flip set | `windows/system_counters.*` |
| L3 cache size (cache topology; if that fails, CPUID, leaf `0x80000006`; 0 if none answers) | `windows/system_counters.*` |
| RAM slots used according to SMBIOS, with a bounded walk: it stops at a truncated entry instead of reading past the end | `windows/system_counters.*` (the pure part, `CountDimmsInSmbiosTable`, is tested with invented tables) |
| Check a window and its thread | `windows/system_counters.*` |
| UTF-8 ↔ UTF-16 conversion (`text_convert`) | from `session/` to `windows/text_convert.*` |
| Version (0.1.0), window class and title, tray command id and the line "processor \| graphics" | `ui/app_identity.hpp` |

### What the repository checks

- `scripts/ci/check_layers.sh` checks every `#include` in `src/` against the [layer table](./index.md#layer-table): 485 includes checked, layers respected (2026-11-25). It runs in CI.
- `session/` is strict since F4.3 (2026-11-25), except for six files listed in the `debt()` function of the script (see [what is still open](#what-is-still-open-in-session)). Before, the whole folder was allowed to include `detect/`, `optimize/` and `windows/`: 16 files and 58 includes. Now it is 6 files and 33 includes.
- The tests that touch the real registry are only in `tests/integration/`; `tests/unit/` uses fake ports. C++ suite on the test laptop, 2026-11-14: `57 PASSED, 0 FAILED`.

### What F4.3 moved (2026-11-25)

| What moved | Where it went |
| --- | --- |
| The reads of `--doctor` (disks, shader caches, hardware reasons, config.json warning, process list, computer name) | behind the `DoctorProbe` port (`ports/doctor_probe.hpp`); the Windows code is `app/doctor_probe.cpp`, and `session/doctor.cpp` includes nothing from `windows/`, `detect/` or `optimize/` |
| What the Juegos buttons change in a game folder (apply, restore, remove DXVK) | behind the `GameFiles` port (`ports/game_files.hpp`); `optimize/game_files_adapter.hpp` forwards to `SteamEngineOptimizer` and `DxvkManager` |
| UTF-8 ↔ UTF-16 conversion (`text_convert.hpp`) | declared in `ports/`; `windows/text_convert.cpp` still implements it |
| The plain data of the optimizers (`MemoryPurgeConfig` and the memory reports, `SteamGameEntry` and the engine types, `SystemGraphicsTweaksState`, `ApuPowerMode`) | `model/memory.hpp`, `model/steam.hpp`, `model/tweaks.hpp` |
| Hardware enum names (`VendorToString` and the other six), the Unreal/Unity preset text and keys, the file-name rule of a Windows path | `model/hardware_names.hpp`, `model/engine_presets.*`, `model/path_name.hpp` |
| The sentences of the hardware profile reasons (a Rust call) | `session/profile_reasons.*` |

Terms: the **host** is this Mac (or any machine without Windows); the host tests run the C++ unit tests there with a stand-in for `text_convert` (`tests/host/host_stubs.cc`). Host tests went from 134 to 238.

### What is still open in `session/`

Six files still use concrete classes of `detect/`, `optimize/` or `windows/`, because each one needs an interface for a whole family of calls (the `GraphicsOptimizer` coordinator alone is used in about 34 places of `settings_actions.cpp`). They are the next step, one class at a time:

| File | Includes still there | Why |
| --- | --- | --- |
| `reports.cpp:26-37` | `config_location`, `privileges`, `hardware_profiler`, `process_detector`, `graphics_optimizer`, `memory_manager`, `cpu_orchestrator`, `steam_engine_optimizer`, `dxvk_manager`, `power`, `registry`, `system_counters` | `--status`, `--dry-run` and `--purge-now` build the optimizers and adapters themselves |
| `run_cycle.cpp:11-23`, `run_cycle.hpp:10-12` | `hardware_profiler`, `process_detector`, `graphics_optimizer`, `memory_manager`, `cpu_orchestrator`, `steam_engine_optimizer`, `device_probe`, `power`, `registry`, `system_counters` | one cycle takes `CpuOrchestrator&` and `MemoryManager&` and creates the Windows adapters |
| `system_info.cpp:2-7` | `hardware_profiler`, `memory_manager`, `graphics_optimizer`, `power`, `registry`, `system_counters` | the report builds a `GraphicsOptimizer` to read the tweaks |
| `game_session.cpp:4-6` | `hardware_profiler`, `cpu_orchestrator`, `system_counters` | `HardwareProfiler::BuildOptimizationPolicy`, CPU Sets and `WindowThreadId` |
| `settings_actions.hpp:11` | `graphics_optimizer` | `SettingsActionsDeps` holds `GraphicsOptimizer&` |
