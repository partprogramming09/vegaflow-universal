# Plan de rediseño y método de actualización

Estado: borrador del 2026-11-04. Cada punto indica si está verificado (con su evidencia) o por verificar.

## 1. Lo que ya se sabe

Fuente: `docs/MEDICION_BASE.md` (Octopath Traveler II, escena fija, PresentMon 2.x).

- Con 12 tweaks viejos aplicados contra Windows de fábrica, el FPS medio no cambia y el 1% Low de fábrica es igual o algo mejor. Verificado.
- Un temporizador NT de 0.5 ms pedido por otro proceso no cambia `Sleep(1)` de un tercero (~15.4 ms en ambos casos). Verificado.
- Una ventana propia delante de un juego en `Legacy Flip` destruye los lows (1% Low de ~38 a ~16). Verificado.
- Las corridas justo tras reiniciar son atípicas; hay que descartar los primeros ~6 minutos. Verificado.
- La línea base de `drawstress` (bgfx, `dim` 30) varía ~10% entre corridas (D3D11 112 a 124.5 FPS, D3D12 108.9 a 119.2). Verificado, 3 corridas por API.

## 2. Problemas no considerados al diseñar el proyecto

| Problema | Estado | Evidencia |
| --- | --- | --- |
| Los efectos de proceso (CPU Sets, EcoQoS, prioridad I/O) solo alcanzan procesos que el detector clasifica como juego | Por verificar con `drawstress.exe` | `process_detector.cpp`: primer plano con `d3d11.dll`/`d3d12.dll`/`vulkan-1.dll` o `custom_game_exes` |
| `Tasks\Games` (MMCSS) solo afecta a hilos que se registran como "Games"; un motor que no lo hace no recibe nada | Por verificar | Mecanismo documentado de MMCSS, sin medición propia |
| El daemon continuo aplica GameDVR/FSE y el perfil de energía APU al arrancar y, al cerrar, solo restaura si había un juego activo | Verificado por lectura de código | `main.cpp`: llamadas al arrancar frente al bloque de cierre condicionado a `game_active` |
| La línea base nunca fue un Windows de fábrica: siguen puestos `UMD\Tessellation=16`, `TurboSync`, `FlipQueueSize`, `TFQ`, `Main3D_DEF` y 13 entradas de `UserGpuPreferences` | Verificado | Informe de `VegaFlow-State` del 2026-11-04 |
| Esos valores del driver no tienen valor de fábrica confirmado, así que `VegaFlow-Restore` no los toca | Verificado | `tweaks_state.ps1` los marca "solo informe" |
| WinBridge no puede pulsar un UAC, pero la sesión SSH del laptop ya corre elevada y no lo necesita | Verificado | `Diagnose` con `input_desktop: secure`; `whoami /groups` por SSH: `High Mandatory Level` |
| La suite de pruebas ejecuta cambios reales (modos "Exec") y deja el sistema modificado: tras una corrida, `UMD\Tessellation` pasó de 16 a 64, `TurboSync`, `TFQ` y `Main3D_DEF` a 0 | Verificado | `VegaFlow-State` antes y después de `build.ps1 -RunTests` el 2026-11-04 |
| `GameConfigStore` (`GameDVR_Enabled=0`, `GameDVR_FSEBehaviorMode=2`) queda aplicado y `tweaks_state.ps1` no lo vigila | Verificado | Lectura del registro el 2026-11-04 |
| Una sesión SSH no tiene credenciales de GitHub ni `g++` en el PATH | Verificado | `git fetch` falló con `wincredman`; `g++` está en `scoop\apps\mingw\16.2.0-rt_v14-rev1\bin` |
| `CLAUDE.md` enlaza rutas `file:///C:/Users/testuser/OneDrive/...` del proyecto original | Verificado | Aparecen en las secciones 3 y 4 |
| El estado de los tweaks no se comprueba antes de medir | Resuelto con `VegaFlow-State` | `scripts/tweaks_state.ps1` |

## 3. Principios del rediseño

1. Ninguna afirmación de rendimiento sin una medición propia que supere el ruido entre corridas.
2. Cada tweak tiene un interruptor individual y una reversión verificable, y se informa si no tiene valor de fábrica confirmado.
3. Las pruebas A/B se intercalan (A, B, A, B) y leen el estado del sistema antes de cada captura.
4. Nada elevado ejecuta código de una carpeta escribible por el usuario: todo vive en `C:\ProgramData\VegaFlow` con hash registrado.
5. Lo que no mejora de forma demostrable se retira o se deja apagado por defecto.

## 4. Método de actualización

Modelo: `winbridge update` (paquete verificado por hash, por SSH, sin UAC, con reversión).

