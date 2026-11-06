# Medición base (sin tweaks)

Banco: `scripts/bench.ps1` con PresentMon 2.x, 60 s por corrida, 5 s de calentamiento.

- Equipo: laptop Ryzen 5 4600H, Radeon Vega 6, 16 GB, Windows 11 build 26300.
- Juego: Octopath Traveler II (`Octopath_Traveler2-Win64-Shipping.exe`), escena fija "Pueblo fantasma" (Tierras Radiantes), personaje quieto, con el overlay de monitoreo del equipo activo.
- Modo de presentación medido: `Hardware: Legacy Flip` en el 100% de los cuadros.
- Fecha: 2026-11-03.

## Corridas limpias (juego en primer plano, sin tocar nada)

| Corrida | Cuadros | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- | --- |
| baseline-1 | 2375 | 43.21 | 37.84 | 35.07 | 0.693 |
| baseline-2 | 2379 | 43.28 | 38.45 | 36.72 | 0.681 |
| baseline-3 | 2370 | 43.13 | 38.30 | 36.52 | 0.662 |

Rango entre corridas: FPS medio 0.15 (0.35%), 1% Low 0.61 (1.6%), 0.1% Low 1.65 (4.7%).
Un tweak solo se puede atribuir si supera ese ruido en la métrica que afirma mejorar.

## Corridas contaminadas (descartadas)

Con una ventana de escritorio delante del juego, o con cambios de ventana durante la captura:

| Corrida | FPS medio | 1% Low | 0.1% Low | Desv. (ms) | Modo |
| --- | --- | --- | --- | --- | --- |
| contaminated-2 | 42.23 | 15.88 | 3.42 | 10.03 | `Composed: Flip` |
| contaminated-3 | 42.92 | 29.65 | 11.85 | 3.54 | `Legacy Flip` (parcial) |
| contaminatedB-2 | 42.76 | 21.05 | 5.09 | 7.30 | `Legacy Flip` (parcial) |
| contaminatedB-3 | 42.98 | 30.95 | 13.42 | 2.96 | `Legacy Flip` (parcial) |

El FPS medio casi no cambia, pero los lows se destruyen. Cualquier ventana propia (GUI, overlay) delante de un juego en `Legacy Flip` hay que medirla como una variable más.

## Temporizador NT a 0.5 ms (proceso aparte)

Condición: `scripts/hold_timer.ps1` pidió 5000 (0.5 ms) en un proceso separado durante las 3 corridas. El propio proceso leyó `current_after=5000` (mínimo 156250, máximo 5000). Mismas condiciones y escena que la línea base.

| Corrida | FPS medio | 1% Low | 0.1% Low | Desv. (ms) |
| --- | --- | --- | --- | --- |
| timer-1 | 43.18 | 38.54 | 36.97 | 0.671 |
| timer-2 | 43.19 | 38.56 | 35.90 | 0.658 |
| timer-3 | 43.16 | 38.75 | 37.59 | 0.621 |
| media timer | 43.18 | 38.62 | 36.82 | 0.650 |
| media base | 43.21 | 38.20 | 36.10 | 0.679 |

Diferencia (timer menos base): FPS medio -0.07%, 1% Low +1.1%, 0.1% Low +2.0%, desviación -4%.
Todas dentro del ruido entre corridas de la línea base (1.6% en 1% Low, 4.7% en 0.1% Low). No hay efecto demostrable.

Límites de esta prueba:
- Una sola escena, limitada por GPU (99%); un temporizador fino solo debería importar en escenas limitadas por CPU o con muchos `Sleep` cortos.
- Solo 3 corridas por condición, y las del temporizador se midieron después de las de base (deriva térmica no controlada).
- No se verificó que el proceso del juego reciba la resolución; solo que el proceso solicitante la ve.

## Estado del sistema durante las mediciones

Todas las corridas anteriores se midieron con tweaks de ejecuciones antiguas del daemon ya aplicados en la laptop (lectura con `scripts/tweaks_state.ps1`, 2026-11-03). La línea base no es un Windows de fábrica.

Distintos del valor de fábrica:

| Valor | Actual | Fábrica |
| --- | --- | --- |
| `OverlayTestMode` (MPO) | 5 | ausente |
| `DisablePagingExecutive` | 1 | 0 |
| `SystemResponsiveness` | 10 | 20 |
| `NetworkThrottlingIndex` | 0xFFFFFFFF | 10 |
| Tasks\Games `Priority` | 6 | 2 |
| Tasks\Games `Scheduling Category` | High | Medium |
| Tasks\Games `SFIO Priority` | High | Normal |
| `TcpAckFrequency`, `TCPNoDelay` | 1 | ausente |
| `NtfsMemoryUsage` | 2 | ausente |
| `KMD_EnableSAM`, `LargePageSupport` | 1 | ausente |

