# Changelog

All notable changes to VegaFlow Universal. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

## [0.2.0] - Unreleased

New native GUI (Win32, owner-draw controls reachable with Tab). Not tagged or published yet: the release needs the
owner's approval.

### Changed
- The window hidden in the tray sleeps on events instead of polling (F4.1): with no game and the window hidden, nothing wakes the process until a foreground, device or power event, or the 30 s safety net.
- Dialogs follow the dark theme (F4.x), including the question about a broken `config.json`.
- A `config.json` that cannot be read is now the person's decision: "Valores de fábrica" (saves a copy of the broken file first) or "Abrir el archivo" (F4.4); before, the daemon went on silently with factory values.
- `session/` is a strict layer (F4.3): it includes nothing from `detect/`, `optimize/` or `windows/`, and `scripts/ci/check_layers.sh` has no exception list. The Windows reads behind `--doctor`, `SystemInfo` and the Ajustes switches, and the game-file buttons, go through ports (`DoctorProbe`, `SystemReads`, `TweakControls`, `GameFiles`); `run_cycle` and the `--status`, `--purge-now` and `--dry-run` commands moved to `app/`. No behaviour change; host tests went from 134 to 268.
- The Unreal Engine `Engine.ini` preset is off unless `config.json` sets `auto_engine_presets` to `true`; it also has an experimental row in Ajustes.
- `--doctor` and `--status` say why the profile is what it is (`VfDecisionReason` mask, one line per rule that fired).
- `--vram-override` and `--observe` help text now describe what the code does.
- No tier caps the CPU frequency any more (`cpu_stapm_cap_mhz = 0`, Smart Uncapped Boost); the old 3000/3400 MHz caps in the docs were removed. Not A/B measured: see `docs/mediciones/estado-de-verificacion.md`.

### Added
- "Copiar informe" in the tray menu (F4.1): the same plain-text report as on Inicio, with the Windows user name replaced by <usuario>.
- Memoria tab drawn natively (F4.2): reachable with the keyboard (one Tab stop for the thread map, arrows to move) and a side menu that screen readers and Tab can reach.
- Screens: Inicio, Juegos, Programas, Tu equipo, Configuración, Requisitos, plus Ajustes del sistema and Historial.
  Plain-language text; every tweak shows its evidence status and whether it is measured.
- Juegos: per-game "Aplicar perfil", "Volver al archivo original", "Me fue mal en este juego" (restores the files and
  excludes the game), "Volver a incluir" and DXVK install/remove; anti-cheat games are never touched.
- Programas and Configuración are saved to `config.json` and read back (round trip tested).
- Per-row undo in Historial and a "Devolver todo a como estaba" button.
- Inicio card "¿Algo va mal?": "Devolver todo a como estaba" and "Copiar informe" (plain text on the clipboard: version, machine, memory, the last 30 changes with before and after; the Windows user name in any path is replaced by <usuario>).
- `dxvk.conf.vegaflow.bak`: the game's own `dxvk.conf` is copied before DXVK is deployed and put back on removal.
- `session/` layer: pure screen models and the action objects behind the buttons, tested with in-memory fakes of the
  file system, registry, power plan and clock. `scripts/ci/check_layers.sh` enforces that `ui` includes only `session`
  and `model`.
- WCAG contrast test of the palette; adversarial tests for every new screen and action object.
- `--observe` with `--gui`: opens the window without automatic changes. Remote jobs to open, drive and close the
  test GUI.

### Changed
- The window asks Windows for "DPI unaware, GDI scaled" instead of per-monitor awareness (it was tiny at 125 %) and
  fits the work area (at 150 % on a 1080p screen its status bar hid under the taskbar).
- `WM_COMMAND` is no longer let through the UIPI filter, so a lower-integrity process cannot click the elevated window's
  buttons; a second launch brings the window forward with a registered message instead.
