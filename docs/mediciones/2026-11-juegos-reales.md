# Mediciones con juegos reales (2026-11-05)

Octopath Traveler II y Zenless Zone Zero, fallos inyectados y ciclos de reposo. Método y límites generales: [`2026-11-ab-daemon.md`](2026-11-ab-daemon.md).

## Juego real: Octopath Traveler II (2026-11-05, D3D11)

Escena fija del autoguardado (personaje quieto, de noche), `Hardware: Independent Flip` en todas las capturas, 30 s por captura (5 s de calentamiento), PresentMon por `bench.ps1`, daemon lanzado con la tarea `VegaFlow-Start` (sesión 1). La escena va limitada por la GPU (overlay del juego: 43 FPS con GPU al 98 a 100%, CPU al 15 a 25%). Cada condición se midió dentro de un mismo lanzamiento del juego.

| Condición | Capturas | FPS medio (mediana, rango) | 1% Low (mediana) | 0.1% Low (mediana) | Desv. frametime (ms) |
| --- | --- | --- | --- | --- | --- |
| Sin carga, daemon apagado | 3 | 43.06 [42.97-43.15] | 35.9 | 32.3 | 0.67-1.17 |
| Sin carga, daemon encendido | 3 | 43.53 [43.49-43.58] | 37.4 | 35.4 | 0.76-0.86 |
| Carga de 4 workers (Chrome sin ventana), daemon apagado | 3 | 27.33 [27.23-27.50] | 19.1 | 16.9 | 4.55-4.63 |
| Carga de 4 workers, daemon encendido | 3 | 36.55 [36.40-37.55] | 25.7 | 18.2 | 2.18-2.51 |

- Sin carga de fondo el daemon no cambia el FPS medio (+1.1%, bajo el umbral de 3%). El 1% Low y el 0.1% Low se solapan entre condiciones.
- Con 4 workers de fondo el FPS medio sube un 34% (27.3 a 36.6) y el 1% Low un 34% (19.1 a 25.7); los rangos no se solapan. El 0.1% Low (16.9 a 18.2) se solapa y no se concluye. La desviación del frametime casi se reduce a la mitad. Sin carga el juego daba 43.1 FPS: la carga lo baja un 37% y el daemon recupera 9.2 de los 15.8 FPS perdidos.
- GameDVR: tres fases de 3 capturas con reinicio del juego entre fases, sin daemon ni carga. `GameDVR_Enabled` y `AppCaptureEnabled` en 1: 43.10 [43.10-43.11]; en 0: 43.20 [42.79-43.21]; otra vez en 1 tras reiniciar: 43.01 [42.96-43.15]. Sin efecto en el FPS medio; los 1% Low (38.9, 36.5 y 33.3) varían entre lanzamientos más de lo que varían entre condiciones. Valores originales restaurados.

Límites:
- Una sola escena, un solo juego, 3 capturas por condición. Con 30 s a 43 FPS el 0.1% Low sale de unos 2 cuadros y el 1% Low de unos 11: los lows de estas capturas son ruido (el 0.1% Low sin carga y sin daemon fue de 23.7 a 38.4).
- No se comprobó en `session_reports.log` que el daemon registrara una sesión de Octopath; la mejora con carga indica que sí actuó.
- La carga es sintética (workers de CPU en Chrome sin ventana), no un navegador o launcher reales.
- `bench.ps1` lee `MsBetweenPresents`, no `MsBetweenDisplayChange`.
- Octopath presentó en `Legacy Flip` el 2026-11-03 y en `Independent Flip` hoy, sin daemon y con ajustes de fábrica: el modo de presentación cambia entre sesiones.

### Carga de fondo en las dos clases, Fase 2 reducida (2026-11-05, Octopath, D3D11)

Dos celdas con `ab`: 5 capturas por condición, orden ABBA, 30 s, una captura de calentamiento que no cuenta, carga de Chrome sin ventana, dentro de un mismo lanzamiento del juego, `Independent Flip` en todas. Umbrales de la Parte 1 de este documento (fijados con el ruido de línea base). Quedan fuera por el tope de tiempo las cargas de 1 y 6 workers en las dos escenas, la de 4 workers en el mundo repetida con este método y la de memoria.

