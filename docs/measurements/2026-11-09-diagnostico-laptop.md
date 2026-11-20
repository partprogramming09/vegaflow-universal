# Diagnóstico del laptop de pruebas (2026-11-09)

**Lector:** personas y agentes. Lecturas hechas por SSH y con los trabajos `doctor` y `state` de WinBridge, sin cambiar nada salvo lo indicado en "Cambios hechos".

Equipo: Ryzen 5 4600H con Radeon Vega 6, Windows 11 26H2 (compilación 26300.9818), 16 GB.

## Fase 0: línea base

Clon limpio de `61eed56` en el laptop (carpeta nueva; la copia vieja no se tocó), compilado con `scripts/build.ps1 -RunTests`:

- **C++: 31 de 31 pruebas en verde** (202 s, compilación incluida).
- **Rust (`cargo test --target x86_64-pc-windows-gnu`): 25 de 25 en verde.**
- El clon nuevo está en `C:\dev\vegaflow-universal`; `scripts/remote/.env` apunta ahí (`VFJOB_REPO`), y `scripts/remote/vfdev build-test` lo confirmó: 31 de 31. La copia vieja sigue en el laptop, sin uso, con el historial antiguo: no hacer `push` desde ella.

## Lo que la investigación decía y lo que el laptop muestra

| Afirmación (documento) | Resultado en el laptop | Veredicto |
|---|---|---|
| El canal de la memoria se puede deducir de `Win32_PhysicalMemory` ([`recursos-de-windows.md`](../investigacion/recursos-de-windows.md)) | `BankLabel` dice "P0 CHANNEL A" y "P0 CHANNEL B": dos módulos de 8 GB (Micron y ADATA), DDR4-3200, configurados a 3200 | **Confirmado en este equipo.** Doble canal y a velocidad nominal. Falta ver otros fabricantes |
| `ConfiguredClockSpeed` y `Speed` existen y se pueden comparar ([`fps-y-estabilidad.md`](../investigacion/fps-y-estabilidad.md)) | Los dos dan 3200 | **Confirmado** |
| HVCI y la plataforma de máquina virtual pueden costar rendimiento | VBS apagado (`VirtualizationBasedSecurityStatus=0`), sin hipervisor, `VirtualMachinePlatform` desactivada | En este equipo **no aplica** |
| `PERFEPP` solo tiene efecto con CPPC en modo autónomo | `PERFAUTONOMOUS=1` (activado); EPP en 50 | En este equipo `PERFEPP` **sí tiene efecto** |
| Un overlay carga `vulkan-1.dll` en un juego DX11 ([`apis-graficas-dx-y-vulkan.md`](../investigacion/apis-graficas-dx-y-vulkan.md)) | Octopath Traveler II con el overlay de Steam cargado (`gameoverlayrenderer64.dll`): **sin** `vulkan-1.dll` | **Desmentido** para el overlay de Steam en este juego |
| "Si `d3d11.dll` está en la carpeta del juego, es DXVK" (propuesta de la misma página) | En la carpeta de Octopath hay un `d3d11.dll` que **no es DXVK**: es SCSKiller (ver abajo) | **Desmentido:** esa regla clasificaría mal |
| Capas Vulkan implícitas huérfanas | Seis capas (overlay de Epic, overlay de Steam y fossilize de Steam, en 64 y 32 bits); todas apuntan a archivos que existen. Cada JSON trae su variable para desactivarla (`disable_environment`, p. ej. `DISABLE_VK_LAYER_VALVE_steam_overlay_1`) | Ninguna huérfana. **Confirmado** el nombre de la clave JSON |
| ICD de Vulkan huérfanos | `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` no existe; el driver de AMD registra su Vulkan en la clave de la clase de pantalla (`VulkanDriverName`) | No hay restos. La ruta a revisar es la clase del adaptador, no `Khronos\Vulkan\Drivers` |
| Vega en mantenimiento | Driver 31.0.21925.1001, fecha 2026-05-19, fecha compatible con la rama "Adrenalin 26.5.2 para Polaris y Vega" que cita la prensa | Coherente, sin confirmación de AMD |
| Precompilación de sombreadores de Steam | La capa `VkLayer_steam_fossilize` está registrada y `config.vdf` tiene `ShaderCacheSize` | Probablemente activa; no se localizó el interruptor |
| Caché de sombreadores y Sensor de almacenamiento | D3DSCache 3 MB, AMD DxCache 143 MB, DxcCache 6 MB, VkCache 1 MB; Sensor de almacenamiento **activado** | Sigue sin saberse si el Sensor la borra |
| Modo de energía efectivo | Plan "Equilibrado" con el **overlay "Mejor rendimiento" en corriente y también en batería** | Quién lo puso no consta (ni VegaFlow ni el usuario lo dicen) |
| Modo Juego, GameDVR | Modo Juego activado; captura de la Game Bar apagada | Sin problema |
| HAGS | `HwSchMode` no existe (valor por defecto del driver) | No configurada |
| Pantalla | 1920×1080 a 60 Hz; el driver dice que 60 Hz es el máximo | Sin problema |

