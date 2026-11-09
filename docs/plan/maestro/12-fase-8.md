# Fase 8: ajustes de sistema por fabricante

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** `GraphicsOptimizer` tiene 36 funciones estáticas repartidas en 11 archivos, y es el bloque más grande que queda sin puertos. Las funciones del driver de AMD comprueban el fabricante cada una a su manera: unas por el texto del nombre del driver (`amd`, `radeon`) y otras por el número de fabricante (`0x1002`). Es la misma pregunta repetida con respuestas que pueden no coincidir. Con la fase 3 ya hay un único dato de fabricante.

**Qué se hace:** las 36 funciones se agrupan en **familias**, cada una con un objeto que recibe el puerto de registro (`ports/registry.hpp`, que guarda el valor original en `SOFTWARE\VegaFlow\RegBackup` como hoy). La familia de energía usa además un puerto de energía (`ports/power.hpp`), porque el plan de energía se cambia con `PowerWriteACValueIndex` y no con el registro (`power_mode.cpp:51-56`):

| Familia | Archivos de hoy | Solo si |
|---|---|---|
| Driver AMD (caché de shaders, DX11, teselación, VRAM reportada) | `amd_driver_tweaks`, `shader_cache`, `vram_reporting` | la GPU es AMD |
| Planificador de Windows (prioridades, paginación, compresión de memoria) | `kernel_tweaks` | siempre |
| MMCSS (prioridad multimedia), sin "GPU Priority" | `mmcss` | siempre |
| Pantalla (MPO, Game DVR, pantalla completa) | `display_tweaks` | siempre |
| Por juego (modelo de presentación, GPU preferida) | `dxgi_flip` | siempre |
| Energía (plan, ASPM, boost) | `power_mode` | según el tipo de equipo (fase 3) |
| Copias del registro y consulta del estado | `registry_backup`, `tweaks_query` | siempre: pasan a ser parte del adaptador de registro |

`main` decide qué familias se crean según el fabricante. La GUI muestra solo las que existen.

**Ajustes que se retiran (decidido el 2026-11-09, comprobado en Microsoft Learn):** el **temporizador de 0,5 ms**, porque desde Windows 10 2004 la resolución que pide un proceso ya no cambia la de los demás (tampoco la del juego); y **GPU Priority** de MMCSS, que Microsoft documenta como "not yet used". Se quitan su código, sus claves en `config.json` (`timer_resolution_ns100`, `timer_forced_manual`), sus filas en la GUI y su restauración pasa a ser solo de limpieza: `--restore-registry` sigue devolviendo lo que hubieran dejado en equipos que ya lo aplicaron. `OverlayTestMode` (MPO) no está documentado por Microsoft: se queda como opción apagada y su ⓘ lo dice.

**Terminado cuando:**
- el fabricante se decide en un solo sitio, y en un perfil simulado de Intel o NVIDIA no se crea ningún objeto de la familia AMD;
- `--restore-registry` devuelve todo igual que hoy (prueba de integración en el laptop);
- no queda ninguna función estática en `GraphicsOptimizer`.

## Evidencia (commit `61eed56`)

**Fase 8 (`src/optimize/graphics/`)**
- 36 funciones estáticas: `graphics_optimizer.hpp:54-122`.
- Fabricante por nombre del driver: `amd_driver_tweaks.cpp:55-67`; por número de fabricante: `vram_reporting.cpp:82`, `:179` y `:285`.