Menú principal (clase con tope de 60 FPS), 4 workers de fondo. Veredicto por cuadros tardíos (tiempo en pantalla sobre 1.5 veces la mediana):

| Condición | FPS medio (mediana [rango]) | Cuadros tardíos % (mediana [rango]) | Tirones por min (mediana [rango]) |
| --- | --- | --- | --- |
| daemon apagado | 59.71 [50.27-59.97] | 1.808 [0.200-12.739] | 4.8 [0.0-237.8] |
| daemon encendido | 59.99 [59.98-60.00] | 0.000 [0.000-0.000] | 0.0 [0.0-0.0] |

- Cuenta como mejora con el criterio fijado (al menos 0.1 puntos porcentuales y rangos sin solape, 5 capturas por condición): el mejor resultado apagado (0.2%) es peor que el peor encendido (0%). El FPS medio sube un 0.47%, bajo el umbral del 1%, pero no es la métrica de esta clase.
- Sin daemon la carga baja el menú a ~50 FPS en las dos primeras capturas apagadas (10.4% y 12.7% de cuadros tardíos) y a 59.7 a 60.0 en las otras tres: dos modos, como la bimodalidad entre trabajos que ya se había visto. Con daemon, las 5 capturas dan 59.98 a 60.00.
- Los 1% Low de 30 s (apagado 17.5, 17.1, 34.3, 48.6 y 39.1; encendido 52.4, 49.4, 54.0, 47.4 y 56.3) no entran en el veredicto por la duración.
- Errores en las dos condiciones: solo el ruido ambiental de `ryzenadj.exe` y su informe de Windows.

Mundo (limitada por GPU), 2 workers de fondo:

| Condición | FPS medio (mediana [rango]) | Tirones por min | 1% Low de 30 s |
| --- | --- | --- | --- |
| daemon apagado | 37.39 [36.79-37.72] | 0, 0, 0, 14.4, 0 | 30.4, 29.5, 28.0, 15.1, 27.0 |
| daemon encendido | 37.03 [36.34-37.62] | 0, 0, 0, 0, 0 | 27.4, 30.4, 29.2, 31.0, 29.5 |

- Sin mejora medible: el FPS medio cambia -0.98%, bajo el umbral del 1%, con rangos que se solapan. No cuenta.
- La carga de 2 workers baja el mundo de 43.1 a ~37 FPS (-13%) con y sin daemon: el daemon no recupera esa pérdida. Con 4 workers sí la recuperaba (27.3 a 36.6, +34%, 3 capturas por condición en la medición anterior).
- Una captura apagada tuvo un tirón (14.4 por minuto, 1% Low 15.1) y las cinco encendidas no; el intervalo de confianza del 1% Low agrupado va de -2.9% a +68.3% porque las capturas de 30 s son demasiado cortas: no hay veredicto sobre los lows.
- Hipótesis sin verificar: con 2 workers confinados a 2 hilos el fondo consume lo mismo (2 hilos al 100%), así que confinar no le quita carga y la pérdida de la iGPU, que comparte potencia y memoria con la CPU, sigue igual; con 4 workers el fondo sí hace menos trabajo.

Límites: una sola máquina, un juego, una carga sintética y 5 capturas de 30 s por condición; cada celda tarda unos 9 minutos.

#### Mezcla de fondo con forma realista, mundo (2026-11-05, `ab Load=mix`)

Carga `mix` de `ab_loop_task.ps1`: Chrome sin ventana con 3 pestañas al 40% de ciclo de trabajo (ráfagas) más 2 procesos de PowerShell que calculan hashes SHA256 de DLL de `System32` sin parar (equivalente a un launcher verificando archivos). Medida antes en el laptop: 3.32 procesadores lógicos y ningún proceso sobrante al cerrar. Es una carga sintética con forma realista: no incluye vídeo, descarga de red ni la actualización de un launcher, que no se pueden provocar de forma reproducible sin conexión. Misma celda que las anteriores (5 capturas por condición de 30 s, ABBA, calentamiento, mundo de Octopath).

| Condición | FPS medio (mediana [rango]) | 1% Low de 30 s | Tirones por min | Cuadros tardíos % |
| --- | --- | --- | --- | --- |
| daemon apagado | 41.03 [40.92-41.17] | 35.9, 36.5, 33.8, 33.5, 34.4 | 0, 0, 0, 0, 0 | 0, 0, 0, 0, 0 |
| daemon encendido | 40.82 [40.71-40.87] | 30.5, 32.6, 33.4, 32.0, 33.9 | 0, 0, 0, 0, 0 | 0.098, 0, 0, 0.196, 0 |

