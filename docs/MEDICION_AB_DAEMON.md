# Medición A/B: daemon apagado contra encendido (bgfx drawstress)

Fecha: 2026-11-04. Equipo: laptop Ryzen 5 4600H, Radeon Vega 6, 16 GB, Windows 11 build 26300, enchufada.

## Método

- Carga: bgfx `17-drawstress`, `dim` 30 (27 000 draw calls por cuadro), ventana en el escritorio, modo de presentación `Hardware: Independent Flip` en el 100% de los cuadros.
- Medición: `scripts/bench.ps1` (PresentMon 2.6, 30 s por captura, 5 s de calentamiento descartados). Un solo `drawstress` abierto por API; el daemon se arranca y se para entre capturas.
- Orden ABBA (`scripts/ab_loop.ps1`): apagado/encendido en la repetición 1, encendido/apagado en la 2, y así. Esto reparte la deriva térmica entre las dos condiciones.
- "Encendido" es `vegaflow_daemon.exe` en modo continuo, sin argumentos, lanzado con `scripts/ab_profile.ps1 -Start`, con 10 s de espera antes de capturar. Se paró con el evento `Global\VegaFlowStop` (cierre limpio, código 0).
- Antes de cada captura se leyó `tweaks_state.ps1`: ningún valor de registro vigilado estaba fuera de fábrica en ninguna captura (`ab_runs.log`, `desviado=[]`).
- La suite de pruebas se ejecutó antes de las mediciones; su efecto sobre los valores `UMD\*` del driver está en `PLAN_REDISENO_Y_ACTUALIZACION.md`.

## Resultados (5 capturas por condición y API)

Media y rango [mín–máx].

| API | Condición | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- | --- |
| D3D11 | daemon apagado | 125.8 [124.6–126.5] | 86.3 [85.2–87.3] | 80.8 [78.4–81.8] | 0.86 |
| D3D11 | daemon encendido | 112.3 [109.2–114.0] | 87.2 [86.6–87.7] | 82.0 [80.3–83.9] | 1.45 |
| D3D12 | daemon apagado | 99.6 [99.5–99.8] | 86.8 [84.4–89.2] | 79.7 [77.7–82.1] | 0.26 |
| D3D12 | daemon encendido | 101.7 [101.6–101.8] | 91.3 [88.2–93.5] | 81.1 [70.0–89.5] | 0.25 |

Lectura:

- D3D11: el daemon encendido baja el FPS medio un 10.7%. Los rangos no se solapan y el orden ABBA descarta la deriva como causa. La desviación del frametime sube de 0.86 a 1.45 ms. El 1% Low y el 0.1% Low no empeoran.
- D3D12: el FPS medio sube un 2.1% (rangos sin solape) y el 1% Low un 5%; el 0.1% Low es irregular (de 70.0 a 89.5), sin efecto atribuible.
- Con 5 capturas por celda y una sola carga, el resultado de D3D12 es pequeño y no se puede extender a otras cargas.

## Causa del coste en D3D11

El daemon cambia cuatro ajustes del plan de energía al arrancar, medido con `powercfg /qh` (incluye los ocultos) antes y después; no cambia ningún valor de registro vigilado:

| Ajuste | Fábrica | Daemon |
| --- | --- | --- |
| Mínimo de núcleos sin aparcar (`CPMINCORES`) | 50 | 100 |
| Preferencia de rendimiento energético (`PERFEPP`) | 50 | 25 |
| Modo de boost (`PERFBOOSTMODE`) | 2 (Agresiva) | 4 (Eficiencia agresiva) |
| PCIe ASPM | 1 | 0 |

Aislamiento, todo en D3D11 con `drawstress`:

