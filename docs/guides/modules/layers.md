# Modules: code layers

**In short:** each folder in `src/` is a layer, and a layer may only use the layers listed for it. Since phase R3, the use-case code does not call Windows itself: it asks `windows/` for the readings. For example, `--status` counts the processes in power-saving mode through `windows/`, so the `session/` code reads without any Windows detail. The full rule table is in the [index](./index.md#layer-table).

**Reader:** people

## Code layers (state after phase R3, 2026-11-14)

**The essentials:** each folder of `src/` may only include the folders below it. Since R3, the use cases in `session/` do not call Win32 functions: they ask `windows/` for those readings.

**Example.** `--status` shows how many processes have power-saving mode (EcoQoS) on. Before, `session/` walked the process list with Windows calls itself. Now the `--status` command (`app/report_commands.cpp`) calls `CountEcoQoSProcesses()`, and the one that talks to Windows is [`src/windows/system_counters.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/system_counters.cpp). That way the `session/` code reads without knowing anything about Win32.

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

- `scripts/ci/check_layers.sh` checks every `#include` in `src/` against the [layer table](./index.md#layer-table): 523 includes checked, layers respected (2026-11-25). It runs in CI.
- `session/` is strict since F4.3 (2026-11-25), with no exception: a file of `session/` that includes `detect/`, `optimize/` or `windows/` fails the check. Before, the whole folder was allowed to include them: 16 files and 58 includes. Now it is 0 files and 0 includes. The `debt()` exception list and the "Migration" mark of the script are gone.
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
| The policy that turns a hardware profile into limits (`BuildOptimizationPolicy`), the CPU Set split and the architecture code for Rust | `bridge/hardware_policy.*` (bridge may now include `model/` and `ports/`); the current processor group travels in the device snapshot |
| One cycle of the daemon and the real-Windows game environment (`WindowsGameSessionEnv`, now with `WindowThreadId`) | `app/run_cycle.*`; `session/game_session.*` only sees the `GameSessionEnvironment` interface |
| What `SystemInfo` reads (hardware, memory sample, tweaks, L3 cache, DIMM slots) | behind the `SystemReads` port (`ports/system_reads.hpp`); `app/system_reads.hpp` is the Windows adapter |
| `--status`, `--purge-now` and `--dry-run`, which build the real detectors and optimizers | `app/report_commands.*`; `session/reports.*` keeps the session log and the report lines |
| The folder of the .exe and the path of config.json (`ExecutableDirectory`, `ConfigFilePath`, `ConfigLocationTrusted`) | declared in `ports/config_location.hpp`; `windows/config_location.cpp` implements it |
| The switches of the Ajustes screen (MPO, memory compression, MMCSS, GameDVR, AMD driver, power plan, registry copies) | behind the `TweakControls` port (`ports/tweak_controls.hpp`); `GraphicsOptimizer` implements it |

Terms: the **host** is this Mac (or any machine without Windows); the host tests run the C++ unit tests there with a stand-in for `text_convert` (`tests/host/host_stubs.cc`). Host tests went from 134 to 268.