## Problemas encontrados

1. **SCSKiller dentro de Octopath Traveler II.** El `d3d11.dll` de la carpeta del juego es "SCSKiller D3D12 proxy" 1.4.6.0, sin firmar. SCSKiller es una herramienta contra los tirones por compilación de sombreadores, instalada desde Descargas (instaladores del 2026-10-13 y del 2026-10-25), que arranca con Windows y guarda sesiones de Octopath (Steam 1971650) y de un juego de EA. El archivo es del 2026-11-07, así que **cualquier medición de Octopath desde entonces lleva SCSKiller dentro**. La línea base [`baselines/octopath_world.json`](../../scripts/remote/baselines/octopath_world.json) entró en git el 2026-11-05, antes de ese archivo, pero SCSKiller está instalado desde el 2026-10-13 y una versión anterior del DLL pudo estar ya en la carpeta: no se sabe con certeza si la línea base lleva SCSKiller. No se cambió nada: es software del usuario.
2. **VegaFlow escribió preferencias de GPU para programas que no son juegos:** `mmc.exe`, `LockApp.exe`, `ShellHost.exe`, Radeon Software, los launchers de EA y Epic, SCSKiller y **el propio `vegaflow_daemon.exe`**. Son falsos positivos del detector de juegos, y además `GpuPreference=2` no hace nada en un equipo con una sola gráfica. Se quedaron en el registro sin copia en `RegBackup`.
3. **Ajustes de energía fuera de fábrica con el daemon parado:** `PERFBOOSTMODE=3` y `ASPM=0` según `doctor` y `state`. VegaFlow solo escribe 4 o 2 en `PERFBOOSTMODE` (`src/optimize/graphics/power_mode.cpp:69-89`), así que el 3 lo puso otro programa o una versión vieja. Después de correr la suite valían 2 y 1. El guardián de la suite debería devolver los valores previos (3 y 0), y leyendo `power_mode.cpp` y `tests/test_main.cpp:74-81` no se encontró qué camino escribió los de fábrica: queda abierto. Si la suite pisa los valores que había, la frase "devuelve el plan a su estado previo" de `estado-de-verificacion.md` no es exacta. Es el defecto conocido de cambios que sobreviven al daemon (fase 2b).
4. **Valores del driver de AMD escritos en el registro:** `ShaderCache=0x32` (forzada), `EnableUlps=0`, `FlipQueueSize=1`, `TurboSync=0`, `Tessellation=64`. `state` los marca "solo informe" porque no se conoce el valor de fábrica; no se tocaron.
5. **Programas de fondo pesados:** la app de EA (`EACefSubProcess`) lleva unos 18 000 s de CPU y 1 GB de memoria; el launcher de Epic y su overlay, unos 4 600 s cada uno, en dos días encendido. Es justo la carga que el confinamiento del fondo corrige, y la que la fase 5 debería clasificar como "launcher".