| Prueba | Resultado |
| --- | --- |
| Daemon encendido, devolver los cuatro ajustes a fábrica contra dejarlos como los pone el daemon, 4 capturas ABBA | 126.0 [125.4–126.2] contra 110.9 [107.1–113.7] FPS: los cuatro ajustes explican todo el coste |
| Daemon apagado, un ajuste por vez a su valor del daemon, 3 rondas rotadas (base, park, epp, aspm) | base 126.2 [124.3–127.3]; **park 99.4 [98.4–100.0]**; epp 126.1 [125.6–126.8]; aspm 126.5 [126.2–127.1] |
| Daemon apagado, solo el modo de boost 2 contra 4, 4 capturas ABBA | 124.5 contra 123.2 FPS (-1.1%) |
| Intervalo de sondeo del daemon 1500 ms contra 60 000 ms, 3 capturas ABBA | 95.0–95.6 en ambos: el sondeo no influye |
| Deriva térmica | Línea base sin daemon 3 capturas seguidas: 124.4, 126.4, 126.0 FPS, procesador a 3.0 GHz |

Conclusión: `CPMINCORES=100` (todos los núcleos sin aparcar) baja el FPS de `drawstress` D3D11 un 21% por sí solo. El modo de boost aporta cerca de un 1%; la EPP y el ASPM no tienen efecto medible en esta carga. El mecanismo no está medido: puede ser reparto de hilos entre núcleos o presupuesto de potencia compartido con la iGPU, ninguno verificado.

Sin explicar: el nivel con el daemon encendido varió entre sesiones de medición, con las mismas condiciones nominales. Fue ~112 FPS en el bucle ABBA y ~111 en la prueba de cuatro ajustes, pero ~95 FPS en la prueba del intervalo de sondeo y en la de revertir solo boost y ASPM. Con el daemon apagado el nivel se mantuvo en ~125 todo el tiempo. No se identificó qué cambió entre esas sesiones; hasta saberlo, el tamaño exacto del coste del daemon (entre -11% y -24%) no está fijado, aunque la dirección sí.

## Verificación tras la corrección

Corrección (`842df5b`): el perfil APU deja `CPMINCORES` en el valor de fábrica (50) y el cierre limpio del daemon restaura siempre el perfil de energía. Mismo método ABBA, 5 capturas por celda, daemon recompilado en el laptop (26 de 26 pruebas en verde).

| API | Condición | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- | --- |
| D3D11 | daemon apagado | 123.6 [123.4–124.1] | 86.3 [85.7–86.8] | 80.9 | 0.65 |
| D3D11 | daemon encendido | 123.6 [123.0–124.0] | 86.2 [85.0–87.2] | 81.2 | 0.66 |
| D3D12 | daemon apagado | 102.8 [102.6–103.0] | 89.8 [87.3–93.1] | 80.6 [71.9–86.0] | 0.27 |
| D3D12 | daemon encendido | 102.8 [102.5–103.1] | 90.0 [87.3–91.7] | 82.5 [79.1–86.5] | 0.27 |

El coste en D3D11 (-10.7%) desaparece y la ganancia aparente en D3D12 (+2.1%) también: con la corrección, las partes globales del daemon encendido no cambian el resultado de `drawstress` en ninguna de las dos APIs (ver "Alcance" abajo). La ganancia anterior en D3D12 venía de la combinación de ajustes de energía, no de una optimización del daemon.

Alcance: en estas corridas el daemon se lanzó desde una sesión SSH, que es la sesión 0 de Windows, sin acceso al escritorio. El detector usa `GetForegroundWindow` y `EnumWindows`, que solo ven las ventanas de la sesión del proceso, así que no detectó ningún juego y actuaron únicamente sus partes globales (plan de energía, `GameConfigStore`). Las optimizaciones por proceso (CPU Sets, EcoQoS, prioridad de I/O) no se midieron. Comprobado con `--once --dry-run` y `drawstress.exe` abierto: desde la sesión 0 dice "No foreground 3D game currently detected"; desde la sesión del escritorio dice "Active 3D Game Detected: drawstress.exe", "Render API: DirectX 11", 18 procesos de fondo a aislar y 8 núcleos para el juego.