- "Me fue mal en este juego" saves the configuration before touching any game file; a refused save leaves the game as it was.
- `IsDxvkInstalled` needs the VegaFlow marker in `dxvk.conf`: a game that ships its own `d3d11.dll` and `dxgi.dll`
  is no longer reported as having DXVK.
- The UMA guard is shown as active only outside safe mode.
- Memory percent on Inicio is computed in 128 bits; path comparisons fold case the Windows way.

### Fixed
- Safe mode is now honoured by `--monitor` too: before, `--observe --monitor` ran the real cycle. The safe-mode notice says why it is on (`--observe` or two unclean exits) instead of always blaming crashes, and the GUI sidebar shows "Modo seguro: sin cambios" instead of "Auto-Suspensión: ACTIVA". The dry-run `[KPI]` line now says FAILED at 15 MB or more instead of always "passed", the dry run says how many processes *would be* isolated, and a second start with no window to focus says so. Checked by C++ Test 104, the package test (60 passed) and a screenshot of the GUI in safe mode.
- Registry backup records were named with `std::hash`, which a different compiler or build may compute differently; VegaFlow would then miss the first backup and save a value it had already changed as the "original". Records now use a fixed FNV-1a name, and records written by older builds are still found when backing up and restoring. C++ Tests 89 and 103.
- Restoring a game or a background process set its I/O priority to 1, which Windows reads as Low (IO_PRIORITY_HINT: 0 VeryLow, 1 Low, 2 Normal), so every program VegaFlow had confined kept a low disk priority. It now goes back to Normal (2); crash recovery too. C++ Test 102 checks it on Windows.
- `--dry-run` with `--monitor` now only simulates; before, the monitor ran the real cycle. An unknown or mistyped option (`--dryrun`) now stops with exit code 2 instead of being ignored and starting the real daemon. Checked by `tests/package/package_tests.ps1` (52 passed on the test laptop, 2026-11-20).
- `--dxvk-install` and `--dxvk-remove` no longer take the next option as their path (`--dxvk-install --dry-run` used `--dry-run` as the folder and dropped the simulation); a missing path exits with code 2. A dry run of either now fails, instead of reporting success, when the game folder does not exist or may not be changed (anti-cheat or outside the allowed folders), as the real run would. `--elevate --dry-run` and `--elevate --observe` on an elevated process now simulate or observe instead of exiting, and `--elevate` hands every argument to the elevated instance whole, quoted by the Windows rules (a path ending in `\`, an empty value or one with a quote inside used to be split or merged; C++ Test 105). Checked by the package test (56 passed) and C++ Tests 16 and 102 (102 now also covers crash recovery).
- A command-line path with "ñ" or an accent (`--dxvk-install "D:\Juegos\Año"`) arrived broken: Windows hands `main` its arguments in the ANSI code page (1252 on the test laptop) and VegaFlow read them as UTF-8. The arguments are now read from `GetCommandLineW`. C++ Test 106, which also checks that Windows reads back every argument `--elevate` forwards.
- At start (not in `--observe` or safe mode), values that still hold the exact 0.1.x string
  `SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;` lose its GPU and HDR part and keep the Flip switch; each
  change is journaled and can be undone from Historial.
- Listing a registry key's string values no longer stops at the first value longer than the buffer; that value is
  skipped and the rest are still read.
- Turning VRAM reporting off no longer writes an invented 512 MB: it puts back only the saved original.
- DXVK: a failed backup of the game's own `d3d11.dll`/`dxgi.dll` now stops the install instead of overwriting them; a
  failed copy undoes the half install; removal keeps the `.original.bak` until the original is back and says so when the
  game (open, with the DLLs loaded) blocks it; a game's own DLLs with no VegaFlow marker are never deleted, also from
  `--dxvk-remove`.

- Windowed-game Flip upgrade (E1.4): writes only `SwapEffectUpgradeEnable=1` into the game's `UserGpuPreferences`, so
  the user's `GpuPreference` and `AutoHDREnable` stay as they were (it used to force high-performance GPU and HDR off);
  `HIGHDPIAWARE` is appended to the game's `AppCompatFlags\Layers` instead of replacing its other flags, and skipped when
  another DPI mode is set; applying it again writes nothing and adds no second journal entry.
- Game Bar capture off no longer turns Fullscreen Optimizations off. The four `GameDVR_*FSE*` values are now a hidden,
  unmeasured compatibility row in Ajustes ("apagar las optimizaciones de pantalla completa", for old DX9/DX10 games) that
  the daemon never applies by itself; turning it off puts back only the saved originals.

### Removed
- Unreachable handlers for the old "apply preset" and "elevate" buttons (a posted `WM_COMMAND` could have reached
  them) and the unused in-memory audit list.
- `ExcludeGameAndRevert`, which nothing called: the GUI excludes a game through the Games screen and the next cycle
  restores it. C++ Test 55 now checks that path.
- `ApplyDirectFlipUpgrade` (`AutoFlipModel=1;DirectFlip=1`, keys with no official source) and
  `RemoveFlipModelUpgrade`, which deleted the whole per-game values; neither had a caller outside the tests. Undo goes
  through the change journal, which puts back the exact previous values.

### Known limits
- The window does no DPI scaling of its own (fixed pixels): Windows enlarges it. Checked with screenshots at 125 % and 150 % (not 175 %). Screen readers were not tested.
- Some actions still live in `gui_mode.cpp` (telemetry poll, purge, isolate, restore CPU affinity, history poll): they
  call Windows classes that have no fakes.
- Tab 2 (Memoria UMA & Núcleos CPU) is still the old GDI view.
- "Devolver todo a como estaba" writes Windows defaults without a prior copy for several settings.
- The UMA guard reads a per-process DXGI counter, so it may never see a game's memory use.
- Not built yet: pause VegaFlow per game, welcome screen, panic hotkey, controller input, dry-run preview, copyable
  report, exit warning. Configuración saves at once, but the daemon reads interval and memory limits at start.
- See `docs/mediciones/estado-de-verificacion.md` for what is measured and what is not.

## [0.1.0] - 2026-11-14

First packaged release. Windows x64 only; the binary is not code-signed.

### Added
- Windows daemon (C++20) with a Rust core (`rust/vegaflow-core`) that makes the decisions behind injectable ports.
- Universal device profile: CPU, GPU with its connection type (integrated, discrete, eGPU), RAM, storage, power,
  display, Windows and architecture, read through DXCore and SetupAPI.
- CPU topology for hybrid Intel (low-power E-cores excluded), multi-CCD AMD and more than 64 logical processors.
- Process classification with a full-path check, DX3D DLL check and generic anti-cheat signals; games with
  anti-cheat are never touched.
- One memory-purge rule and one UMA guard (threshold 0.85, shared 90 s pause).
- Game `.ini` and Unity `boot.config` editor that keeps BOM, UTF-16 and CRLF, with a `.vegaflow.bak` copy verified
  byte by byte before anything is written.
- Atomic change journal (`config\change_journal.vfj`): safe mode after two abnormal exits, recovery with
  `--restore-registry`, optional System Restore point.
- File access limited to an allow-list of folders (junctions and 8.3 names resolved, checked by handle).
- `config.json` next to the executable, read only if no other account can write that folder.
- Package with `install.ps1` and `uninstall.ps1` (both with `-DryRun`), SHA-256 checks and an Add/Remove Programs
  entry. `vegaflow_daemon.exe --version`.

### Removed
- ETW frame telemetry and every FPS field: VegaFlow is not a frame-time meter.
- The C++ copy of the CPU Sets split, the 0.5 ms NT timer tweak and the MMCSS `GPU Priority` tweak.

### Changed
- The window no longer shows the test laptop's CPU or GPU on other machines, no longer uses another program's
  name for its window class and title, and its texts no longer promise gains.

### Known limits
- The new GUI (plain-language text, evidence status per tweak) is planned for 0.2.0.
- Tweaks without their own measurement are listed as not measured in `docs/mediciones/estado-de-verificacion.md`.
