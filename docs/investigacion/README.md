# Investigación

**Lector:** personas y agentes. Fecha: 2026-11-09.

Lo que ya se investigó y verificó contra fuentes oficiales, para **no repetir búsquedas**. Antes de buscar algo en la web, mira aquí; si está y no ha caducado, úsalo.

## Documentos

| Documento | Qué responde |
|---|---|
| [`dxvk-descarga.md`](dxvk-descarga.md) | De dónde bajar las DLL de DXVK-GPLAsync con versión fija, y su licencia |
| [`cpu-energia-y-procesos.md`](cpu-energia-y-procesos.md) | Cómo reparte Windows los núcleos (EcoQoS, CPU Sets, Intel híbrido, AMD X3D, más de 64 hilos), el temporizador, MMCSS y las versiones mínimas de Windows |
| [`graficas-y-perfil-del-equipo.md`](graficas-y-perfil-del-equipo.md) | Cómo saber qué gráfica hay y cómo está conectada, qué leer del equipo (disco, batería, pantalla, Windows, ARM), cómo se cuentan los fotogramas y qué variables de Unreal existen |
| [`librerias-y-herramientas.md`](librerias-y-herramientas.md) | Qué librerías sirven con MinGW y cuáles no, y por qué |
| [`fps-y-estabilidad.md`](fps-y-estabilidad.md) | Todas las palancas ordenadas por lo que pueden subir los FPS o la estabilidad, con lo ya medido, y qué medir primero |
| [`apis-graficas-dx-y-vulkan.md`](apis-graficas-dx-y-vulkan.md) | Qué le quita rendimiento a un juego según su API (DX9 a DX12 y Vulkan), funciones que solo aprovecha el juego, drivers en mantenimiento, capas Vulkan y errores encontrados en la detección de API de VegaFlow |
| [`recursos-de-windows.md`](recursos-de-windows.md) | Qué hace Windows con CPU, memoria, energía y gráfica mientras se juega; revisión de lo que VegaFlow ya toca y palancas nuevas (estables, experimentales, solo informar y descartadas) |
| [`gui-y-accesibilidad.md`](gui-y-accesibilidad.md) | Umbrales de WCAG, contrastes medidos de la paleta y qué documenta Microsoft sobre lo que muestran los botones ⓘ |

## Reglas

1. **Cada dato lleva fuente, fecha y confianza** (alta: fuente oficial; media: fuente oficial con matices, de un fabricante, o prensa que cita una declaración del fabricante; baja: solo terceros, foros o guías). Sin fuente, se marca **sin verificar**.
2. **Cada dato dice qué se decidió con él** y dónde (fase del [plan](../plan/maestro/README.md)), para que se vea qué se rompe si cambia.
3. **Caducidad:** un dato de una API de Windows se revisa si sale una versión mayor de Windows; el de una librería, si cambia de versión mayor; el de un fabricante, al año. La fecha de cada tabla dice cuándo se comprobó.
4. Un dato nuevo va en el documento de su tema. Si cambia, se corrige en su sitio y se pone la fecha nueva; la versión vieja queda en git.
5. Lo que falta por investigar está abajo, para que una búsqueda futura vaya directa.

Resuelto en el laptop el 2026-11-09 (clave `disable_environment`, ICD de Vulkan, `vulkan-1.dll` con el overlay de Steam, `ConfiguredClockSpeed`): ver [`../mediciones/2026-11-09-diagnostico-laptop.md`](../mediciones/2026-11-09-diagnostico-laptop.md).

## Pendiente de investigar