Resultado válido solo para las partes globales del daemon. Medir las optimizaciones por proceso exige que el daemon corra elevado en la sesión del escritorio (tarea programada interactiva `VegaFlow-Start` de `scripts/setup_tasks.ps1`, que requiere un UAC del usuario una vez).

## Optimizaciones por proceso: daemon en la sesión del escritorio

Daemon lanzado con la tarea interactiva `VegaFlow-Start` (sesión 1, elevado), con el juego abierto en el escritorio, mismo método ABBA. El daemon sí detecta el juego: `session_reports.log` registra la sesión (`sesion_registrada=True`) y `--once --dry-run` lista 18 procesos de fondo y 8 núcleos para el juego. Corrección de partida: `CPMINCORES` ya queda en 50. Carga: `drawstress` `dim` 30.

| API | Condición | Capturas | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- | --- | --- |
| D3D11 | daemon apagado | 5 | 122.5 [121.4–123.1] | 82.4 | 74.2 | 0.99 |
| D3D11 | daemon encendido | 5 | 99.4 [96.2–102.5] | 69.1 | 55.2 | 1.66 |
| D3D12 | daemon apagado | 3 | 99.1 [98.3–99.5] | 82.3 | 70.7 | 0.38 |
| D3D12 | daemon encendido | 4 | 99.8 [99.7–99.9] | 80.7 | 73.3 | 0.38 |

- D3D11: con las optimizaciones por proceso el FPS medio baja un 18.9%, el 1% Low un 16% y el 0.1% Low un 26%, y la desviación del frametime sube un 69%. Los rangos de FPS no se solapan.
- D3D12: sin efecto fuera del ruido (+0.7%). La carga está limitada por la GPU a unos 100 FPS.
- Durante las capturas con el daemon encendido, `Win32PrioritySeparation` pasa de 2 a 38 o 40 y `PERFEPP` a 25 (`ab_runs.log`).
- D3D12 tiene solo 3 y 4 capturas porque el primer intento se interrumpió por una carrera del script (espera por el proceso en vez de por las tareas), corregida; las capturas obtenidas son válidas.
### Causa y corrección

Aislamiento con el daemon apagado, D3D11, 3 rondas rotadas (`Win32PrioritySeparation` por registro y restricción del juego con máscara de afinidad de 8 de los 12 hilos):

| Condición | FPS medio | 1% Low | 0.1% Low |
| --- | --- | --- | --- |
| base | 123.3 [123.0–123.6] | 84.1 | 76.8 |
| `Win32PrioritySeparation` 40 | 123.7 [123.4–124.0] | 82.6 | 71.8 |
| juego limitado a 8 de 12 hilos | 93.1 [89.8–99.6] | 77.2 | 70.4 |
| ambas | 90.5 [90.3–90.7] | 75.2 | 69.9 |

La prioridad del planificador no tiene efecto. Limitar el juego a 8 de los 12 hilos le quita un 24.5% de FPS en esta carga multihilo limitada por CPU. La afinidad no es exactamente un CPU Set, así que se confirmó con el daemon mismo.

Corrección (`bcf360c`): opción `restrict_game_cpu_sets` en la configuración del daemon, apagada por defecto. El juego conserva todos los hilos y solo se confina el fondo. Repetición del A/B ABBA de D3D11, 5 capturas por condición, mismo método:

| D3D11 | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- |
| daemon apagado | 120.0 [118.3–120.9] | 81.3 | 72.8 | 1.18 |
| daemon encendido, juego sin restringir | 120.4 [116.6–122.5] | 80.1 | 73.1 | 1.13 |

Daemon encendido igual que apagado (+0.3% en FPS, dentro del ruido; el 1% Low y el 0.1% Low también). El coste del -18.9% venía por completo de confinar al juego. Lo que queda activo (EcoQoS e I/O del fondo, prioridad de I/O del juego) no mejora ni empeora `drawstress`: sin beneficio demostrado.

Límites: una sola carga sintética sin descargas de disco ni procesos de fondo pesados; el beneficio del aislamiento de fondo (EcoQoS, I/O) se mediría con una carga de fondo real, que no se probó.

