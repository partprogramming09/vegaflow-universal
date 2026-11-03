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

## Observaciones

- El juego se presenta en `Legacy Flip`, no en borderless. Los tweaks de DirectFlip y MPO apuntan a otro modo de presentación y no se pueden evaluar con este juego tal como está configurado.
- Escena limitada por GPU (GPU 99%, ~43 FPS).
- `bench.ps1` marca `clean: true` cuando al menos el 99% de los cuadros comparten modo de presentación. `baseline-1` es anterior a esa marca y se considera limpia por protocolo.