6. **RyzenAdj del usuario en marcha durante todo:** `C:\RyzenAdj\Auto_Gaming_Watcher.ps1` lanza `ryzenadj.exe` cada 20 s con el cargador (STAPM 30 W, rápido 35 W, 85 °C) y cada 30 s con batería (15 W). `ryzenadj.exe` falla: 300 errores 1000 en el registro de aplicaciones en 3 días (el máximo leído). La carpeta incluye `WinRing0x64.sys`, un driver con fama de vulnerable (sin comprobar en la lista de bloqueo de Microsoft). Cambia los límites de potencia por debajo de cualquier medición; no se tocó.

## Cambios hechos en el laptop

| Qué | Estado final | Copia o vuelta atrás |
|---|---|---|
| Clon nuevo en `C:\dev\vegaflow-universal` | Se queda | La copia vieja sigue intacta |
| `vegaflow_daemon.exe` de `C:\ProgramData\VegaFlow` cambiado por el del clon (`61eed56`) | Se queda | El anterior en `vegaflow_daemon.2026-11-08.exe` |
| Tareas `VegaFlow-Start`, `-Stop`, `-State` y `-Restore` con `-WindowStyle Hidden` | Se queda (arreglo) | Igual que `scripts/setup_tasks.ps1` del repo |
| 8 preferencias de GPU de programas que no son juegos | Quitadas | `respaldo-2026-11-09\UserGpuPreferences.reg` |
| `PERFBOOSTMODE` y `ASPM` | En 2 y 1 (fábrica); ya estaban así al restaurar | `respaldo-2026-11-09\*.txt` |
| `config\config.json` (temporizador a 15,6 ms para una prueba) | Devuelto a 0,5 ms | `config.json.2026-11-09.bak` |
| `ab_profile.ps1` (modo `--dry-run` para una prueba) | Devuelto | — |
| `d3d11.dll` de SCSKiller en la carpeta de Octopath | Renombrado durante las pruebas y **restaurado** | — |
| Programa SCSKiller | Cerrado durante las pruebas; **no se volvió a abrir** (arranca solo al iniciar sesión) | — |
| Octopath Traveler II | Abierto y cerrado varias veces | — |
| EcoQoS aplicado a mano a procesos de fondo | Quitado al terminar cada prueba | — |
| Guiones temporales `vfx_*.ps1` y capturas `oct1/oct2.png` | Borrados | — |

En el Mac, `scripts/remote/.env` (no versionado) apunta ahora al clon nuevo. Los procesos `chrome` que aparecen en las pruebas son del Chrome del usuario en segundo plano (`--no-startup-window`), no restos de la carga de prueba. Los resúmenes JSON de los A/B con la herramienta corregida están en [`assets/2026-11-09/`](assets/2026-11-09/); los CSV de cuadros (16 MB) quedan en el laptop (`vfdev fetch n09fix`).

## Mediciones en Octopath Traveler II sin SCSKiller (2026-11-09)

Condición: `d3d11.dll` de SCSKiller renombrado a `d3d11.dll.scskiller-off` y SCSKiller cerrado (a petición del usuario, para medir solo VegaFlow); daemon compilado de `61eed56` desplegado (el anterior quedó como `vegaflow_daemon.2026-11-08.exe`); escena del autoguardado; PresentMon con `scripts/bench.ps1`. El `RyzenAdj` del usuario siguió activo en todas las condiciones (ver problemas).

**Defecto de la herramienta de pruebas, corregido.** La tarea `VegaFlow-Start` lanzaba PowerShell sin `-WindowStyle Hidden` y dejaba una consola "Administrador: powershell.exe" encima del juego. Se corrigió en las tareas del laptop y en `scripts/setup_tasks.ps1`. No era la causa de la regresión de abajo (la regresión sigue sin la consola), pero sesgaba cualquier medición "con daemon" anterior.

