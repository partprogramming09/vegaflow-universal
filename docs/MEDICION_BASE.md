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

## Observaciones

- El juego se presenta en `Legacy Flip`, no en borderless. Los tweaks de DirectFlip y MPO apuntan a otro modo de presentación y no se pueden evaluar con este juego tal como está configurado.
- Escena limitada por GPU (GPU 99%, ~43 FPS).
- `bench.ps1` marca `clean: true` cuando al menos el 99% de los cuadros comparten modo de presentación. `baseline-1` es anterior a esa marca y se considera limpia por protocolo.
