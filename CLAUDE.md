# VegaFlow Universal

Daemon de Windows en C++20 con núcleo híbrido en Rust (`rust/vegaflow-core`) que optimiza juegos en APUs AMD (banco de pruebas: Ryzen 5 4600H + Vega 6). Aislamiento de procesos de fondo, perfiles por nivel de APU (`ApuCapabilityGuard`), DXVK-GPLAsync y telemetría de frametimes, sin inyectar nada en juegos con anti-cheat.

## Dónde está cada cosa

- `docs/guides/`: cómo funciona (hardware APU, SandboxGuard, núcleo híbrido, módulos) y cómo se usa, compila e instala, con cada diagrama en `docs/guides/img/`; léelo antes de cambiar una optimización. `docs/measurements/verification-status.md`: lo medido y lo no medido. `docs/guides/build-and-test.md`: compilación manual. El sitio es VitePress (`pnpm docs:build`). [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"] + [claude 2026-11-08 · pedido: "Dale y quita el .Claude de el git u otros as cosas que el usuario no necesita o es basura"]
- `docs/estado.md` (fuera de git, uno por worktree): lo hecho, lo abierto y lo que sigue; lo carga el hook de relevo al empezar. `docs/plans/maestro/` (fuera de git): el único plan, con un archivo por fase y un `README.md` de entrada. `docs/measurements/`: mediciones fechadas; `docs/research/` (fuera de git): lo ya investigado con fuentes oficiales (mirar antes de buscar en la web); `docs/guides/index.md`: entrada con la tabla "What do you want to do?"; `scripts/README.md`: qué hace cada script. [claude 2026-11-08 · pedido: "Quita plan .md por qué lo único que hace es acumular planes… elimina el Docs de .Claude"] + [claude 2026-11-08 · pedido: "organiza el doc que tenemos que está desorganizado y algunos archivos que debería ser uno solo"] + [claude 2026-11-08 · pedido: "Dale y quita el .Claude de el git u otros as cosas que el usuario no necesita o es basura"] + [claude 2026-11-08 · pedido: "No me gusta la estructura de archivos se está volviendo grande y no es claro que hace da archivo ni como uno guiar parece que todo estuviera revuelto"]
- `src/` por función, con cada `.hpp` junto a su `.cpp`: `app/` (entradas), `session/` (casos de uso), `model/` (tipos compartidos), `ports/` (interfaces que las pruebas falsifican: archivos y reloj), `detect/`, `optimize/` (con `graphics/` y `steam/` partidos por tema), `ui/`, `windows/` (toda llamada a Windows, también `system_counters` y `text_convert`), `bridge/`. `tests/`: la suite (425 pruebas C++ activas, `integration/` (tocan Windows, un archivo por módulo), `unit/` (con puertos falsos) y `test_main.cpp`, que las corre en orden y restaura el sistema al final; las de Rust están en `rust/vegaflow-core` y corren en el CI de GitHub).
- `scripts/`: `build/` (`build.ps1`, `build-cross.sh`, `package.sh`, que arma el zip con `install.ps1` y `uninstall.ps1` de `scripts/package/`; su prueba: `tests/package/package_tests.ps1`), `bench.ps1` (PresentMon), `ab_loop_task.ps1` (A/B intercalado), `tweaks_state.ps1`, `remote/` (trabajos remotos con WinBridge).

## Modo de los docs

Tipo: abierto

Terceros: repo público; la documentación publicada (`README.md`, `docs/guides/`, `docs/measurements/verification-status.md`, ADR) va en inglés, una sola versión. Los informes fechados de `docs/measurements/` se quedan en español; planes, investigación y `.claude/` también (no se publican).

## Compilar y probar

Solo en Windows (MinGW-w64 g++ 13+ y cargo con el objetivo `x86_64-pc-windows-gnu`); desde este Mac se corre por `scripts/remote/`. En este Mac `cargo test` del núcleo Rust también corre: el núcleo no llama a Windows (las llamadas a `ntdll` y DXGI viven en C++, `src/windows/memory.cpp`).

    powershell -ExecutionPolicy Bypass -File .\scripts\build\build.ps1 -RunTests    # compila el daemon y las pruebas y corre las 425 de C++

La suite debe pasar entera antes de cada commit. La compilación manual está en `docs/guides/build-and-test.md`, sección 5.3. [claude 2026-11-07 · pedido: "un lcaude mejor"] + [claude 2026-11-08 · pedido: "organizar los archivos y tenga una buean distrbucion de como daber dividi usu temas y doc"] + [claude 2026-11-08 · pedido: "quita esas estrcuturas ques e hieron en cada una de los .claude y dejarlo normal"] + [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"]

## Reglas del proyecto

- Toda función que modifica el sistema (registro, archivos, DLLs, planes de energía) lleva un parámetro `dry_run` para probarla sin efecto real. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Antes de escribir un `.ini`, `.config` o DLL, crea su copia `.vegaflow.bak` o `.original.bak`; los cambios de registro guardan el valor original en `SOFTWARE\VegaFlow\RegBackup` y `vegaflow_daemon.exe --restore-registry` los devuelve todos. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Nunca se inyectan DLLs ni se modifican archivos de juegos con anti-cheat; toda operación de disco y procesos pasa por `SandboxGuard`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Sin frameworks pesados (Qt, wxWidgets, Boost): la GUI es Win32/GDI y el daemon en reposo debe quedar bajo 15 MB. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Rutas con `std::wstring` hacia Win32; convierte con `Utf8ToWide` y `WideToUtf8`. En Windows en español usa SID en `icacls` y `WindowsBuiltInRole::Administrator`. [claude 2026-11-07 · pedido: "un lcaude mejor"]
- Medir con `scripts/bench.ps1` y A/B intercalado ABBA, con el juego abierto en el escritorio (sesión 1, no por SSH). Una optimización sin medición propia se documenta como no medida en `docs/measurements/verification-status.md`. [claude 2026-11-07 · pedido: "un lcaude mejor"] + [claude 2026-11-08 · pedido: "organizar los archivos y tenga una buean distrbucion de como daber dividi usu temas y doc"] + [claude 2026-11-08 · pedido: "quita esas estrcuturas ques e hieron en cada una de los .claude y dejarlo normal"] + [claude 2026-11-08 · pedido: "elimina el Docs de .Claude ya que así si tengo uno en la raíz no sabe de cuál hablo"]
- La suite ejecuta cambios reales en registro y plan de energía y un guardián final los restaura: no la interrumpas a mitad. [claude 2026-11-07 · pedido: "un lcaude mejor"]