### Aislamiento de fondo con carga real

Misma carga `drawstress` D3D11 `dim` 30 y mismo método ABBA (5 capturas por condición, juego sin restringir, daemon en la sesión del escritorio), pero con una carga de fondo en las dos condiciones: Chrome sin ventana con 6 workers quemando CPU (unos 6 de los 12 hilos ocupados). `chrome.exe` está en la lista de procesos ruidosos del daemon.

| D3D11 con carga de fondo | FPS medio | 1% Low | 0.1% Low | Desv. frametime (ms) |
| --- | --- | --- | --- | --- |
| daemon apagado | 51.0 [47.1–52.9] | 31.9 | 29.2 | 4.58 |
| daemon encendido | 109.3 [107.9–110.7] | 66.6 | 56.9 | 1.93 |

Con carga de fondo pesada el daemon encendido da 2.14 veces el FPS medio, 2.09 veces el 1% Low y 1.95 veces el 0.1% Low, y reduce la desviación del frametime de 4.58 a 1.93 ms. Los rangos no se solapan. Sin carga de fondo, el daemon encendido y apagado dan lo mismo (120.4 contra 120.0 FPS), y con carga y daemon encendido el juego queda un 9% por debajo de ese valor sin carga.

Es la primera ganancia demostrada de VegaFlow. Límites:

- La carga es un peor caso: 6 workers ocupando CPU sin pausa. Una pestaña o un launcher normales cargarían mucho menos, y el efecto sería menor.
- Qué parte produce la ganancia, medido por separado en el siguiente apartado.
- Una sola carga de juego sintética; no se probó con un juego real.

#### Qué acción protege al juego

Con el daemon apagado, `jobs/bg_factors.ps1` aplica a mano cada acción a la carga de fondo (Chrome sin ventana, 6 workers). D3D11, 3 rondas rotadas por condición:

| Acción aplicada a la carga de fondo | FPS medio | 1% Low | 0.1% Low | Desv. (ms) |
| --- | --- | --- | --- | --- |
| ninguna | 51.5 [51.0–52.0] | 34.3 | 30.9 | 3.74 |
| limitarla a 2 hilos lógicos (equivale a los CPU Sets de fondo) | 102.1 [101.1–103.4] | 71.5 | 62.3 | 1.20 |
| EcoQoS | 57.0 [56.2–57.6] | 36.1 | 32.9 | 3.28 |
| prioridad de I/O muy baja | 51.5 [50.3–52.1] | 34.3 | 32.1 | 3.73 |
| las tres juntas | 100.8 [100.7–100.9] | 69.9 | 61.2 | 1.18 |

Confinar el fondo a pocos hilos duplica el FPS (+98%) y explica casi toda la ganancia. EcoQoS aporta un 11%. La prioridad de I/O no tiene efecto en una carga que no usa disco. Las tres juntas quedan en 100.8 FPS, algo por debajo de los 109.3 del daemon real; la diferencia no se investigó (el daemon reaplica cada 1.5 s a los procesos nuevos de Chrome y yo apliqué una sola vez).

#### Otras cargas

| Escenario | Condición | FPS medio | 1% Low | 0.1% Low | Desv. (ms) |
| --- | --- | --- | --- | --- | --- |
| D3D12, carga de 6 workers (5 capturas) | daemon apagado | 39.5 [36.8–41.1] | 26.0 | 24.4 | 3.92 |
| D3D12, carga de 6 workers (5 capturas) | daemon encendido | 90.0 [89.9–90.2] | 71.9 | 61.8 | 0.45 |
| D3D11, carga ligera de 2 workers (5 capturas) | daemon apagado | 95.7 [94.3–98.3] | 61.9 | 55.3 | 2.04 |
| D3D11, carga ligera de 2 workers (5 capturas) | daemon encendido | 108.0 [105.3–108.7] | 63.5 | 47.6 | 2.11 |

