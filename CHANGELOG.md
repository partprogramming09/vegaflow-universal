# Changelog

All notable changes to VegaFlow Universal. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versions follow [Semantic Versioning](https://semver.org/).

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
- The new GUI (seven screens, plain-language text, evidence status per tweak) is planned for 0.2.0.
- Tweaks without their own measurement are listed as not measured in `docs/mediciones/estado-de-verificacion.md`.