Sin diferencia con la fábrica (no hacen nada): `GPU Priority` 8, `Clock Rate` 10000, `Win32PrioritySeparation` 2, `LargeSystemCache` 0.

También activos (sin valor de fábrica confirmado, solo informe): `EnableUlps=0`, `ShaderCache`, `UMD\Tessellation=16` y otros valores `UMD`, 13 entradas en `UserGpuPreferences`, perfil de energía APU.

Varios de estos tweaks ya no existen en el código, así que el daemon actual no los revierte.
`scripts/tweaks_state.ps1 -Restore` simula la restauración (12 cambios); con `-Apply` la ejecuta y requiere consola de administrador. Algunos valores (DWM, paginación del kernel) solo se releen tras reiniciar sesión o reiniciar.

## Restauración a valores de fábrica

2026-11-03 00:43: la tarea `VegaFlow-Restore` (elevada, `C:\ProgramData\VegaFlow`) aplicó 12 cambios con código de salida 0. Una lectura posterior con `scripts/tweaks_state.ps1` mostró los 16 valores controlados en su valor de fábrica.

Pendiente tras la restauración: reiniciar Windows para que DWM y la paginación del kernel relean sus valores, y medir una línea base de Windows limpio. Las mediciones anteriores de esta página se hicieron antes de la restauración.

Detalle del mecanismo: la primera ejecución falló con código 1 porque `IsInRole('Administrator')` busca el nombre en inglés y en Windows en español el grupo es "Administradores"; el script lo tomó por no administrador y no escribió nada. Se corrigió usando `WindowsBuiltInRole::Administrator`.

Siguen sin tocarse (sin valor de fábrica confirmado): `EnableUlps`, `ShaderCache`, `UMD\*`, `UserGpuPreferences`, perfil de energía APU, compresión de memoria.

## Windows limpio tras restaurar y reiniciar

2026-11-03 00:51, laptop reiniciada con los 16 valores en fábrica (lectura posterior al reinicio: 0 diferencias). Misma escena y protocolo manos libres. La app de EA arrancó con la sesión y se minimizó antes de medir.

| Corrida | FPS medio | 1% Low | 0.1% Low | Desv. (ms) |
| --- | --- | --- | --- | --- |
| stock-1 | 43.30 | 28.04 | 17.86 | 1.843 |
| stock-2 | 43.17 | 36.33 | 33.43 | 0.858 |
| stock-3 | 43.42 | 38.74 | 32.31 | 0.661 |

Comparado con la línea base anterior (tweaks viejos aplicados):

- FPS medio: 43.30 contra 43.21. Sin diferencia.
- 1% Low: media 34.4 (rango 10.7) contra 38.2 (rango 0.6). Sin `stock-1`: 37.5, dentro del ruido de la línea anterior.
- 0.1% Low: media 27.9 (rango 15.6) contra 36.1 (rango 1.7). Sin `stock-1`: 32.9.

No se puede concluir que los tweaks viejos mejoren los lows. La dispersión de las corridas limpias es unas 10 veces mayor que la de la línea anterior, y `stock-1` (la primera tras el arranque) es un valor atípico. Causa no verificada: actividad de fondo posterior al arranque (EA, indexado, shaders) o la propia restauración. Antes de comparar hay que repetir la medición con la máquina asentada y más corridas.

## Windows limpio, máquina asentada (corridas 4 a 8)

Cinco corridas más, 00:57 a 01:05, sin tocar la laptop. Todas con el 100% de los cuadros en `Hardware: Legacy Flip`.

| Corrida | FPS medio | 1% Low | 0.1% Low | Desv. (ms) |
| --- | --- | --- | --- | --- |
| stock-4 | 43.49 | 39.44 | 37.37 | 0.641 |
| stock-5 | 43.55 | 40.03 | 37.23 | 0.564 |
| stock-6 | 43.52 | 39.75 | 36.07 | 0.582 |
| stock-7 | 43.52 | 39.25 | 36.30 | 0.598 |
| stock-8 | 43.56 | 39.83 | 37.05 | 0.566 |
| media | 43.53 | 39.66 | 36.80 | 0.590 |
| media con tweaks viejos | 43.21 | 38.20 | 36.10 | 0.679 |

Diferencia (limpio menos tweaks viejos): FPS medio +0.7%, 1% Low +3.8%, 0.1% Low +1.9%, desviación -13%.

Las primeras tres corridas tras el reinicio (`stock-1` a `stock-3`) fueron peores y más dispersas, y se estabilizaron a partir de la cuarta, unos 6 minutos después. Una medición justa necesita descartar los primeros minutos tras arrancar.

