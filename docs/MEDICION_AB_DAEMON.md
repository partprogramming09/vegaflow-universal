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

## Causas descartadas

| Prueba | Resultado |
| --- | --- |
| Modo de boost del procesador 2 (Agresiva) contra 4 (Eficiencia agresiva), daemon apagado, 4 capturas ABBA | 124.5 contra 123.2 FPS (-1.1%): explica una décima parte del efecto |
| Con el daemon encendido, devolver boost y ASPM a su valor original contra dejarlos como los pone el daemon, 4 capturas ABBA | 95.2 contra 95.5 FPS: sin diferencia, así que los ajustes de energía no son la causa |
| Deriva térmica | Línea base sin daemon 3 capturas seguidas: 124.4, 126.4, 126.0 FPS, procesador a 3.0 GHz |

El daemon cambia dos ajustes del plan de energía al arrancar (`PERFBOOSTMODE` 2 a 4, `ASPM` 1 a 0), medido con `powercfg /q` antes y después; no cambia ningún valor de registro vigilado.

## Observaciones

- El daemon no registró ninguna sesión de juego con `drawstress.exe` (`session_reports.log` no cambió en ninguna parada): no lo trató como juego, así que las optimizaciones por proceso no se aplicaron. El coste viene de algo que hace sin detectar un juego.
- Con el daemon encendido varios minutos seguidos el FPS medio fue ~95, no ~112: el coste parece crecer con el tiempo encendido. Sin causa verificada.

## Límites

- Una sola carga sintética limitada por CPU y driver (draw calls), en ventana. No es un juego.
- Una sola máquina y una sola sesión de medición.
- La causa del coste en D3D11 no está identificada.
