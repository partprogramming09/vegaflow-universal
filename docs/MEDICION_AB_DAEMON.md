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

El coste en D3D11 (-10.7%) desaparece y la ganancia aparente en D3D12 (+2.1%) también: con la corrección, el daemon encendido no cambia el resultado de `drawstress` en ninguna de las dos APIs. La ganancia anterior en D3D12 venía de la combinación de ajustes de energía, no de una optimización del daemon.

Esto es coherente con la observación siguiente: el daemon no trata a `drawstress.exe` como juego, así que solo actúan sus partes globales, y tras la corrección esas partes no mueven este benchmark.

## Observaciones

- El daemon no registró ninguna sesión de juego con `drawstress.exe` (`session_reports.log` no cambió en ninguna parada): no lo trató como juego, así que las optimizaciones por proceso no se aplicaron. El coste viene de algo que hace sin detectar un juego.
- La variación del nivel del daemon encendido entre sesiones (~95 a ~112 FPS antes de la corrección) quedó sin explicar; la corrección la elimina en la práctica, pero no se identificó su origen.

## Límites

- Una sola carga sintética limitada por CPU y driver (draw calls), en ventana. No es un juego.
- Una sola máquina y una sola sesión de medición.
- La causa del coste en D3D11 no está identificada.