- D3D12 con carga pesada: 2.28 veces el FPS medio y 2.77 veces el 1% Low.
- Carga ligera (D3D11): el FPS medio sube un 12.9%, pero los mínimos no mejoran. El 1% Low pasa de 61.9 a 63.5 (dentro del ruido) y el 0.1% Low empeora de 55.3 a 47.6 por una sola captura con un tirón (`bgl2-on-d3d11-3`: 1% Low 52.0 y 0.1% Low 20.6); sin ella el 0.1% Low encendido es 54.3. Con una captura atípica no se concluye que el daemon cause tirones, pero tampoco que los evite.
- La ganancia crece con la carga de fondo: nula sin ella, +13% con 2 workers y de 2.1 a 2.3 veces con 6.

#### Escala de carga y número de hilos para el fondo (2026-11-04, D3D11, 5 capturas por condición en A/B, 3 en hilos)

| Workers de fondo | daemon apagado (FPS / 1% Low) | daemon encendido (FPS / 1% Low) | Mejora FPS |
| --- | --- | --- | --- |
| 1 | 87.7 / 65.7 | 100.4 / 64.6 | +14% |
| 2 | 95.7 / 61.9 | 108.0 / 63.5 | +13% |
| 3 | 83.1 / 55.6 | 107.2 / 67.3 | +29% |
| 4 | 72.7 / 48.8 | 105.4 / 68.7 | +45% |
| 6 | 51.0 / ver arriba | 109.3 / ver arriba | +114% |

Con el daemon encendido el FPS queda casi constante (100 a 109) sea cual sea la carga; con él apagado cae al subir la carga. Los mínimos solo mejoran claramente desde 3 workers.

Hilos asignados al fondo (de 12), con el daemon apagado y el resto de acciones aplicadas a mano (`bg_factors.ps1 -Mode threads`):

| Hilos del fondo | 6 workers: FPS / 1% Low / CPU del fondo (s) | 2 workers: FPS / 1% Low / CPU del fondo (s) |
| --- | --- | --- |
| sin confinar | 52.0 / 33.0 / 181 | 91.8 / 63.5 / 61 |
| 8 | 62.0 / 36.9 / 180 | 92.3 / 62.8 / 61 |
| 6 | 88.0 / 50.0 / 173 | 104.2 / 63.3 / 59 |
| 4 | 90.4 / 60.1 / 120 | 104.2 / 72.2 / 45 |
| 2 | 113.1 / 70.1 / 60 | 104.2 / 74.1 / 44 |

- Con carga pesada, cada hilo menos para el fondo da más FPS, pero el fondo hace menos trabajo (con 2 hilos, un tercio). Con carga ligera, 2 a 6 hilos dan el mismo FPS y 4 a 2 mejoran el 1% Low. Es un compromiso entre el juego y el fondo, no un óptimo único.
- Esto apoya confinar el fondo en proporción a lo que consume, no un número fijo. No está implementado.
- Solo se midió en un 6 núcleos / 12 hilos Zen 2. En equipos con menos núcleos, con núcleos híbridos o con más de 8 núcleos no hay medición y el resultado puede ser distinto.

- El daemon sufre el mismo patrón que `tweaks_state.ps1`: su `--apu-balanced` escribe valores fijos y no los que había (borró `PERFBOOSTMODE=3` y `ASPM=0` que estaban puestos tras un reinicio).

## Observaciones

- El daemon no registró ninguna sesión de juego en estas corridas (`session_reports.log` no cambió en ninguna parada) porque corría en la sesión 0 (ver "Alcance"). El coste de -10.7% vino de algo que hace sin detectar un juego: el perfil de energía.
- La variación del nivel del daemon encendido entre sesiones (~95 a ~112 FPS antes de la corrección) quedó sin explicar; la corrección la elimina en la práctica, pero no se identificó su origen.

## Límites

- El daemon corrió en la sesión 0: las optimizaciones por proceso no están medidas.
- Una sola carga sintética limitada por CPU y driver (draw calls), en ventana. No es un juego.
- Una sola máquina y una sola sesión de medición.
- La causa del coste en D3D11 no está identificada.