| Pregunta | Para qué fase | Dónde buscar |
|---|---|---|
| Si `IOCTL_STORAGE_QUERY_PROPERTY` funciona sin permisos de escritura sobre `\\.\X:` (Microsoft se contradice) | 3 | Probarlo en el laptop |
| Qué señales genéricas (servicios o drivers) delatan un anti-cheat (EAC, BattlEye, Vanguard) sin falsos positivos | 5 | Documentación de cada anti-cheat |
| Cómo convive el driver "3D V-Cache Performance Optimizer" de AMD con otra herramienta que fije núcleos | 7 | AMD (no hay fuente oficial a 2026-11-09) |
| Cómo identificar los LP E-cores de Intel Core Ultra (`EfficiencyClass` o caché aparte) | 7 | Intel, topologías reales |
| Si `r.Streaming.AmortizeCPUToGPUCopy` y `MaxNumTexturesToStreamPerFrame` existen en el código de Unreal | 3 | Código fuente de Unreal Engine |
| Si el ejecutable arranca en Windows 10 1709 | G | Probarlo |
| Resoluciones y escala por defecto de ROG Ally, Legion Go y Steam Deck con Windows; XInput con un juego delante | G | Fabricantes y prueba real |
| Cómo se ve una notificación de bandeja sobre un juego en pantalla completa exclusiva | G | Prueba real |
| Si la API de punto de restauración funciona con la protección del sistema apagada, y su límite de frecuencia | 2b | Microsoft Learn (`SRSetRestorePoint`) |
| Coste de comprobar la firma de un ejecutable (`WinVerifyTrust`) cada 1,5 s | 5 | Medirlo |
| Si un cambio de nombre de archivo (temporal y renombrar) es seguro en NTFS ante un corte de luz, y cómo encaja con `SandboxGuard` | 2b | Microsoft Learn (`MoveFileEx`, `ReplaceFile`) |
| Permisos y rutas de los juegos de Xbox / Microsoft Store (`WindowsApps`), y dónde guardan la configuración Epic, GOG y EA | §9 | Microsoft Learn y cada tienda |
| Qué tocan Armoury Crate, Legion Space y otros optimizadores del fabricante | §9 | Fabricantes |
| Opciones y coste de firmar el ejecutable de un proyecto de código abierto | §9 | Autoridades de certificación, SmartScreen |
| Métodos oficiales para cambiar los límites de potencia de una APU, y si las herramientas conocidas usan drivers que Windows marca como vulnerables | §10 | AMD, Microsoft (lista de drivers bloqueados) |
| Si un mando (XInput) cuenta como presencia del usuario para la QoS por inactividad con batería | 8 | Microsoft Learn (Quality of Service) y prueba real |
| Si `AutoFlipModel` y `DirectFlip` en `UserGpuPreferences` existen para Windows | 8 | Microsoft Learn, DirectX blog |
| Si quitar la compresión de memoria o vaciar la memoria del fondo ayuda o perjudica en una APU con 8 o 16 GB | 8 | Medirlo con A/B |
| Texto actual de la página de soporte de Microsoft sobre HVCI, VMP y juegos | 3 | support.microsoft.com |
| Qué procesadores tienen CPPC v2 en modo autónomo (dónde `PERFEPP` tiene efecto) y cómo detectarlo | 8 | Microsoft Learn (PerfAutonomousMode), AMD, Intel |
| Si el control deslizante en "Mejor rendimiento" (o el plan que pone VegaFlow) anula el EcoQoS explícito de los procesos de fondo | 7 | Microsoft Learn (power slider, Quality of Service) y prueba real con `GetProcessInformation` |
| Si las GPU upload heaps siguen exigiendo el modo desarrollador de Windows | 3 | DirectX-Specs, Microsoft Learn |
| Si `GpuPreference` cambia la gráfica que elige un juego Vulkan en un portátil con dos gráficas | 3 | Khronos (código del cargador) y prueba real |
| Anuncio original de AMD sobre Vega y Polaris en mantenimiento, y si RDNA 1 y 2 también lo están | 3 | Notas de versión de AMD |
| Si el driver DX11 de AMD gasta más CPU que DXVK en una APU Vega | 8 | Medirlo con A/B en el laptop |
| Si el Sensor de almacenamiento borra la caché de sombreadores de DirectX | 3 | Microsoft Learn |
| Si el paso a mantenimiento de Vega incluye las APU (Ryzen 4000 y 5000 con Vega) | 3 | Notas de versión de AMD |
| Si el modo "Mejor rendimiento" sube los límites de potencia en portátiles que no son Surface | 3 | Fabricantes |
| Por qué el preset UMA borra `r.ScreenPercentage` y si conviene ofrecerlo como opción | 8 | Historial de git y A/B |
| Fuente de AMD para la clave de registro de la caché de sombreadores que fuerza `shader_cache.cpp` | 8 | AMD |
| Dónde guarda Steam si la precompilación de sombreadores está activada | 3 | Valve |
| Si limitar los FPS mejora el 1 % Low en una APU (y si la energía sobrante va a la CPU) | 8 | Medirlo con A/B |
| Si la línea base de Octopath (`octopath_world.json`) se midió con SCSKiller dentro del juego | 8 | Fechas de la línea base frente al `d3d11.dll` de SCSKiller (2026-11-07) |
| Qué exponen AMD ADLX, NVIDIA NVAPI e Intel IGCL sin drivers propios, su licencia y si compilan con MinGW | §10 | SDK de cada fabricante |
