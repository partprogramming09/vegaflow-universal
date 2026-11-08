# scripts/

Solo Windows (PowerShell) salvo `remote/`, que corre en la Mac. Ninguno forma parte del producto: son para compilar, medir y probar.

## Compilar

| Script | Qué hace |
| --- | --- |
| `build.ps1` | Compila el daemon y la suite de pruebas; con `-RunTests` corre las 31 |
| `build-cross.sh` | Lo mismo desde un Mac o Linux con mingw-w64 (compilación cruzada); los `.exe` se prueban luego en Windows |

## Medir (PresentMon y A/B)

| Script | Qué hace |
| --- | --- |
| `bench.ps1` | Mide FPS medio, 1% Low y 0.1% Low de un proceso y guarda JSON |
| `bench_pack.ps1` | Mide una escena con el protocolo y empaqueta resultados con la huella del equipo |
| `ab_loop.ps1` | A/B intercalado (ABBA) de un juego ya abierto, daemon apagado contra encendido |
| `ab_loop_task.ps1` | El mismo A/B con el daemon en la sesión del escritorio (tareas `VegaFlow-Start` y `-Stop`) |
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

## Por qué la carpeta es plana

Los scripts no se agrupan en subcarpetas a propósito. Se buscan entre sí con `$PSScriptRoot`, y existe una copia plana instalada en `C:\ProgramData\VegaFlow` (la que usan las tareas programadas y `scripts/remote/`), así que cada script debe funcionar con las dos disposiciones. Moverlos exige reescribir esas rutas y volver a desplegar como administrador, y no se puede comprobar sin un juego abierto. Por eso el orden por tema vive en las tablas de arriba y no en carpetas.

## `remote/`: operar el laptop desde la Mac

Herramienta de desarrollo con `vfdev`, los trabajos en `jobs/`, las referencias en `baselines/` y el paquete de WinBridge. Su guía completa está en [`remote/README.md`](remote/README.md).