- Sin mejora: el FPS medio cambia -0.53%, bajo el umbral del 1%, así que no cuenta. Los rangos no se solapan (la peor captura apagada, 40.92, supera a la mejor encendida, 40.87): el daemon queda unas décimas de FPS por debajo, en las 5 capturas.
- Esta mezcla le cuesta poco al juego: de 43.1 FPS sin carga a 41.0 apagado (-4.8%), contra -13% con 2 workers y -37% con 4 workers de Chrome al 100% en la misma escena. El daemon no tiene pérdida que recuperar.
- Los 1% Low de 30 s del daemon encendido son más bajos (mediana 32.6 contra 34.4; agrupados -7%, intervalo de confianza del 95% de -12% a -1.8%). Las capturas son más cortas que los 120 s del criterio, así que no hay veredicto, pero el signo es negativo en 4 de 5 pares y queda como sospecha de un coste del daemon en esta carga; hace falta repetirlo con capturas de 120 s.
- Hipótesis sin verificar: la pérdida de rendimiento depende del tipo de trabajo del fondo (el bucle de JavaScript de Chrome al 100% pesa más que los hashes y las ráfagas), no del número de procesadores lógicos que ocupa.
- Errores en las dos condiciones: solo el ruido ambiental de `ryzenadj.exe`.

Confirmación con capturas de 120 s (mismo escenario, 3 capturas por condición, ABBA, calentamiento, 4900 cuadros por captura):

| Condición | FPS medio | 1% Low por captura | 1% Low agrupado | Tirones por min | Cuadros tardíos % |
| --- | --- | --- | --- | --- | --- |
| daemon apagado | 41.07, 40.90, 41.03 | 35.06, 29.23, 34.85 | 32.71 | 0, 3, 0 | 0.041, 0.163, 0 |
| daemon encendido | 41.00, 41.09, 40.99 | 33.38, 35.05, 34.00 | 33.99 | 0, 0, 0 | 0.020, 0, 0.020 |

- El FPS medio cambia -0.07%: la diferencia de -0.53% de las capturas de 30 s desaparece. El 1% Low agrupado sube +3.9% con un intervalo del 95% de -3.3% a +14.1% (incluye 0): no hay diferencia. La sospecha de coste del daemon en los lows no se confirma; el signo incluso se invierte. Con esta mezcla el daemon no ayuda ni daña.
- Una captura apagada tuvo un tirón (1% Low 29.2, 0.1% Low 15.2, 3 tirones por minuto); las tres encendidas no. Con 3 capturas no se atribuye al daemon.

## Fallos inyectados: matar el daemon a mitad de partida (2026-11-05)

Octopath abierto, Chrome sin ventana con 4 workers como fondo, daemon lanzado con la tarea `VegaFlow-Start` (sesión 1), cada estado leído con `GetProcessInformation` y `GetProcessDefaultCpuSets` y con `tweaks_state.ps1`:

| Momento | Fondo (Chrome) | Juego | Valores fuera de fábrica |
| --- | --- | --- | --- |
| sin daemon | 0 CPU Sets, sin EcoQoS | 0 CPU Sets, sin EcoQoS | ninguno |
| daemon vivo | 2 CPU Sets, EcoQoS activo | 0 CPU Sets, sin EcoQoS, prioridad normal | `Win32PrioritySeparation` 40 (fábrica 2) |
| daemon muerto con `Stop-Process -Force` | sigue con 2 CPU Sets y EcoQoS | igual | `Win32PrioritySeparation` sigue en 40 |
| tras `--restore-registry` y `--apu-balanced` | n/d | n/d | ninguno; los dos salen con código 0 |

