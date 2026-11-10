# Fase 3: perfil del equipo (empieza por la GPU)

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** La arquitectura de la GPU decide el Tier, y el Tier decide qué protecciones se aplican. Si se clasifica mal, una APU moderna se trata como una vieja (o al revés), o pierde las protecciones de memoria compartida.

**Flujo de hoy:**

```
+---------------------------------------------+
| Leer la GPU (DXGI) y su memoria dedicada    |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Más de 2 GB dedicados = "tarjeta dedicada"  |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: adivinar la arquitectura por el nombre |
| ("780m", "680m") o por el modelo de CPU     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Rust: elegir el Tier                        |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: fijar los valores de ese Tier          |
+---------------------------------------------+
```

**Fallos confirmados:**

| Equipo | Qué pasa hoy | Debería ser |
|---|---|---|
| APU con más de 2 GB reservados en la BIOS (780M o Z1 con 4-8 GB) | Se trata como tarjeta dedicada (Tier 4) y pierde las protecciones de memoria compartida | APU moderna (Tier 3) |
| Ryzen 9000 de escritorio (gráfica RDNA2) | Se clasifica como RDNA3 | RDNA2 |
| Ryzen 7000 de escritorio y Steam Deck (el nombre del driver no dice el modelo) | Se clasifican como Vega | RDNA2 |
| Intel UHD de 12.ª generación (por ejemplo UHD 770) | Se trata como gráfica vieja (Tier 1) | Xe |

**Fallos del soporte universal (dGPU y eGPU):**

| Fallo | Qué pasa hoy |
|---|---|
| Se elige la GPU con **más memoria dedicada** | En un portátil con iGPU y dGPU no se sabe en cuál corre el juego |
| No se distingue una GPU **externa** (Thunderbolt, USB4) de una interna | Una eGPU recibe las mismas reglas que una tarjeta interna |
| El preset de Unreal se aplica **a cualquier** juego Unreal | Con GPU dedicada se recortan sombras y reflejos (`r.AOQuality=0`, `r.SSR.Quality=0`) sin necesidad |

**Flujo nuevo:**

```
+---------------------------------------------+
| C++ (adaptador DXCore): por cada GPU,       |
| nombre, memoria, fabricante, ¿integrada?,   |
| ¿extraíble?                                 |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++ (SetupAPI): política de retirada del    |
| dispositivo, para confirmar una eGPU        |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: preferencia de GPU que el usuario puso |
| en Windows para el juego (UserGpuPreferences|
| ya se lee hoy). Si no hay, la de por defecto|
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Rust: en un solo paso, arquitectura, Tier   |
| y conexión (integrada, dedicada o externa)  |
| de la GPU que usa el juego                  |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: aplicar el perfil de ese tipo          |
+---------------------------------------------+
```

**Perfil por tipo de conexión:**

| Conexión | Qué se aplica | Estado de la evidencia |
|---|---|---|
| Integrada (UMA) | Lo de hoy: guardián de memoria compartida, preset de sombras y purga | Medido en el laptop |
| Dedicada interna | **Sin recortes visuales.** Solo la caché de shaders (`r.ShaderPipelineCache.Enabled=1`, documentada por Epic). Sin guardián UMA | Sin medir: va como opción |
| Externa (eGPU) | Lo mismo que la dedicada, y con `r.Streaming.AmortizeCPUToGPUCopy=1` + `r.Streaming.MaxNumTexturesToStreamPerFrame` (repartir la subida de texturas, porque el cable es más lento) | Sin medir. Además, esas dos variables solo salen en una wiki de terceros: se comprueban en el código del motor antes de usarlas |

Se quita del plan antiguo `r.ShaderPipelineCache.AsyncCompileRate`: no aparece en la documentación de Epic. `r.GTSyncType=1` tampoco va por defecto: cambia la sincronización del motor para bajar la latencia, no es un arreglo de rendimiento, y no está medido.

**Perfil completo del equipo (pedido el 2026-11-09).** La GPU es solo una parte. Para tratar bien cada dispositivo, VegaFlow reúne al arrancar un **perfil del equipo** (el disco, en cambio, por cada juego): C++ lo lee y Rust decide con él. Además, es lo primero que ve la persona en Inicio.

