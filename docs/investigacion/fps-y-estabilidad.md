# Más FPS y más estabilidad: qué funciona de verdad

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

Este documento ordena **todas** las palancas conocidas según dos preguntas: ¿sube los FPS o la estabilidad (menos tirones, mínimos más altos)? y ¿con qué prueba? Junta lo medido en este proyecto con lo investigado en [`recursos-de-windows.md`](recursos-de-windows.md) y [`apis-graficas-dx-y-vulkan.md`](apis-graficas-dx-y-vulkan.md), y añade palancas nuevas. Ninguna cifra de aquí es una promesa al usuario: el producto dice "Recomendado" y explica qué cambia.

## Lo que ya está medido en este proyecto

De [`estado-de-verificacion.md`](../mediciones/estado-de-verificacion.md):

| Palanca | Resultado medido |
|---|---|
| **Confinar los procesos de fondo** (con el juego sin restringir) | La mayor ganancia demostrada, pero **solo con carga de fondo**: de 2,1 a 2,5 veces los FPS con una carga pesada sintética; +34 % en Octopath Traveler II con 4 workers de Chrome (método antiguo). Sin carga de fondo, nula |
| **Preset UMA de `Engine.ini`** (Unreal) | +40 % de FPS medio en Octopath Traveler II (no intercalado; la causa exacta dentro del preset no está aislada). Ojo: el preset **baja calidad visual** (apaga reflejos SSR, oclusión ambiental, profundidad de campo y desenfoque de movimiento; reduce sombras y bloom: `src/optimize/steam/engine_presets.cpp:29-63`). Parte de la ganancia es calidad cambiada por FPS, y su ⓘ tiene que decirlo |
| Plan de energía (EPP, boost, ASPM), `Win32PrioritySeparation` | Sin efecto medible (≈1 %) |
| `CPMINCORES=100` y confinar al juego | Empeoran (−21 % y entre −19 % y −24 %): ya quitados |

**Conclusión:** casi todo lo investigado en los dos documentos anteriores sirve para **estabilidad, seguridad o información**, no para subir FPS. Las únicas palancas nuevas que pueden subir FPS de forma directa son DXVK (en ciertos casos) y la resolución interna; las dos están **sin medir** aquí.

## Palancas ordenadas por lo que pueden mover

### 1. Lo que cambia el usuario en su equipo (lo que más mueve en una APU)

VegaFlow no puede cambiarlo, pero sí detectarlo y explicarlo. En una APU suele pesar más que cualquier ajuste de software.