| Prueba (A/B intercalado) | Apagado | Encendido | Lectura |
|---|---|---|---|
| Daemon, sin carga, 3×120 s (con la consola visible) | 42,77 · 42,91 · 42,84 FPS; 1 % Low 33,2 · 34,6 · 34,6 | 42,80 · 42,61 · 42,74; 1 % Low 32,8 · 30,9 · 32,4 | Media igual; 1 % Low ≈ −6 % |
| Daemon, sin carga, 3×120 s (consola corregida) | 42,81 · 42,81 · 42,76; 1 % Low 34,1 · 34,6 · 34,4 | 42,70 · 42,67 · 42,73; 1 % Low 32,2 · 32,1 · 32,6 | **Regresión real: 1 % Low ≈ −6 %**; el juego pasa de Legacy Flip a Independent Flip |
| Daemon, 4 workers de Chrome, 3×120 s | 26,78 · 25,70 · 25,66; 1 % Low 18,6 · 18,7 · 17,9 | 35,68 · 34,88 · 35,69; 1 % Low 23,1 · 23,4 · 25,2 | **+36 % de media, +30 % de 1 % Low** |
| Daemon sin el temporizador de 0,5 ms, sin carga | 42,83 · 42,83 · 42,85; 1 % Low 34,2 · 34,6 · 35,8 | 42,83 · 42,79 · 42,74; 1 % Low 32,0 · 34,3 · 32,6 | El temporizador no es la causa |

Aislamiento con el daemon apagado (3×60 s intercalado cada uno, sin carga):

| Componente aplicado a mano | Resultado |
|---|---|
| Prioridad del juego Normal / AboveNormal / High | Todas 42,8 de media y ≈34 de 1 % Low: **HIGH no aporta nada sin carga de fondo**; con carga está sin aislar |
| Perfil de energía APU (`--apu-gpu-priority`: boost 4, EPP 25, ASPM 0) | Sin cambio (42,4–42,9; 1 % Low 34–35) |
| `Win32PrioritySeparation=0x28` | Sin cambio |
| Valores FSE de `GameConfigStore` | Ya estaban iguales con el daemon apagado: no pueden ser la causa |
| EcoQoS solo al overlay de Epic (`EOSOverlayRenderer`) | Sin cambio (1 % Low 34,0 frente a 34,0) |
| Daemon en `--dry-run` (calcula sin aplicar) | Se queda en Legacy Flip: **la regresión viene de algo que aplica** |
| EcoQoS a todo el fondo de la sesión (33 procesos) | Sin cambio (1 % Low 33,8 · 34,1 · 33,4 frente a 33,9 · 34,0 · 33,6) |
| Reescribir con el mismo valor `GameConfigStore` y `UserGpuPreferences` del juego | Sin cambio; se queda en Legacy Flip |
| Purga de la lista en espera y vaciado del fondo (`--purge-now`, dos veces por medición de 90 s) | Dentro del ruido (1 % Low medio 33,8 con purga, 34,2 sin ella). El guardián de memoria compartida (`EvaluateUmaPressureGuard`) solo hace este mismo vaciado del fondo (`memory_manager.cpp:408`), así que queda cubierto |

Los descartes son de 60 s (90 s la purga), con menos potencia que el A/B de 120 s: una diferencia pequeña podría no verse.

Qué aplica el daemon de verdad sin carga (leído en vivo): prioridad High y E/S 2 al juego; EcoQoS a **todo** el fondo de la sesión aunque no consuma CPU (Chrome, Steam, EA, Epic y su overlay, `msedgewebview2`, `AUEPMaster`), sin confinar a nadie con CPU Sets. El código pone la E/S a 2 con el comentario "High", pero en NT el 2 es Normal (`src/optimize/cpu_orchestrator.cpp:180`).

**Causa de la regresión sin carga: sin aislar.** Ningún componente aplicado por separado desde fuera la reproduce, ni el cambio a Independent Flip. Quedan la prioridad de E/S y de memoria del juego, el hilo de la ventana y las combinaciones (por ejemplo, High más EcoQoS a la vez). El siguiente paso es una compilación de diagnóstico con un interruptor por componente (trabajo de código, con su test).

## Pendiente

- Aislar la regresión del 1 % Low sin carga con una compilación de diagnóstico.
- SCSKiller: su `d3d11.dll` ya está restaurado; el programa se cerró durante las pruebas y vuelve a arrancar al iniciar sesión.
- Pruebas de [`fps-y-estabilidad.md`](../investigacion/fps-y-estabilidad.md) que necesitan reiniciar el juego: resolución interna y límite de FPS.
