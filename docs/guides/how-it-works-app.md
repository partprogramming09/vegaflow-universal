# How app/ works

**In short:** VegaFlow is one program with several jobs: a background loop, a window, a console monitor, a simulation, and small commands that change one setting and exit. `src/app/` reads what you typed and decides which job runs.

It reads the flags first, then loads the settings, then checks whether the last run ended cleanly. Only after that does it start the chosen mode.

The one thing not to forget: `--dry-run` only simulates and wins over every other mode, and a misspelled flag stops the program with exit code 2 instead of starting the background loop (both since the 2026-11-20 fix).

**Reader:** people and agents. **Last review:** 2026-11-22. **Explains:** `src/app/`. **Answers:** what happens when VegaFlow starts, and which flag sends it to which mode?

## Why it exists

VegaFlow can run in the background, show a window, print a live monitor, simulate its work, or run a command that changes one setting and exits. Something has to decide which of these runs when you start it. That is `src/app/`, the entry point (the first code that runs; in C++ it is called `main`).

It is also the only place that starts the parts in order and stops them in order. It loads the settings, checks for a bad previous run, and makes sure the changes are undone when the program ends.

A few terms used below. A **flag** is an option typed after the program name, such as `--gui`. A **daemon** is a program that runs in the background without a window. The **registry** is the Windows settings database. The **journal** is a log file where VegaFlow notes each change it makes, so it can undo them later. **Administrator** (or admin) is the Windows account level that may change system settings.

## A concrete example

This is the real output of `vegaflow_daemon.exe --dry-run --once` on the test laptop (AMD Ryzen 5 4600H with Vega 6 graphics, Windows 11 build 26300, run on 2026-11-20). The program only simulated; it changed nothing.

```text
[WARN] config.json está en una carpeta donde pueden escribir cuentas sin permisos de administrador: se usan los valores de fábrica y no se guarda nada.
VEGAFLOW DAEMON (DRY-RUN SIMULATION & EVALUATION)
[1/4] Scanning foreground processes for 3D Game / Graphics API...
  -> No foreground 3D game currently detected.
  -> Demonstrating evaluation on background processes under simulated game conditions:
[2/4] [DRY-RUN] Graphics / Registry Tweaks: Idle (applied on game launch)
[3/4] [DRY-RUN] CPU Orchestrator Evaluation:
  -> Background Processes Found: 22 PIDs
  -> Background Sets to confine: 2 core IDs (would be isolated: 22 background processes; nothing changed)
[4/4] [DRY-RUN] Memory Manager Evaluation:
  -> Standby Purge Triggered : NO
  -> Background Trim Count   : 0
  -> Evaluation Reason       : Thresholds not met: Free/Zero=7132MB (min 1200MB), Standby=189MB (min 1500MB)
[KPI] Daemon Memory Working Set: 2.09375 MB (< 15 MB KPI passed)
```

How to read it, line by line:

- The first line is a warning from the settings step (`main.cpp:90-92`). The settings folder can be written by ordinary accounts, so VegaFlow used its factory values and saved nothing.
- `[1/4]` looks for a game in the foreground. None was open, so the simulation used the background programs as a stand-in (`reports.cpp:376-377`, `:429-431`).
- `22 PIDs` are the process IDs (the numbers Windows gives each program) of background programs. `2 core IDs` are the CPU sets (groups of processor cores) the background work would be kept in (`reports.cpp:442-452`). "Would be isolated" and "nothing changed" say that a simulation touches no process.
- `Standby` is memory that Windows keeps as a cache. The memory step decided not to clear it, and the reason line gives the numbers it checked (`reports.cpp:456-458`).
- The `KPI` line shows the daemon's own memory use, about 2 MB. It says "KPI passed" below 15 MB and "KPI FAILED" at 15 MB or more (`reports.cpp:463`, `:476-481`).
- `--once` does not change this output. The simulation branch runs before any code reads that flag (`main.cpp:94-98`).

## How it works, step by step

![Flow from the start of vegaflow_daemon.exe to the chosen mode: read flags, one-shot commands, elevation, settings, dry-run exit, single-instance lock, journal check, session start, mode choice](./img/how-it-works-app.png)

