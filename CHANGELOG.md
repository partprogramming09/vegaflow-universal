# Changelog

All notable changes to VegaFlow Universal. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

## [0.2.0] - Unreleased

New native GUI (Win32, owner-draw controls reachable with Tab). Not tagged yet: the version number in the binary and
`Cargo.toml` still reads 0.1.0 until the release step.

### Added
- Screens: Inicio, Juegos, Programas, Tu equipo, Configuración, Requisitos, plus Ajustes del sistema and Historial.
  Plain-language text; every tweak shows its evidence status and whether it is measured.
- Juegos: per-game "Aplicar perfil", "Volver al archivo original", "Me fue mal en este juego" (restores the files and
  excludes the game), "Volver a incluir" and DXVK install/remove; anti-cheat games are never touched.
- Programas and Configuración are saved to `config.json` and read back (round trip tested).
- Per-row undo in Historial and a "Devolver todo a como estaba" button.
- `dxvk.conf.vegaflow.bak`: the game's own `dxvk.conf` is copied before DXVK is deployed and put back on removal.
- `session/` layer: pure screen models and the action objects behind the buttons, tested with in-memory fakes of the
  file system, registry, power plan and clock. `scripts/check_layers.sh` enforces that `ui` includes only `session`
  and `model`.
- WCAG contrast test of the palette; adversarial tests for every new screen and action object.
- `--observe` with `--gui`: opens the window without automatic changes. Remote jobs to open, drive and close the
  test GUI.

### Changed
- "Me fue mal en este juego" saves the configuration before touching any game file; a refused save leaves the game as it was.
- `IsDxvkInstalled` needs the VegaFlow marker in `dxvk.conf`: a game that ships its own `d3d11.dll` and `dxgi.dll`
  is no longer reported as having DXVK.
- The UMA guard is shown as active only outside safe mode.
- Memory percent on Inicio is computed in 128 bits; path comparisons fold case the Windows way.

### Fixed
- Turning VRAM reporting off no longer writes an invented 512 MB: it puts back only the saved original.
- DXVK: a failed backup of the game's own `d3d11.dll`/`dxgi.dll` now stops the install instead of overwriting them; a
  failed copy undoes the half install; removal keeps the `.original.bak` until the original is back and says so when the
  game (open, with the DLLs loaded) blocks it; a game's own DLLs with no VegaFlow marker are never deleted, also from
  `--dxvk-remove`.

### Removed
- Unreachable handlers for the old "apply preset" and "elevate" buttons (a posted `WM_COMMAND` could have reached
  them) and the unused in-memory audit list.

### Known limits
- No DPI scaling; 125 % and 150 % were not checked with screenshots. Screen readers were not tested.
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
