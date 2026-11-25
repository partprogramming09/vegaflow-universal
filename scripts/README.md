# scripts/

Solo Windows (PowerShell) salvo `remote/`, que corre en la Mac. Ninguno forma parte del producto: son para compilar, medir y probar.

## Compilar: `build/`

| Script | Qué hace |
| --- | --- |
| `build/build.ps1` | Compila el daemon y la suite de pruebas; con `-RunTests` corre la suite entera |
| `build/build-cross.sh` | Lo mismo desde un Mac o Linux con mingw-w64 (compilación cruzada); los `.exe` se prueban luego en Windows |
| `build/package.sh` | Arma `dist/VegaFlow-<versión>-win-x64.zip` (daemon, `install.ps1`, `uninstall.ps1`, `SHA256SUMS`); se niega si `Cargo.toml`, `src/ui/app_identity.hpp` y el `CHANGELOG.md` no coinciden. Los scripts del paquete están en `scripts/package/`; su prueba, en `tests/package/package_tests.ps1` |

Los tres suben dos niveles hasta la raíz del repo (`cd "$(dirname "$0")/../.."`, `Split-Path` dos veces) y solo se corren desde un clon.

## CI: `ci/`

| Script | Qué hace |
| --- | --- |
| `ci/check_layers.sh` | Comprueba que cada carpeta de `src/` solo incluye las capas permitidas (tabla en el plan maestro, sección 3b); corre en el CI |
| `ci/host-unit-tests.sh` | Compila con `clang++` y corre en el propio Mac (sin Wine ni mingw) las pruebas unitarias de C++ que no necesitan Windows, contra un `windows.h` falso de solo tipos (`tests/host/`). Qué entra lo fija a mano `tests/host/allowlist.txt` (cada `tests/unit/*_tests.cpp` debe estar como `test` o `exclude` con un motivo de una palabra: `compile`, `link` o `stub`). Imprime `HOST UNIT TESTS: N PASSED, M FAILED`, la lista de excluidos y sale con 0 solo si todas pasan. No reemplaza la suite del laptop (`build.ps1 -RunTests`) |

## Medir (PresentMon y A/B)

| Script | Qué hace |
| --- | --- |
| `bench.ps1` | Mide FPS medio, 1% Low y 0.1% Low de un proceso y guarda JSON |
| `bench_pack.ps1` | Mide una escena con el protocolo y empaqueta resultados con la huella del equipo |
| `ab_loop.ps1` | A/B intercalado (ABBA) de un juego ya abierto, daemon apagado contra encendido |
| `ab_loop_task.ps1` | El mismo A/B con el daemon en la sesión del escritorio (tareas `VegaFlow-Start` y `-Stop`) |
| (log de sesión) | Desde E2.6 el daemon escribe `logs\session_reports.log` junto a su `.exe` (no en la carpeta de trabajo) y lo rota a `.log.1` al pasar de 2 MB; `ab_loop.ps1` y `ab_loop_task.ps1` lo buscan en la carpeta del `.exe` (`-WorkDir` / `-Dir`) y detectan la sesión por la hora de escritura, no por el tamaño |
| `ab_capture.ps1` | Una sola captura de una condición A/B (ningún archivo la cita; candidata a borrar) |
| `ab_profile.ps1` | Arranca o detiene el daemon como condición "aplicado" |

## Estado y ajustes de Windows

| Script | Qué hace |
| --- | --- |
| `tweaks_state.ps1` | Muestra los tweaks de registro frente al valor de fábrica |
| `setup_tasks.ps1` | Crea las dos tareas programadas elevadas que restauran y revisan los tweaks |
| `games_tweak.ps1` | Activa o desactiva el perfil MMCSS de juegos para el A/B |
| `hold_timer.ps1` | Pide una resolución de temporizador NT y la mantiene |
| `sleep_probe.ps1` | Mide cuánto dura `Sleep(1)` de verdad |

## Por qué los de medir y de estado siguen en la raíz

Los `.ps1` de medir y de estado de Windows no se mueven a una subcarpeta a propósito. Corren en dos disposiciones: el clon y una copia plana desplegada (`vfdev` y `remote/gui-install.sh` copian `bench.ps1`, `bench_pack.ps1` y `ab_loop_task.ps1` a `C:\dev\vf-pkg\scripts\`; `setup_tasks.ps1` y `remote/deploy_package.ps1` copian `tweaks_state.ps1`, `games_tweak.ps1` y `ab_profile.ps1` a `C:\ProgramData\VegaFlow`). Su ruta asume que están un nivel bajo la raíz: `bench.ps1`, `bench_pack.ps1`, `ab_loop.ps1`, `ab_loop_task.ps1` y `hold_timer.ps1` usan `Split-Path $PSScriptRoot` para `bench\results`, y `setup_tasks.ps1` busca `..\build` y `..\config`. En `scripts/bench/` el clon escribiría en `scripts/bench/results`, y cambiarlo a dos niveles rompería la copia desplegada; ninguna de las dos cosas se puede comprobar sin el laptop y un juego abierto. Por eso el orden por tema de esos scripts vive en las tablas de arriba y no en carpetas.

## `remote/`: operar el laptop desde la Mac

Herramienta de desarrollo con `vfdev`, los trabajos en `jobs/`, las referencias en `baselines/` y el paquete de WinBridge. Su guía completa está en [`remote/README.md`](remote/README.md).

`vfdev package-test [ZIP]` prueba el paquete en el laptop con el `tests/package/package_tests.ps1` de la rama actual: arma el zip con `build/package.sh` (o usa el que le pases o `VFJOB_PKG_ZIP`), lo copia con el script a `C:\ProgramData\VegaFlow\pkgtest`, corre el trabajo `package-test` (`jobs/package_test.ps1`, comprueba el SHA-256 de ambos) y trae la salida entera, con fecha, a `build/package-test/`. Sale con 0 solo si hubo `0 FAILED`. El trabajo se instala una vez con `vfdev deploy`.

`vfdev octo Conditions=full,factory,... Prefix=x [Captures=1 Seconds=120 Warmup=1 Leave=0]` mide Octopath Traveler II sin nadie en el laptop: por cada condición cierra el juego, escribe la variante de `Engine.ini` (`factory`, `streaming`, `visual`, `async`, `full` o `keep`), lo abre por Steam, carga el autoguardado, comprueba la escena (captura `dda` no negra, juego en primer plano y 15-55 FPS) y mide con `bench.ps1`. Al final cierra el juego y devuelve el `Engine.ini` original (con `Leave=1` lo deja abierto para otro trabajo, y `vfdev run octo-close` lo cierra y lo devuelve). Trae las capturas de verificación a `build/octo/`. Detalle en [`remote/README.md`](remote/README.md).

`vfdev run gui-mem [Wait=N]` lee, sin tocar nada, el working set y el working set privado (MB) de la GUI de prueba que abrió `vfdev run gui` (espera N segundos antes de leer; falla con un mensaje si no hay GUI abierta). `vfdev screen NAME [DEST]` trae al Mac el PNG que dejó `vfdev run screenshot Name=NAME` (por `scp`, con la clave de `.env`; `NAME` solo letras, dígitos, punto, guion y guion bajo) para abrirlo con la herramienta de lectura de imágenes.