Lectura: sin los tweaks viejos, el resultado no es peor y puede ser algo mejor. El 1% Low supera el rango entre corridas de la línea anterior (1.6%) y el de estas cinco (2%); el resto está dentro del ruido.

Límites:
- Las dos series no se intercalaron: la anterior se midió con la máquina encendida desde hacía horas y esta tras un reinicio. Deriva térmica y estado del sistema no controlados.
- Una sola escena, limitada por GPU, y un solo juego.
- No se puede atribuir la diferencia a un tweak concreto; es el conjunto de 12 valores contra ninguno.

## Temporizador NT: ¿lo hereda otro proceso?

Prueba sin administrador y sin el juego (2026-11-03 09:36 a 09:38). `scripts/sleep_probe.ps1` mide 1000 `Sleep(1)` en un proceso; `scripts/hold_timer.ps1` pide 0.5 ms desde otro proceso. Tres rondas alternadas, sin y con el temporizador. En las tres rondas "con", el proceso externo registró `set_status=0` y `current_after=5000`.

| Ronda | Sin temporizador (media) | Con temporizador externo (media) |
| --- | --- | --- |
| 1 | 15.487 ms | 15.376 ms |
| 2 | 15.501 ms | 15.385 ms |
| 3 | 15.499 ms | 15.389 ms |

p99 de 16.0 a 16.5 ms en ambos casos.

Resultado: `Sleep(1)` sigue en ~15.4 ms con la resolución pedida por otro proceso. La diferencia de 0.11 ms es constante pero irrelevante frente a los ~15.6 ms de la granularidad por defecto; no se acerca a 1 ms ni a 0.5 ms. Una resolución de temporizador pedida por el daemon no cambia la planificación de otro proceso en esta máquina (Windows 11), así que no puede mejorar el juego. Esto complementa la medición de FPS, que ya no mostraba efecto.

Límite: solo se probó un proceso que no es el juego; no se midió el `Sleep` dentro del propio juego.

## Observaciones

- El juego se presenta en `Legacy Flip`, no en borderless. Los tweaks de DirectFlip y MPO apuntan a otro modo de presentación y no se pueden evaluar con este juego tal como está configurado.
- Escena limitada por GPU (GPU 99%, ~43 FPS).
- `bench.ps1` marca `clean: true` cuando al menos el 99% de los cuadros comparten modo de presentación. `baseline-1` es anterior a esa marca y se considera limpia por protocolo.

## Ruido de línea base por escena, 2026-11-05 (Fase 1 del plan de pruebas)

Octopath Traveler II, D3D11, `Hardware: Independent Flip` en todas las capturas, ajustes de fábrica, daemon apagado, ruido ambiental de `ryzenadj.exe` (se cae cada ~20 s) presente en todas. Cada lanzamiento del juego: una captura de calentamiento que no cuenta y 5 capturas de 120 s de datos (`Seconds=125`, 5 s descartados). 3 lanzamientos por escena, 30 capturas en total, sin ventanas delante del juego.

Clase de cada escena según el CSV de PresentMon (mediana en ms):

| Escena | `MsBetweenPresents` | `MsCPUBusy` | `MsGPUBusy` | Lectura |
| --- | --- | --- | --- | --- |
| Mundo (autoguardado, el personaje quieto) | 23.16 | 5.37 | 22.94 | limitada por GPU: la GPU ocupa casi todo el cuadro |
| Menú principal (Continuar resaltado) | 16.67 | 13.98 | 7.01 | con tope de 60 FPS; la GPU va holgada y la CPU ocupa el 84% del cuadro |

Mundo, FPS medio y 1% Low (mediana y rango de las 5 capturas; 1% Low agrupado de los cuadros de las 5):

| Lanzamiento | FPS medio | 1% Low | 1% Low agrupado |
| --- | --- | --- | --- |
| 1 | 43.10 [43.10-43.12] | 39.14 [38.67-39.35] | 39.08 |
| 2 | 43.20 [43.18-43.22] | 39.05 [39.00-39.56] | 39.22 |
| 3 | 43.15 [43.12-43.15] | 39.43 [38.44-39.71] | 39.24 |

Menú:

| Lanzamiento | FPS medio | 1% Low | 1% Low agrupado |
| --- | --- | --- | --- |
| 2 | 60.00 [59.99-60.00] | 52.59 [51.44-53.08] | 52.31 |
| 3 | 60.00 [59.99-60.00] | 52.26 [51.31-52.90] | 52.13 |
| 4 | 60.00 [60.00-60.00] | 52.25 [51.30-53.01] | 52.21 |

Ruido medido:

- FPS medio del mundo: rango de 0.05 a 0.09% dentro de un lanzamiento y 0.23% entre medianas de lanzamientos (43.10, 43.20, 43.15). Es 13 veces menor que el umbral de 3% del plan, así que el umbral se baja a 1%.
- 1% Low del mundo: rango de 1.4 a 3.3% entre capturas de 120 s de un lanzamiento, contra 18 a 59% con capturas de 30 s (las tres series de 30 s de esta misma escena). Agrupando los cuadros el rango entre lanzamientos baja a 0.41%.
- Control nulo (lanzamiento contra lanzamiento, sin nada cambiado): el 1% Low agrupado difiere entre +0.04 y +0.41% y los tres intervalos de confianza del 95% por remuestreo de bloques de un segundo incluyen 0 (de -1.1 a +1.8%). El umbral del 1% Low agrupado se baja de 5% a 3% (el doble de la semianchura de esos intervalos).
- Desviación del frametime: rango de 26% entre medianas de lanzamientos (0.53 a 0.68 ms) y de 19 a 42% entre capturas de un lanzamiento en la desviación del tiempo en pantalla. No sirve como métrica de veredicto salvo con diferencias mucho mayores (con carga de fondo pasó de 4.6 a 2.3 ms).
- Tirones (cuadros sobre 2 veces la mediana): 0 por minuto en el mundo y como máximo 0.5 por minuto en el menú. Cuadros tardíos (tiempo en pantalla sobre 1.5 veces la mediana): 0.0025% de media en el mundo y 0.0056% en el menú, máximo 0.028%.
- El menú va a 60.00 FPS en todas las capturas: sin carga no tiene margen de mejora. Solo discrimina bajo carga de fondo, donde el porcentaje de cuadros tardíos puede subir.
- La luz del mundo cambió de día (lanzamiento 1) a noche (lanzamientos 2 y 3) sin efecto visible en el FPS medio (43.10 contra 43.20 y 43.15).

Estado del motor, descubierto el 2026-11-05 después de estas mediciones: el `Engine.ini` de Octopath (`%LOCALAPPDATA%\Octopath_Traveler2\Saved\Config\WindowsNoEditor\`) lleva el preset UMA de VegaFlow desde el 2026-10-29 (`r.Streaming.PoolSize=3000`, `LimitPoolSizeToVRAM=0`, `FullyLoadUsedTextures=1`, `FramesForFullUpdate=1`, `MaxNumTexturesToStreamPerFrame=100`, `AmortizeCPUToGPUCopy=0`, `Boost=1.5` y `r.RDG.AsyncCompute=0`), con su copia `Engine.ini.vegaflow.bak`. `tweaks_state.ps1` solo mira el registro y no lo ve. Todas las capturas de Octopath de este documento y de `MEDICION_AB_DAEMON.md`, con daemon apagado y encendido, corrieron con ese preset, así que la línea base de motor no es la de fábrica; la comparación A/B no se ve afectada porque el archivo es igual en las dos condiciones. La fecha de modificación del archivo (2026-11-05 16:29) coincide con un arranque del juego con el daemon apagado, así que lo reescribe el propio juego.

### Palanca: preset UMA del `Engine.ini` contra el original (2026-11-05)

Mundo "Pueblo fantasma", daemon apagado, un lanzamiento con el `Engine.ini.vegaflow.bak` (original) y 5 capturas de 120 s, contra las 15 capturas de Phase 1 con el preset (3 lanzamientos). Umbrales del plan, fijados antes.

| Condición | FPS medio | 1% Low agrupado |
| --- | --- | --- |
| Preset UMA (A) | 43.15 [43.10-43.22] | 39.17 |
| Original (B) | 30.72 [30.69-30.74] | 28.19 (IC95 del cambio -29.0% a -27.1%) |

El preset da +40% de FPS medio y +39% de 1% Low en este escena, y no cambia el modo de presentación (Independent Flip), 0 errores y 0 tirones en las dos. Cuenta contra los umbrales.

Límites: no está intercalado (el preset viene de lanzamientos anteriores del mismo día y el original es un solo lanzamiento; el ruido entre lanzamientos medido es 0.23%, muy por debajo de la diferencia), escena limitada por GPU, un solo juego y motor UE4. No aísla qué clave del preset causa la ganancia (la sospecha es `r.RDG.AsyncCompute=0`, coherente con la sección 2.4 de CLAUDE.md, pero no está probado). El archivo quedó restaurado y se comprobó idéntico.


Límites: una sola máquina, un solo juego y 2 escenas; el mundo es una escena estática con el personaje quieto, así que no incluye streaming ni movimiento. Los umbrales nuevos se fijaron con estos datos antes de medir ninguna palanca.