| Palanca | Por qué | Fuente | Confianza | Qué hace VegaFlow |
|---|---|---|---|---|
| **Memoria en doble canal** | La gráfica integrada usa la RAM del sistema; un solo canal es la mitad de ancho de banda. AMD lo llamó "absolutamente obligatorio" para sus APU 8000G | [Tom's Hardware](https://www.tomshardware.com/pc-components/cpus/amd-says-dual-channel-ddr5-6000-is-the-sweet-spot-for-ryzen-8000g-apus), [TechSpot](https://www.techspot.com/news/101505-amd-recommends-pairing-ryzen-8000g-apus-dual-channel.html) (entrevista de PCWorld a AMD) | Media (prensa que cita a AMD; habla de las 8000G) | **Informar** en Equipo (fase 3), con el texto prudente de [`recursos-de-windows.md`](recursos-de-windows.md) |
| **Velocidad de la memoria** (perfil XMP o EXPO activado en la BIOS) | Misma razón: más ancho de banda para la gráfica integrada | Mismas fuentes (AMD recomienda DDR5-6000 para las 8000G) | Media | **Informar** la velocidad configurada frente a la nominal del módulo (`Win32_PhysicalMemory`: `ConfiguredClockSpeed` y `Speed`); pendiente comprobar que esos campos existen y son fiables |
| **Memoria reservada para la gráfica (UMA) en la BIOS** | — | Ya en el plan, §10 | — | Fuera del plan |
| **Enchufado y modo de energía "Mejor rendimiento"** | En algunos equipos el fabricante ata este modo a los límites de potencia del procesador (PL1/PL2) y al ventilador | [Surface Book 3](https://learn.microsoft.com/surface/surface-book-gpu-overview) (un fabricante concreto) | Media para Surface / sin verificar para el resto | **Informar** el modo efectivo (API documentada, ver [`recursos-de-windows.md`](recursos-de-windows.md)). Cambiarlo solo es posible con `PowerSetActiveOverlayScheme`, que **no está documentada** ([Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/3919778/changing-overlay-power-mode-with-script)): experimental |

### 2. Resolución interna (la palanca de software que más mueve en una gráfica justa)

Si la gráfica va al límite, dibujar menos píxeles y escalar es lo que más FPS da. A cambio, la imagen pierde nitidez: es una decisión del usuario, nunca un valor por defecto.

| Palanca | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Unreal: `r.ScreenPercentage` con escalado temporal (TAA upsampling o TSR) dibuja a menos resolución y reconstruye la imagen | [Epic, Screen Percentage](https://dev.epicgames.com/documentation/en-us/unreal-engine/screen-percentage-with-temporal-upscale-in-unreal-engine), [Epic, TSR](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine) | Alta (la variable) / depende de la versión del motor | El preset UMA no fija la resolución, pero **borra** las líneas `r.ScreenPercentage` y `r.TemporalAA.Upsampling` que haya en el archivo, aunque las haya puesto el usuario (`src/optimize/steam/engine_presets.cpp:161`); no hay documento que explique por qué. Propuesta: opción **visible y por juego** "Resolución interna" con ⓘ ("dibuja a menos píxeles: más FPS, imagen menos nítida"), apagada por defecto, en juegos sin anti-cheat. Medir antes con A/B |
| Radeon Super Resolution (escalado del driver de AMD) | [TechPowerUp](https://techpowerup.com/293006/amd-unveils-radeon-super-resolution-brings-performance-improvements-to-thousands-of-games), [análisis de TechPowerUp](https://techpowerup.com/review/amd-radeon-super-resolution-rsr) | Media | **No sirve en Vega** (solo RDNA en adelante). En gráficas RDNA: informar de que existe en el panel de AMD |
| FSR dentro del juego | Depende del juego | — | Nada que hacer desde fuera; como mucho, mencionarlo en el ⓘ |

### 3. Estabilidad: menos tirones y mínimos más altos

| Palanca | Por qué | Fuente | Confianza | Qué hace VegaFlow |
|---|---|---|---|---|
| **Limitar los FPS** un poco por debajo de lo que el equipo aguanta (Radeon Chill o FRTC de AMD, o el limitador del juego) | AMD lo presenta para bajar consumo, calor y ruido | [AMD, Radeon Chill](https://www.amd.com/en/products/software/adrenalin/radeon-software-chill.html), [AMD DH-033](https://www.amd.com/en/resources/support-articles/faqs/DH-033.html) | Alta (consumo) / sin verificar (que dé más estabilidad, o que en una APU la energía que sobra vaya a la CPU) | **Sugerir** en el ⓘ; VegaFlow no inyecta limitadores (anti-cheat). Prueba A/B en el modo desarrollador |
| **No borrar las cachés de sombreadores** | Al borrarlas, el juego vuelve a compilar y tira más | [`apis-graficas-dx-y-vulkan.md`](apis-graficas-dx-y-vulkan.md) | Baja | Nunca borrarlas sin que lo pida el usuario |
| **Precompilación de sombreadores de Steam** (activada por defecto) | Steam descarga sombreadores ya compilados para juegos OpenGL y Vulkan | [Phoronix](https://www.phoronix.com/news/Steam-Shader-Pre-Caching), [ComputerBase](https://www.computerbase.de/2017-12/steam-shader-pre-caching/) | Baja (prensa de 2017, sin página de Valve) | **Informar** si está apagada (pendiente: dónde lo guarda Steam) |
| **Caché de sombreadores del driver de AMD** forzada por registro | — | La clave de `src/optimize/graphics/shader_cache.cpp` no tiene fuente de AMD | Sin verificar | Pasa a experimental hasta encontrar fuente o medirlo |
| **Capas Vulkan huérfanas** | Una capa rota provoca fallos en todos los juegos Vulkan | [`apis-graficas-dx-y-vulkan.md`](apis-graficas-dx-y-vulkan.md) | Media | Informar (fase 3) |
| **Modo Juego de Windows activado** | Dicen que aplaza instalaciones de drivers y avisos de reinicio de Windows Update mientras se juega | Solo guías de terceros; no se encontró la página de Microsoft | Baja | Informar si está apagado; no cambiarlo |
| **Confinar el fondo** | Con 4 workers en el menú limitado a 60 FPS, los cuadros tardíos bajaron de 1,8 % a 0 % | Medido aquí | Alta | Ya existe; la fase 5 lo amplía por tipo de programa |

### 4. Traducción de API (puede subir FPS o bajarlos)

| Caso | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| DXVK en juegos DX11 con una APU AMD | Sin fuente oficial | Sin verificar | **Medir primero** con Octopath Traveler II (D3D11, procedimiento autorizado) |
| DXVK en juegos DX9 con Intel Arc o Xe | [`apis-graficas-dx-y-vulkan.md`](apis-graficas-dx-y-vulkan.md) | Media | Experimental |

## Lo que el preset UMA escribe y la investigación no respalda

El bloque del preset (`src/optimize/steam/engine_presets.cpp:29-63`) incluye tres variables que [`graficas-y-perfil-del-equipo.md`](graficas-y-perfil-del-equipo.md) ya marcó:

- `r.ShaderPipelineCache.AsyncCompileRate=16`: no aparece en ninguna fuente (descartada).
- `r.Streaming.AmortizeCPUToGPUCopy=0`: solo fuente de terceros (experimental).
- `r.GTSyncType=1`: cambia la latencia y no está medida (no va por defecto).

Hay que quitarlas o separarlas en la fase 3 antes de volver a medir el preset, para saber qué parte del +40 % aporta cada cosa.

## Qué medir primero

Cada prueba con el procedimiento de siempre: A/B intercalado ABBA, `scripts/bench.ps1`, juego en el escritorio, al menos 3 corridas por condición, misma máquina. Ordenadas por lo que se espera que muevan:

1. **Resolución interna** en Octopath Traveler II: 100 % frente a 75 %, con escalado temporal.
2. **DXVK frente a DX11 nativo** en el mismo juego (GPLAsync ya está sin medir).
3. **Limitador de FPS**: sin límite frente a un límite un poco por debajo de la media; comparar el 1 % Low y los cuadros tardíos, no solo la media.
4. **Prioridad del juego**: medida sin carga el 2026-11-09, sin diferencia ([`estado-de-verificacion.md`](../mediciones/estado-de-verificacion.md)); falta con carga de fondo.
5. **Memoria**: compresión encendida frente a apagada, y vaciar el fondo frente a no vaciarlo, con 8 GB ocupados.

Lo que salga de cada prueba va a `docs/mediciones/` con fecha, y la palanca cambia de destino según el resultado: si sube, "Recomendado" con su ⓘ; si no, experimental o se quita.

## Qué cambia en el plan si se adopta

- **Fase 3:** informar de doble canal, velocidad de la RAM, modo de energía efectivo, precompilación de Steam y Modo Juego; separar en el preset UMA las tres variables sin respaldo y explicar en su ⓘ que baja calidad visual.
- **Fase 8:** la clave de caché de sombreadores de AMD pasa a experimental; nueva opción por juego "Resolución interna", apagada por defecto.
- **Modo desarrollador:** las cinco pruebas A/B de arriba, guiadas.

Nada de esto está aprobado en el plan: es investigación.
