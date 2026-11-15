# Regresión de Octopath Traveler II con el preset UMA (2026-11-15)

**Lector:** quien interprete la puerta de regresión de Octopath (`scripts/remote/baselines/octopath_world.json`).

## Qué se midió
La escena de la línea base (autoguardado, de noche, personaje quieto, D3D11), con `vfdev run ab` (3 repeticiones de 120 s, 1 de calentamiento, sin carga) y `vfdev regress` contra la línea base. Dos tandas, ambas con el daemon apagado en las capturas «off»:

| Tanda | `Engine.ini` del juego | FPS medio (3 capturas «off») | Veredicto de `vfdev regress` |
|---|---|---|---|
| A | de fábrica (sin preset) | 30,49 · 30,53 · 30,51 | **FAIL**: 30,51 contra 42,81 (-28,74 %); 1 % Low 28,15 contra 34,40 (-18,15 %) |
| B | preset UMA aplicado con el botón «Aplicar Preset APU» de la GUI | 42,49 · 42,73 · 42,71 | **PASS**: 42,71 contra 42,81 (-0,23 %, límite -1,00 %); 1 % Low 38,68 contra 34,40 (+12,45 %) |

En la tanda B las capturas con el daemon encendido dieron 43,13 · 43,07 · 43,05 FPS (`Hardware: Independent Flip`); las «off» salieron en `Hardware: Legacy Flip`, como la línea base.

## Qué significa
- La línea base se tomó con el preset puesto. Sin él, la escena queda limitada por la GPU (el overlay del equipo marcaba GPU al 100 % y 30 FPS; `FrameRateLimit=60` en `GameUserSettings.ini`, así que no es un tope del juego).
- Con el preset aplicado por la propia GUI, la escena vuelve a la línea base. Eso comprueba que «Aplicar Preset APU» sigue funcionando sobre un juego real.

## Límites
- Cada tanda son 3 capturas de **un solo lanzamiento** del juego, no 3 lanzamientos.
- **SCSKiller estuvo presente.** La línea base dice «sin SCSKiller»; se renombró su `d3d11.dll` de la carpeta del juego, pero SCSKiller (que sigue en ejecución) la recreó a las 02:23:20, pocos minutos después. La comparación no es de la misma condición: el +12 % del 1 % Low puede venir de SCSKiller. Para repetirla de verdad hay que cerrar SCSKiller antes de renombrar el archivo.
- Windows 10.0.26300; PresentMon 2.6 según `docs/mediciones/2026-11-ab-daemon.md`.
- No se probó nada sobre Zenless Zone Zero.