| Dato | Cómo se lee en Windows | Para qué lo usa VegaFlow | Si no se puede leer |
|---|---|---|---|
| Procesador: fabricante, modelo, núcleos, hilos, tipos de núcleo, cachés y grupos | CPUID y `GetSystemCpuSetInformation` (ya se usan) | Reparto de núcleos (fase 7) | Sin confinar el fondo a núcleos; solo EcoQoS |
| Gráficas: nombre, fabricante, conexión, memoria y driver | DXCore, DXGI y SetupAPI (esta fase) | Nivel, guardián de memoria, perfil de motor, familia de driver (fase 8) | Perfil "dedicada" sin recortes y sin tocar el driver |
| RAM: total y en uso | `GlobalMemoryStatusEx` (ya se usa) | Umbrales de memoria (fase 4) | Sin purgas |
| Disco del juego: SSD, NVMe o HDD (se lee por juego, no una sola vez) | Carpeta del juego → volumen (`GetVolumePathName`) → disco (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`) → `IOCTL_STORAGE_QUERY_PROPERTY`: penalización de búsqueda (SSD o HDD) y `BusType` (NVMe). Se prueba primero sin pedir acceso de escritura | **Solo informativo.** Cambiar la purga según el disco sería una palanca nueva sin medir | "No se pudo leer" (también con RAID, Storage Spaces o USB) |
| Energía: con o sin batería, enchufado o no, ahorro de batería | `GetSystemPowerStatus` (el valor 255 es "desconocido"). No dice si es portátil o sobremesa: un sobremesa con SAI puede tener "batería" | Con batería no se fuerza el plan de alto rendimiento | No tocar el plan de energía |
| Pantalla: resolución y frecuencia, por monitor | `EnumDisplayDevices` + `EnumDisplaySettings`, leídos desde la GUI (corre en la sesión del usuario) | Solo informativo | — |
| Windows: versión y compilación | Solo para mostrarla. Para activar algo se **comprueba que la función existe** (por ejemplo, que `dxcore.dll` exporta `DXCoreCreateAdapterFactory`), como pide Microsoft | Activar solo lo que existe | Lo mínimo común |
| Arquitectura del sistema | `IsWow64Process2`: si la máquina nativa es ARM64, el daemon x64 está emulado (el campo del proceso no sirve, porque un x64 emulado no es WOW64). Alternativa documentada: `GetProcessInformation(ProcessMachineTypeInfo)`, build 22000 o posterior | En Windows sobre ARM (Snapdragon X): avisar y no aplicar ajustes de driver | No aplicar ajustes de driver |

Las filas de esta tabla se comprobaron contra Microsoft Learn el 2026-11-09 (fuentes en el anexo). Queda por probar en el laptop si leer el disco funciona sin pedir acceso de escritura.

**Cómo se sabe la arquitectura de la GPU:** por fabricante e **ID de dispositivo** (ya se lee en `hardware_profiler.cpp:127` y no se usa), con una tabla en Rust; el nombre queda como respaldo. Sin eso no se arreglan el Ryzen 7000 ni la Steam Deck. **Si la arquitectura es desconocida:** con GPU integrada, protecciones de memoria compartida de nivel 2; con GPU dedicada, sin recortes. Hoy "desconocida" acaba en Tier 4 sin protecciones (`tests/vectors/apu_tier.tsv`). Sin DXCore no se vuelve al corte de 2 GB: la conexión queda "desconocida" y se usa el perfil de dedicada sin recortes y sin guardián de memoria compartida (lo prudente); la regla exacta se fija con ejemplos en esta fase.

**Regla de oro:** un dato que no se pudo leer nunca se adivina. Se marca como "no se pudo leer" en la GUI y la decisión usa el valor más prudente.

**Equipos que deben quedar cubiertos con ejemplos** (en `tests/vectors/device_profiles.tsv`): APU AMD Vega (el laptop), RDNA2 y RDNA3 (680M, 780M, Z1 de ROG Ally y Legion Go), Steam Deck con Windows, Intel UHD, Iris Xe y Arc integrada, portátil con iGPU y NVIDIA, sobremesa con AMD o NVIDIA dedicada, eGPU, Intel híbrido, Ryzen X3D, más de 64 hilos, portátil con batería, disco HDD y Windows sobre ARM. Cada ejemplo dice qué debe decidir VegaFlow, no cuánto mejora.

**Por qué así:**
- **DXCore y no DXGI:** la descripción de DXGI solo da memorias; DXCore dice directamente si la GPU es integrada (`IsIntegrated`) o extraíble (`IsDetachable`). Funciona desde Windows 10 build 18936.
- **Si no hay DXCore** (Windows anterior a esa build; además, Microsoft marca esa página como versión previa), se usa DXGI como hoy y la conexión queda como "desconocida", con el perfil integrado o dedicado de siempre.
- **La GPU "del juego" es la preferida, no la confirmada.** La preferencia dice qué GPU pidió el usuario o qué elige Windows por defecto. Cuál usa de verdad no se confirma en este plan: la captura de la fase 6 no identifica el adaptador. Es una limitación conocida. Mientras tanto, el guardián de la fase 4 usa la GPU preferida.
- **SetupAPI además de DXCore:** Microsoft avisa de que una GPU puede desaparecer aunque `IsDetachable` diga que no. La política de retirada del dispositivo es una segunda opinión.
- **El Tier y la conexión son datos distintos:** el Tier habla de cuánta memoria compartida hay; la conexión, de dónde está la GPU. No se crea un "Tier 5 eGPU" (como proponía el plan antiguo), porque mezclaría las dos cosas.
- **Rust decide, porque es una decisión pura** y hoy está repartida entre los dos lenguajes, con reglas repetidas. Además, la parte que adivina por el nombre nunca se ha probado: los ejemplos actuales dan la arquitectura ya resuelta.

**Heredado de R2:** el daemon y la GUI calculan la política una sola vez al arrancar. Una eGPU conectada en caliente no cambia la política hasta reiniciar el daemon; esta fase debe decidir cuándo se vuelve a leer el perfil (por ejemplo, al detectar un cambio de adaptador). Falta también una prueba de que `run_cycle` pasa a los optimizadores la política estática y no la que lleva la memoria de vídeo en vivo: necesita falsificar la detección de procesos, el registro y la energía, y esos puertos llegan con `session/game_session.cpp` en esta fase (aplazada desde la fase 1 el 2026-11-10).

**Terminado cuando:**
- hay una tabla con al menos los equipos de arriba, más el Ryzen 5 4600H, un portátil iGPU+dGPU y una eGPU, todos simulados, y todos dan el Tier y la conexión correctos;
- con GPU dedicada o externa no se escriben recortes visuales en el `Engine.ini`;
- el laptop sigue saliendo como Tier 2 integrada;
- los ejemplos de `tests/vectors/device_profiles.tsv` pasan: cada equipo de la lista de arriba da la decisión esperada;
- cada dato del perfil que no se puede leer (simulado) produce "no se pudo leer" y el valor prudente de su fila;
- lo que no se puede probar en el laptop (dGPU y eGPU reales) queda como **sin medir** en `docs/mediciones/estado-de-verificacion.md`.

## Evidencia (commit `61eed56`)

**Fase 3 (`src/detect/hardware_profiler.cpp`)**
- Corte en 2 GB: `:140-146`.
- Familia `0x1A` tratada como RDNA3: `:227`.
- RDNA2 solo con familia `0x19` y modelo `0x40-0x4F`, o por el nombre: `:232-235`.
- Intel UHD de 12.ª generación: `:243-248`.
- Ejemplos de Tier: `tests/vectors/apu_tier.tsv`, con la arquitectura ya resuelta.

**Fase 3, soporte universal**
- Se elige la GPU con más memoria dedicada: `src/detect/hardware_profiler.cpp:113-118`.
- El preset de Unreal se aplica sin mirar la GPU: `src/app/run_cycle.cpp:132` y `src/app/gui_mode.cpp:457`.
- La preferencia de GPU por juego ya se lee: `GraphicsOptimizer::QueryUserGpuPreferences` (`src/optimize/graphics/graphics_optimizer.hpp:118`).