1. En la Mac: compilar o cross-compilar, generar `bundle.zip` con el daemon, los scripts y un `manifest.json` con SHA-256 de cada archivo.
2. Enviar el paquete por `scp` a una carpeta de preparación del laptop (sin credenciales de GitHub).
3. En el laptop, sin elevación: verificar los hashes y las 26 pruebas del paquete; si fallan, no se instala nada.
4. Con la tarea elevada `VegaFlow-Update` (se registra una sola vez): copiar a `C:\ProgramData\VegaFlow\staging`, conservar la versión anterior en `previous\`, intercambiar y registrar el hash instalado en `setup.log`.
5. Humo posterior: `VegaFlow-State` sin diferencias inesperadas; si falla, reversión automática a `previous\`.

Estado: los pasos 1 a 3 se pueden hacer hoy. El paso 4 también puede correr por SSH, que ya está elevado, sin registrar tareas: queda como mejora opcional una tarea `VegaFlow-Update` si se quiere ejecutar sin sesión SSH.

## 5. Fases y estado

1. Instrumento. Hecho: `perf_record` con `drawstress` solo da medias (`drawstress-log`) y un proceso lanzado desde SSH corre en la sesión 0, donde PresentMon no ve presents. Se usa `scripts/bench.ps1` con el juego lanzado en el escritorio (WinBridge, `Win+R`).
2. A/B intercalado de `drawstress` (D3D11 y D3D12). Hecho: `docs/MEDICION_AB_DAEMON.md`.
3. Auditoría de `CLAUDE.md`. Parcial: enlaces corregidos; las afirmaciones de rendimiento siguen sin medición propia (sección 6).
4. Reorganización: retirar o apagar lo que no se demuestre. Pendiente.
5. Método de actualización por paquete (sección 4). Pendiente.

## 6. Hallazgos de la sesión del 2026-11-04 y trabajo pendiente

Corregido y verificado:

- `CPMINCORES=100` del perfil APU bajaba el FPS de `drawstress` D3D11 un 21% por sí solo; con el daemon completo el coste era del 10.7%. El perfil ahora deja el valor de fábrica y el A/B posterior muestra daemon encendido igual a apagado en D3D11 y D3D12 (`842df5b`).
- El cierre limpio del daemon en modo continuo no restauraba el perfil de energía si no había juego activo; ahora lo restaura siempre.
- El daemon aplicaba dos veces `SetGameDvrAndFseOptimized` y `SetApuLaptopPowerProfile` al arrancar; se quitó el duplicado.
- `tweaks_state.ps1` no vigilaba el plan de energía ni `GameConfigStore`: ahora informa `CPMINCORES`, `PERFEPP`, `PERFBOOSTMODE`, `ASPM` y los valores `GameDVR_*`.
- `CLAUDE.md` tenía 20 enlaces absolutos al proyecto original; ahora son relativos y todos resuelven.

Pendiente, por prioridad:

1. Las pruebas ejecutan cambios reales y "restauran" a valores por defecto, no a los previos (`UMD\Tessellation` quedó en 64 tras correrlas). Cambiarlas a simulación o a guardar y restaurar el valor previo.
2. Las demás partes del perfil APU (EPP 25, boost 4, ASPM 0) no tienen efecto medido en `drawstress`; medirlas en una carga con presupuesto térmico compartido con la iGPU (un juego real o un benchmark de GPU) antes de mantenerlas.
3. `GameDVR_Enabled=0` y `GameDVR_FSEBehaviorMode=2` se escriben sin guardar el valor previo y no se restauran al salir. Guardar el valor previo, como el plan de energía.
4. El daemon no detectó `drawstress.exe` como juego (ventana de bgfx, `d3d11.dll` o `d3d12.dll` cargada). Investigar por qué: si la detección falla con apps en ventana, las optimizaciones por proceso no se aplican a ese tipo de aplicación.
5. Ninguna optimización por proceso (CPU Sets, EcoQoS, prioridad de I/O) tiene todavía una medición propia con carga real. Medirlas con un workload que el daemon sí detecte.
6. `src/main.cpp` (1925 líneas) y `src/optimizers/graphics_optimizer.cpp` (2009 líneas) concentran demasiada lógica; dividir con las pruebas actuales como red de seguridad.
7. Medir otras cargas con percentiles: Godot (`godot-benchmarks`) y la muestra `nBodyGravity` (DirectX-Graphics-Samples) para AsyncCompute.
8. Registrar en `CLAUDE.md` que una sesión SSH corre en la sesión 0 y que las mediciones de PresentMon requieren lanzar la carga en el escritorio.