- Con el daemon vivo el aislamiento es el esperado: el fondo confinado a 2 hilos con EcoQoS y el juego sin tocar. El daemon no modificó el `Engine.ini` de Octopath (misma fecha de modificación antes y durante).
- Defecto: un cierre no limpio deja `Win32PrioritySeparation` en 40 y los procesos de fondo confinados y con EcoQoS mientras vivan. No hay recuperación automática si el daemon no vuelve a ejecutarse.
- Recuperación probada: arrancar el daemon otra vez y cerrarlo con normalidad (tarea `VegaFlow-Stop`) devuelve `Win32PrioritySeparation` a 2, así que la copia de seguridad conserva el valor original tras el fallo; también sirve `vegaflow_daemon.exe --restore-registry`.
- Corrección propuesta, no implementada (registrar una tarea de arranque necesita al usuario): una tarea al iniciar sesión que ejecute `--restore-registry` si el daemon no está corriendo.

## Reposo y ciclos de abrir y cerrar el juego (2026-11-05)

Daemon en la sesión del escritorio, 1 minuto en reposo y luego 6 ciclos de `drawstress` abierto 30 s y cerrado 12 s, con una lectura de proceso al final de cada fase (`cycles.sh`).

| Fase | Memoria privada | Manejadores | CPU acumulada |
| --- | --- | --- | --- |
| Reposo | 1.8 MB | 106-107 | 0.77 s en 61 s (0.2 s en el último minuto) |
| Con el juego abierto (ciclos 1 a 6) | 3.6-3.7 MB | 133-137 | |
| Con el juego cerrado (ciclos 1 a 6) | 2.2 → 2.1 MB | 131 → 127 | |
| Reposo final y tras 6 ciclos | 2.1 MB | 127 | 2.17 s en total |

- Cumple el KPI (menos de 15 MB privados, casi 0% de CPU en reposo) con margen de 7 veces.
- No hay fuga: la memoria privada con el juego abierto es constante (3.6 → 3.7 MB) y la de cerrado baja (2.2 → 2.1); los manejadores de cerrado bajan de 131 a 127 y se estabilizan; los hilos vuelven a 2-3.
- El registro de sesión crece 788 bytes por ciclo, constante.
- Tras el cierre limpio el daemon no queda corriendo y `doctor` no ve procesos sobrantes.
- Límite: 6 ciclos y 8 minutos.

Reposo largo, 20 minutos sin juego, una lectura cada 2 minutos: memoria privada 1.8-1.9 MB constante, manejadores 104 constantes, hilos 1 a 4, CPU acumulada 0.5 s cada 2 minutos (0.4% de un hilo, 0.04% del procesador de 12 hilos), registro de sesión sin crecer (79226 bytes), cierre limpio y sin procesos sobrantes. Sin fuga en 20 minutos; una fuga más lenta que eso no se descarta.

## Zenless Zone Zero con anti-cheat (2026-11-05, D3D11)

Segundo juego real, con HoYoverse Protect activo. El proceso carga `d3d11.dll` y `dxgi.dll` y ningún módulo de D3D12, así que esta celda es D3D11, no D3D12. El juego corría como administrador (lo abrió el usuario con UAC), 1920x1080 en pantalla completa sin bordes según el registro, escena fija del mundo con el personaje quieto, tope de 60 FPS. La calidad gráfica no se pudo registrar: el menú no está en el registro y Windows bloqueó mis teclas sobre el proceso elevado. Medido con `vfdev run pack`, 1 calentamiento y 5 capturas de 120 s por condición, sin carga de fondo, sin tocar archivos del juego; el daemon solo detecta el juego y no lo modifica.

| Condición | FPS medio | 1% Low agrupado |
| --- | --- | --- |
| Daemon apagado | 58.57 [58.50-58.68] | 41.90 |
| Daemon encendido | 58.89 [58.60-58.96] | 41.42 (IC95 del cambio -2.4% a +0.2%) |

- Sin efecto: +0.54% de FPS (umbral 1%, rangos con solape) y -1.15% de 1% Low (umbral 3%, el intervalo incluye 0). Es lo esperado sin carga de fondo.
- Estabilidad: 5 de 5 capturas `clean` con el daemon encendido, 0 errores del sistema, el juego y el anti-cheat siguieron funcionando (sin expulsión ni cierre), modo de presentación "Hardware Composed: Independent Flip" en todas.
- Cuadros tardíos con el daemon encendido: 1.75% a 2.39% de los cuadros (juego topado a 60 FPS); la condición apagada no se leyó para este conteo.
- Límites: una sola escena quieta, sin carga de fondo, D3D11, calidad gráfica sin registrar, una sesión. No mide D3D12 ni un juego limitado por CPU.

