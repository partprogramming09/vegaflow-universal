# VegaFlow Universal

Daemon de Windows en C++20 con núcleo híbrido en Rust (`rust/vegaflow-core`) que optimiza juegos en APUs AMD (banco de pruebas: Ryzen 5 4600H + Vega 6). Aislamiento de procesos de fondo, perfiles por nivel de APU (`ApuCapabilityGuard`), DXVK-GPLAsync y telemetría de frametimes, sin inyectar nada en juegos con anti-cheat.

## Dónde está cada cosa

- `docs/GUIA_ARQUITECTURA.md`: la guía completa (hardware APU, SandboxGuard, módulos, compilación manual, suite, hoja de ruta y la sección 9 con lo medido y lo no medido). Léela antes de cambiar una optimización.
- `docs/ROADMAP_FOCUS.md`: enfoque actual. `docs/MEDICION_BASE.md` y `docs/MEDICION_AB_DAEMON.md`: mediciones.
- `include/` y `src/` en cuatro subdominios: `core/`, `optimizers/`, `security/`, `gui/`. `tests/test_main.cpp`: la suite (31 pruebas C++ más 15 de Rust).
- `scripts/`: `build.ps1`, `bench.ps1` (PresentMon), `ab_loop_task.ps1` (A/B intercalado), `tweaks_state.ps1`, `remote/` (trabajos remotos con WinBridge).

## Compilar y probar

Solo en Windows (MinGW-w64 g++ 13+ y cargo con el objetivo `x86_64-pc-windows-gnu`); desde este Mac se corre por `scripts/remote/`. En este Mac `cargo test` del núcleo Rust no enlaza (pide `ntdll`).

    powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests    # compila el daemon y las pruebas y corre las 31 de C++

La suite debe pasar entera antes de cada commit. La compilación manual está en la guía, sección 5.3. [claude 2026-11-07 · pedido: "un lcaude mejor"]

## Reglas del proyecto

- Toda función que modifica el sistema (registro, archivos, DLLs, planes de energía) lleva un parámetro `dry_run` para probarla sin efecto real. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Antes de escribir un `.ini`, `.config` o DLL, crea su copia `.vegaflow.bak` o `.original.bak`; los cambios de registro guardan el valor original en `SOFTWARE\VegaFlow\RegBackup` y `vegaflow_daemon.exe --restore-registry` los devuelve todos. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Nunca se inyectan DLLs ni se modifican archivos de juegos con anti-cheat; toda operación de disco y procesos pasa por `SandboxGuard`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Sin frameworks pesados (Qt, wxWidgets, Boost): la GUI es Win32/GDI y el daemon en reposo debe quedar bajo 15 MB. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Rutas con `std::wstring` hacia Win32; convierte con `Utf8ToWide` y `WideToUtf8`. En Windows en español usa SID en `icacls` y `WindowsBuiltInRole::Administrator`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Medir con `scripts/bench.ps1` y A/B intercalado ABBA, con el juego abierto en el escritorio (sesión 1, no por SSH). Una optimización sin medición propia se documenta como no medida en la sección 9 de la guía. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- La suite ejecuta cambios reales en registro y plan de energía y un guardián final los restaura: no la interrumpas a mitad. [claude 2026-11-07 · pedido: "un lcaude mejor"]
