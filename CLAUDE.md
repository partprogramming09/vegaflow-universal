# VegaFlow Universal

Daemon de Windows en C++20 con núcleo híbrido en Rust (`rust/vegaflow-core`) que optimiza juegos en APUs AMD (banco de pruebas: Ryzen 5 4600H + Vega 6). Aislamiento de procesos de fondo, perfiles por nivel de APU (`ApuCapabilityGuard`), DXVK-GPLAsync y telemetría de frametimes, sin inyectar nada en juegos con anti-cheat.

## Dónde está cada cosa

- `docs/arquitectura/`: cómo funciona (hardware APU, SandboxGuard, núcleo híbrido, diseños); léelo antes de cambiar una optimización. `.claude/PRUEBAS.md`, sección "Estado de verificación": lo medido y lo no medido. `.claude/RUNBOOK.md`, sección "Compilar, enlazar y probar": compilación manual. [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"] + [claude 2026-11-08 · pedido: "Dale y quita el .Claude de el git u otros as cosas que el usuario no necesita o es basura"]
- `.claude/ESTADO.md`: lo hecho, lo abierto y lo que sigue. `docs/plan/universal-amd-intel-egpu.md`: plan para AMD, Intel, dGPU y eGPU. `.claude/ROUTING.md`: dónde vive cada tipo de información. `docs/mediciones/`: mediciones fechadas; `docs/README.md`: entrada de `docs/` con la tabla "¿Qué quieres hacer?" (arquitectura, mediciones, plan, guías); `scripts/README.md`: qué hace cada script. [claude 2026-11-08 · pedido: "Quita plan .md por qué lo único que hace es acumular planes… elimina el Docs de .Claude"] + [claude 2026-11-08 · pedido: "organiza el doc que tenemos que está desorganizado y algunos archivos que debería ser uno solo"] + [claude 2026-11-08 · pedido: "Dale y quita el .Claude de el git u otros as cosas que el usuario no necesita o es basura"] + [claude 2026-11-08 · pedido: "No me gusta la estructura de archivos se está volviendo grande y no es claro que hace da archivo ni como uno guiar parece que todo estuviera revuelto"]
- `src/` por función, con cada `.hpp` junto a su `.cpp`: `app/`, `detect/`, `optimize/` (con `graphics/`, `steam/` y `fsr/` partidos por tema), `safety/`, `ui/`, `windows/`, `bridge/`. `tests/test_main.cpp`: la suite (33 pruebas C++; las de Rust están en `rust/vegaflow-core` y corren también en el Mac con `cargo test`).
- `scripts/`: `build.ps1`, `bench.ps1` (PresentMon), `ab_loop_task.ps1` (A/B intercalado), `tweaks_state.ps1`, `remote/` (trabajos remotos con WinBridge).

## Compilar y probar

Solo en Windows (MinGW-w64 g++ 13+ y cargo con el objetivo `x86_64-pc-windows-gnu`); desde este Mac se corre por `scripts/remote/`. En este Mac `cargo test` del núcleo Rust no enlaza (pide `ntdll`).

    powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests    # compila el daemon y las pruebas y corre las 33 de C++

La suite debe pasar entera antes de cada commit. La compilación manual está en `docs/guias/compilar-y-probar.md`, sección 5.3. [claude 2026-11-07 · pedido: "un lcaude mejor"] + [claude 2026-11-08 · pedido: "organizar los archivos y tenga una buean distrbucion de como daber dividi usu temas y doc"] + [claude 2026-11-08 · pedido: "quita esas estrcuturas ques e hieron en cada una de los .claude y dejarlo normal"] + [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"]

## Reglas del proyecto

- Toda función que modifica el sistema (registro, archivos, DLLs, planes de energía) lleva un parámetro `dry_run` para probarla sin efecto real. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Antes de escribir un `.ini`, `.config` o DLL, crea su copia `.vegaflow.bak` o `.original.bak`; los cambios de registro guardan el valor original en `SOFTWARE\VegaFlow\RegBackup` y `vegaflow_daemon.exe --restore-registry` los devuelve todos. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Nunca se inyectan DLLs ni se modifican archivos de juegos con anti-cheat; toda operación de disco y procesos pasa por `SandboxGuard`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Sin frameworks pesados (Qt, wxWidgets, Boost): la GUI es Win32/GDI y el daemon en reposo debe quedar bajo 15 MB. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Rutas con `std::wstring` hacia Win32; convierte con `Utf8ToWide` y `WideToUtf8`. En Windows en español usa SID en `icacls` y `WindowsBuiltInRole::Administrator`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Medir con `scripts/bench.ps1` y A/B intercalado ABBA, con el juego abierto en el escritorio (sesión 1, no por SSH). Una optimización sin medición propia se documenta como no medida en `.claude/PRUEBAS.md`, sección "Estado de verificación". [claude 2026-11-07 · pedido: "un lcaude mejor"] + [claude 2026-11-08 · pedido: "organizar los archivos y tenga una buean distrbucion de como daber dividi usu temas y doc"] + [claude 2026-11-08 · pedido: "quita esas estrcuturas ques e hieron en cada una de los .claude y dejarlo normal"] + [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"]
- La suite ejecuta cambios reales en registro y plan de energía y un guardián final los restaura: no la interrumpas a mitad. [claude 2026-11-07 · pedido: "un lcaude mejor"]
