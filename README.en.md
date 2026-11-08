# VegaFlow Universal

[Español](README.md) · **English**

A Windows daemon written in C++20, with a hybrid Rust core, that optimizes games on AMD APUs (test bench: Ryzen 5 4600H with Radeon Vega 6). It isolates background processes, applies profiles according to the APU tier, uses DXVK-GPLAsync and records frametimes. It never injects anything into games that use anti-cheat.

![Language](https://img.shields.io/badge/C%2B%2B20%20%2B%20Rust-blue.svg) ![Platform](https://img.shields.io/badge/Windows%2010%20%7C%2011%20x64-0078D6.svg) ![Tests](https://img.shields.io/badge/tests-31%20C%2B%2B%20%7C%2025%20Rust-success.svg)

> The detailed documentation under `docs/` is written in Spanish for now.

## Why it exists

On an APU the CPU and the iGPU share the RAM and the thermal budget, and the system is designed for desktops with a dedicated GPU. VegaFlow acts on five problems:

1. **Shared memory (UMA):** background processes compete with the iGPU for the same DDR4 bus.
2. **The false 512 MB VRAM limit:** BIOSes report it and game engines take it literally.
3. **No GPL in AMD's Vega drivers:** shaders compile on the game's own thread; DXVK-GPLAsync moves them to background threads.
4. **Windowed DWM composition:** the intermediate copy adds latency; Flip mode avoids it.
5. **Shared thermal budget (STAPM):** if the CPU draws too much, the GPU lowers its clock.

## What it does

- **Background isolation:** confines background processes (affinity, EcoQoS, I/O priority) to leave the CPU and the disk to the game.
- **Per-tier profiles:** `ApuCapabilityGuard` classifies the machine into 4 levels and never applies what was calibrated for one to another.
- **Anti-cheat:** if it detects an anti-cheat driver, `SandboxGuard` blocks every write and does not touch the game's files.
- **Reversible:** every change keeps a copy (`.vegaflow.bak`, or the original registry value); `--restore-registry` brings them all back.
- **Lightweight:** no heavy frameworks; Win32/GDI GUI and under 15 MB at rest.

Each feature is described in [`docs/guias/uso-y-funciones.md`](docs/guias/uso-y-funciones.md) (Spanish).

## Measured results

The figures come from PresentMon, with their method and limits, in [`docs/mediciones/`](docs/mediciones/); what is still unmeasured is in [`estado-de-verificacion.md`](docs/mediciones/estado-de-verificacion.md). In short: in Octopath Traveler II the UMA preset in `Engine.ini` gave +40% average FPS in the overworld (one non-interleaved capture, cause not isolated); in Zenless Zone Zero the daemon did not change the FPS (58.57 to 58.89, no effect) and there was no anti-cheat ban.

## Build and use

The program runs on Windows only. It is built with MinGW-w64 g++ 13+ and cargo with the `x86_64-pc-windows-gnu` target:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```

From a Mac or Linux you can cross-compile (`scripts/build-cross.sh`, needs mingw-w64) and test the `.exe` files on a Windows machine. The rules written in Rust can be tested on any system: `cargo test --manifest-path rust/vegaflow-core/Cargo.toml`.

Guides: [build and test](docs/guias/compilar-y-probar.md) and [using the daemon](docs/guias/uso-y-funciones.md) (Spanish).

## Repository layout

```
src/                C++20 daemon code, grouped by function (each .hpp next to its .cpp)
  app/                startup, command line and modes (daemon, window, monitor)
  detect/             hardware, processes and SystemInfo
  optimize/           the levers: graphics, memory, CPU, DXVK, Steam engines
  safety/             SandboxGuard and frame telemetry
  ui/                 Win32/GDI window
  windows/            native Windows calls and privileges
  bridge/             contract between C++ and Rust
rust/vegaflow-core/ pure rules in Rust (UMA pressure, topology, confinement, tiers, path policy, signatures)
tests/              the 31 C++ tests and the cases shared with Rust (tests/vectors/)
scripts/            build, measure and operate the test laptop (see scripts/README.md)
assets/dxvk/        DXVK DLLs the daemon installs into games
config/             daemon defaults
docs/               all the documentation (see docs/README.md)
```

## Documentation

Start with [`docs/README.md`](docs/README.md): a "What do you want to do?" table points you to the document you need. What is still to be done is in [`docs/plan/`](docs/plan/).

## Safety

DLLs are never injected and the files of games with anti-cheat are never modified. Every disk and process operation goes through `SandboxGuard`, and every function that changes the system accepts `dry_run`.

## License

Free software under the **GNU GPL v3 or later** (see [`LICENSE`](LICENSE)): you may use, study, modify and share it. If you distribute a modified version, you must also publish its source code under the same license.