| Step | File:line | What to look at | Why |
| --- | --- | --- | --- |
| 1 | [`cli.cpp:31`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/cli.cpp#L31) | `ParseCommandLine` checks each argument and sets one `bool` in `CliOptions`. `--version` and `--help` print and return at once. | Every later decision reads these flags. |
| 2 | [`cli.cpp:141`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/cli.cpp#L141), [`main.cpp:83`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L83) | `RunOneShotCommand` runs the commands that do one job and exit: `--system-info`, `--status`, `--restore-registry`, `--purge-now`, the `--mpo-*` and `--dxvk-*` pairs. A returned exit code ends `main`. | These run before the settings are loaded, so they stay quick and do not start a mode. |
| 3 | [`cli.cpp:158-166`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/cli.cpp#L158-L166) | If the process is not Administrator, and the run is not `--dry-run` or `--status`, it starts itself again through UAC (the Windows prompt that asks to elevate) with the same flags, each one quoted so Windows reads it back whole (`BuildForwardedArgs`, [`command_line.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/command_line.hpp)). Before step 1, `main` reads the arguments as UTF-8 from `GetCommandLineW` ([`main.cpp:72-79`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L72-L79)): Windows hands `argv` over in the ANSI code page, which broke a path with "ñ". With no flags it asks for `--gui`. | Changing the registry, processes and power plans needs Administrator rights. |
| 4 | [`main.cpp:88-92`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L88-L92) | The settings are read from `config\config.json` next to the `.exe` (`config_location.cpp:60-63`). A warning is printed if that folder is not safe. | Every mode uses the settings, so they are read once here. |
| 5 | [`main.cpp:94-98`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L94-L98) | `--dry-run` runs `ExecuteDryRun` and returns 0, also with `--monitor` (fix of 2026-11-20; before, `--monitor` skipped the simulation). It does not take the single-instance lock and does not open the journal. | A simulation must not touch the system. |
| 6 | [`main.cpp:100-115`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L100-L115) | A named mutex (a Windows lock object) called `Local\VegaFlowUniversal_SingleInstance` allows one copy. A second GUI copy brings the first window forward; when the first copy has no window, the second says so. Skipped with `--once`. | Two loops or two windows would fight over the same settings. |
| 7 | [`main.cpp:125-165`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L125-L165), [`restore.cpp:37`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/restore.cpp#L37) | The journal in `config\change_journal.vfj` says whether the last run ended cleanly. If it did not, the user is asked whether to undo the changes it left. On the console that question goes through `AskPerson` ([`main.cpp:48-53`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L48-L53)), which only asks a visible console and waits at most 30 s (see [Startup questions](#startup-questions-never-wait-forever)). | This is the only memory of a crash, so it is read before anything new starts. |
| 8 | [`main.cpp:167-203`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L167-L203) | Computes `safe_mode` and the reason it shows (`--observe`, or two unclean ends in a row; see [`change_journal.cpp:834`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/change_journal.cpp#L834)). Offers a Windows restore point before the first change, not in dry-run, `--once` or safe mode, through the same `AskPerson`. Calls `BeginSession`; a guard object marks the run as clean when `main` returns ([`main.cpp:55-63`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L55-L63)). | The next start can tell a clean end from a crash. |
| 9 | [`main.cpp:220`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L220), [`main.cpp:229`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L229), [`main.cpp:232`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp#L232) | `--monitor` goes to `RunMonitorDashboard`, `--gui` to `RunGuiMode`, and anything else to `RunDaemon`, the background loop. | One place chooses the mode, so every mode gets the same setup. |

## The pieces inside

| File | Responsibility |
| --- | --- |
| [`main.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp) | The entry point; runs steps 1 to 9. Holds the crash handler (lines 40-43), the console questions (45-80) and the clean-exit guard (82-91). |
| [`cli.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/cli.hpp), [`cli.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/cli.cpp) | `CliOptions` (one `bool` per flag, plus two paths for DXVK), `ParseCommandLine`, `RunOneShotCommand` and the `--help` text. |
| [`daemon_loop.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/daemon_loop.cpp) | `RunDaemon`: the background loop, or a single tick with `--once`. |
| [`gui_mode.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/gui_mode.cpp) | `RunGuiMode`: builds the functions that the window's buttons call, runs the window loop (line 441), then undoes its changes (lines 442-451). |
| [`monitor.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/monitor.cpp) | `RunMonitorDashboard`: prints one telemetry line per poll interval. |
| [`app_state.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/app_state.cpp) | Three process-wide values (a run flag, a stop event and the lock handle) and the console handler. |
| [`session/reports.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/reports.cpp) | `PrintStatusReport` (line 154), `ExecutePurgeNow` (332) and `ExecuteDryRun` (360). |
| [`session/run_cycle.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/run_cycle.cpp) | `RunSingleIteration` (line 147): one evaluation tick. The loop, the window and the monitor all call it, and it hands the work to `RunGameSessionCycle` in [`game_session.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/game_session.cpp). |

**How they talk.** `main` creates the long-lived objects once (hardware profile, memory manager, CPU orchestrator, settings) and passes them by reference to the mode. Each mode keeps its own state, such as the ID of the game being optimized. The app layer may include everything, because its job is to assemble the pieces; the rule is on the [code layers](./modules/layers.md) page.

### The modes

| Mode | Flag | What it does | Changes the system? |
| --- | --- | --- | --- |
| Background loop | none (the default) | Waits for a stop request or a timer tick. Each tick runs one evaluation. On stop it undoes its changes. | Yes, unless in safe mode. |
| One tick | `--once` | The same loop with one tick, then it undoes its changes and exits. | Yes, for the length of the run; undone before exit. |
| Simulation | `--dry-run` | Prints what it would do, with the `dry_run` flag set. | No. |
| Monitor | `--monitor` | Prints one telemetry line per poll interval (free RAM, standby memory, VRAM, active game). | Yes, when a game starts: the tick applies the same per-game changes as the loop (the new-game branch starts at `game_session.cpp:264`). The start-up settings of the loop and the window are not set. |
| Window | `--gui` | Opens the Win32 window with tray support; sets GameDVR and the power profile at start and undoes them on close. | Yes, unless in safe mode. |
| Observe / safe mode | `--observe`, or automatic after two unclean ends | No automatic change. The loop and the window skip the tick. | No, automatically. |
| Status | `--status` | Prints a report of the hardware and the system state. | No. |
| System info | `--system-info` | Prints the hardware and OS data as JSON. | No. |
| Restore | `--restore-registry` or `--recover` | Replays the journal, then restores the saved registry values. | Yes: it puts values back. |

**The loop in detail.** After the start banner, `RunWakeLoop` (`src/session/wake_loop.cpp`) waits on an `EventSource` (`src/ports/event_source.hpp`). The Windows one (`src/windows/event_sources.cpp`) wakes the thread for five reasons: **Stop** (the stop event), **Foreground** (`SetWinEventHook` on `EVENT_SYSTEM_FOREGROUND`), **Device** (`CM_Register_Notification` for display-adapter arrival and removal), **Power** (a message-only window with `RegisterPowerSettingNotification` for the AC/DC source, handled as `PBT_POWERSETTINGCHANGE`) and **Timer**. The wait is `MsgWaitForMultipleObjectsEx`, and the message queue is drained with `PeekMessageW` before waiting again. Every wake except Stop runs one `RunSingleIteration`, unless the run is in safe mode. The timer is only a heartbeat: with no game it fires every 30 s (a safety net in case an event is lost); with a game it fires every `poll_interval_ms` (never below 250 ms). Wakes closer than 250 ms are merged into one iteration, so an Alt+Tab storm costs one cycle per 250 ms. The adapter signature is read only on a Device event, on the idle safety net, or in polling mode, not on every tick. If a hook cannot be registered, the loop logs a warning and polls every 1500 ms as before. On the way out, the loop restores the CPU sets, the power profile, GameDVR (the Windows game-recording feature) and the saved registry values, exactly once.

**One copy at a time.** `app_state.cpp` keeps three values: `g_running` (a flag the loop checks), `g_stop_event` (the event the loop waits on) and `g_single_instance_mutex` (the lock). The console handler (`app_state.cpp:9-24`) clears the flag and signals the event on Ctrl+C, when the console closes, on logoff and on shutdown. The stop event has a name, `Global\VegaFlowStop` (`daemon_loop.cpp:84-85`), so an elevated script can ask the loop to stop cleanly without a console.

## Why it was built this way

- **Events, not a polling loop.** The thread sleeps until a window-focus, adapter or power event, the stop event, or the heartbeat timer wakes it. The factory `poll_interval_ms` is still 1500 ms ([`config.hpp:24`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/config.hpp#L24)); set 2500 ms to get the planned 2.5 s tick with a game open.
- **A named stop event.** It lets a script request a clean shutdown, which runs the restore steps, without a console window (`daemon_loop.cpp:84-85`).
- **One message through the security filter.** Windows blocks messages between windows of different privilege levels (UIPI, User Interface Privilege Isolation). The second-launch message is the one message the code lets through on purpose (`gui_window.cpp:306` and `gui_window.cpp:386`).
- **Two unclean ends, then safe mode.** The start-up message names the reason (two unclean ends, or `--observe`), says the program starts "without applying the system tweaks" and that `--restore-registry` reverts them (`daemon_loop.cpp:51-53`). The counter is computed in [`change_journal.cpp:834`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/change_journal.cpp#L834).
- **Startup questions never wait forever.** <a id="startup-questions-never-wait-forever"></a>On the console, the restore-point offer and the "revert pending changes?" question go through `AskPerson` (`main.cpp:47-53`). It asks only when stdin is a console whose window is visible and the run is not `--once`, which never asks (`ClassifyStdin` in [`windows/console_prompt.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/console_prompt.cpp); the rule is `CanAskPerson` in [`session/startup_prompts.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/startup_prompts.cpp)), and reads the line key by key with a 30 s limit. The limit and the key rules (Enter, Backspace, a line capped at 256 characters) are `ReadLineWithDeadline` in the same session file, which takes the console as a `KeyInput` port and a clock, so the host tests run it with scripted keys and a fake clock; `ConsoleKeyInput` is the real console. When the time runs out, the input ends or a read fails, keys still waiting in the console input are flushed (`FlushConsoleInputBuffer`), so a late half answer does not reach the next question. No answer means the safe default: nothing is reverted, no restore point is created, and the journal is not marked as asked, so the offer comes back on the next start with someone in front. Before 2026-11-22 the question waited on `std::getline` with no limit, and a hidden console still counted as interactive: the daemon started by the installer's logon task or by `VegaFlow-Start` with no journal hung before creating `Global\VegaFlowStop`. Windows Terminal (1.25, measured on the test laptop) reports its window as visible, so the question is asked there too. The cost: until someone answers, each start from the installer's logon task shows the question for 30 s before the loop begins, because the journal is not marked.
- **The entry point only assembles.** Rules and Windows calls live in the layers below, as set out in [ADR 0001](../adr/0001-nucleo-que-decide-y-puertos.md).
- **Alternatives discarded:** pendiente de confirmar. The code and the docs do not record which options were discarded for the entry point, the flag parser or the single-instance lock.

## What happens when it fails

These rows come from reading the code. Only the rows listed in the evidence section were run.

| Situation | What the code does | What you see |
| --- | --- | --- |
| `--dry-run` with `--monitor` | Since the 2026-11-20 fix, `--dry-run` wins: it simulates once and exits (checked by `tests/package/package_tests.ps1`). Before, the monitor ran the real tick. | A simulation, never a live monitor. |
| A mistyped or unknown option (`--dryrun`) | Since the 2026-11-20 fix, `ParseCommandLine` prints `[ERROR] Unknown option or missing value` and exits with code 2. Before, it was ignored and the real daemon started. | The program stops; nothing changes. |
| `--dxvk-install` or `--dxvk-remove` with no path, or followed by another option | Since the 2026-11-20 fix, the next option is not taken as the path: the same error and exit code 2. A dry run of either now checks what the real run checks: it fails, instead of reporting success, when the game folder does not exist or may not be changed (anti-cheat or outside the allowed folders). | The program stops; nothing changes. |
| `--elevate --dry-run` or `--elevate --observe` when already running as Administrator | Since the 2026-11-20 fix, the simulation or the observing run goes on (`cli.cpp:179-188`). Before, the program printed that it was already elevated and exited with 0, doing nothing. | The simulation text, or VegaFlow observing. |
| A path with "ñ" or an accent (`--dxvk-install "D:\Juegos\Año"`) | Since the 2026-11-20 fix, the arguments are read as UTF-8 (`main.cpp:72-79`). Before, Windows passed "ñ" as the single ANSI byte 0xF1 and the path arrived broken. | The path as typed. |
| `--dry-run` with `--gui` | The simulation runs and returns before the window is created. | The simulation text, and no window. |
| UAC refused on the automatic restart | `RelaunchAsAdmin` returns false and the process keeps running without Administrator rights (`cli.cpp:163-165`). | The loop prints that it runs without Administrator rights and that the standby purge needs admin (`daemon_loop.cpp:109-110`). |
| Settings folder writable by ordinary accounts | Factory values are used and nothing is saved (`main.cpp:90-92`). | The first line of the example above. |
| Last run did not end cleanly | The window shows a Yes/No box; a visible console asks `[S/n]` for up to 30 s. Answering no, or no answer, keeps the changes and prints "Puedes recuperarlos luego con --restore-registry" (`main.cpp:134`, `main.cpp:162`). A corrupt journal only prints a warning, asks nothing, and VegaFlow still starts (`main.cpp:130`). | The question, or the warning text in the window's status bar. |
| Unattended start: the installer's logon task, `VegaFlow-Start`, a pipe or a file as input | No startup question blocks. A hidden console, a pipe or a file is never asked; a visible console with nobody in front waits 30 s and then goes on with "no". Measured on the test laptop on 2026-11-22 with `vfdev first-start`: hidden console, stop event after 0.5 s (before the fix: no event after 45 s); the installer's task, visible console, after 30.4 s (before: none after 45 s). | The loop starts; the journal keeps `restore_point_prompted=0`, so the next attended start offers the restore point again. |
| Two unclean ends in a row | Safe mode: the loop, the monitor and the window skip every tick and change nothing automatically (`daemon_loop.cpp:51-53` and `:124`, `monitor.cpp:45-46` and `:51`, `gui_mode.cpp:137`). | "[WARN] MODO SEGURO ACTIVO (...)" in the console, with the reason in brackets: two unclean ends, or "pedido con --observe: solo observar". |
| Monitor closed while a game is active | It saves the session report and restores the CPU sets only (`monitor.cpp:97-99`). It does not restore GameDVR, priority or power there. Whether the game-end branch undoes them is pendiente de confirmar. | Not reproduced. |
| A second copy of the window | It finds the window by class name, sends the restore message and exits with 0 (`main.cpp:101-107`). | The first window comes to the front. |
| A second copy of the loop or the monitor | It finds no window, says so and exits with 0 (`main.cpp:109`). | `[INFO] VegaFlow Universal is already running in the background (no window to show).` |
| The process crashes | The crash handler (`main.cpp:41-44`) lets Windows end the process before the clean-exit mark is written, so the next start counts one unclean end. | Not reproduced. |
| Ctrl+C, close, logoff or shutdown | The handler clears the run flag and signals the stop event (`app_state.cpp:9-24`). The loop exits and undoes its changes (`daemon_loop.cpp:139-155`). | Not reproduced. |
| An event hook cannot be registered | The loop logs `[WARN] Event hooks unavailable: polling every 1500 ms.` and keeps working like before E2.4 (`wake_loop.cpp`). | A warning line, and the start banner shows `Poll Interval : 1500 ms (events unavailable)`. |
| `--restore-registry` with a corrupt journal | The journal is skipped, the registry backups are restored directly, and the exit code is 1 (`cli.cpp:292-298` and `:312`). | A warning, then "Se devolvieron X de Y." |

## Evidence that it works this way

Only lines from the real runs are shown. Indentation is trimmed.

| Step | Command or action | Real output |
| --- | --- | --- |
| 1 and 2 | `vegaflow_daemon.exe --version` | `VegaFlow Universal 0.1.0` |
| 2 (`--status`) | `vegaflow_daemon.exe --status` (two lines, trimmed) | `Process Elevated (Admin)   : YES` and `Detected APU Tier & Guard  : Tier 2: Mid Vega UMA [Sweet-Spot Guard]` |
| 1, 2, 4 and 5 (simulation path) | `vegaflow_daemon.exe --dry-run --once` (three lines, trimmed) | `[WARN] config.json está en una carpeta donde pueden escribir cuentas sin permisos de administrador: se usan los valores de fábrica y no se guarda nada.`, `-> Background Sets to confine: 2 core IDs (would be isolated: 22 background processes; nothing changed)` and `[KPI] Daemon Memory Working Set: 2.09375 MB (< 15 MB KPI passed)` |
| 8 and 9 (safe mode in the monitor) | `vegaflow_daemon.exe --observe --monitor --once` (one line) | `[WARN] MODO SEGURO ACTIVO (pedido con --observe: solo observar): el monitor solo muestra datos, no aplica ajustes.` |
| 6 (second copy, no window) | `--observe` started hidden, then a second `vegaflow_daemon.exe --observe` | `[INFO] VegaFlow Universal is already running in the background (no window to show).`, exit code 0 |
| 1, 3 and 5 (flags) | `tests/package/package_tests.ps1` on the installed package (unknown option, `--dxvk-*` with no path, `--elevate --dry-run`, `--elevate --observe`, a "ñ" argument, monitor in safe mode, second copy) | `PACKAGE TESTS: 63 PASSED, 0 FAILED` |
| 3 (quoting read back by Windows) | Suite, `[PASS]` lines | `[PASS] Test 105: --elevate forwards each argument whole, quoted when needed` and `[PASS] Test 106: the elevated instance reads back each argument unchanged, a path with an accent included` |
| 7 and 8 (unattended first start) | `vfdev first-start` (`MODE=hidden`, then `MODE=task`, the installer's logon task), no journal, 2026-11-22 | Before the fix: `evento=ausente tras 45 s cpu_s=0.05` (hidden) and `consola_visible=True` … `evento=ausente tras 45 s` (task). After: `evento=Global\VegaFlowStop a_los_s=0.6` and `a_los_s=30.5`, both with `diario: SESSION active=0 unclean_streak=0 restore_point_prompted=0` |
| Startup question rule | Suite, `[PASS]` line | `[PASS] Test 136: startup questions ask only a visible console; no answer is the safe default (Who=1, Revert=1, RestorePoint=1)` |
| 3 (the UAC prompt itself), 7 (journal question answered by a person), restore, Ctrl+C, crash | Not reproduced: over SSH the process is already Administrator, so UAC never shows. | Not reproduced. |

Source: runs on the test laptop on 2026-11-20, with the code of this branch merged over the CLI fixes.

## Check it yourself

```text
vegaflow_daemon.exe --version
vegaflow_daemon.exe --dry-run --once
```

The first prints the version and exits. The second only simulates and changes nothing. If a game is open, its `[1/4]` section names that game instead of the background programs (`reports.cpp:379-385`).

## Related

- [Using the daemon and the command line](./usage-and-features/daemon-and-cli.md): what each flag does, from the user's side.
- [Modules](./modules/) and [Code layers](./modules/layers.md): where `app/` sits among the other folders.
- [How ui/ works](./how-it-works-ui.md) and [How session/ works](./how-it-works-session.md): how the window's buttons reach the session code.
- [System info](./modules/system-info.md): what `--system-info` reports.
- [Security](./security.md): the sandbox every file and process operation passes through.
- [Safety and tiers](./usage-and-features/safety-and-tiers.md): which changes apply on which hardware.
- [Verification status](../measurements/verification-status.md): what has been measured and what has not.
- [ADR 0001](../adr/0001-nucleo-que-decide-y-puertos.md): why the rules live below the entry point.
