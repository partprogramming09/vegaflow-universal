# Regresión de Octopath Traveler II con el preset UMA (2026-11-15)

> **Note:** this dated report is kept in Spanish, as it was recorded. Use your browser's translator to read it in another language; the living summary in English is [Verification status](./verification-status.md).

**Lector:** quien interprete la puerta de regresión de Octopath (`scripts/remote/baselines/octopath_world.json`).

## Qué se midió
La escena de la línea base (autoguardado, de noche, personaje quieto, D3D11), con `vfdev run ab` (3 repeticiones de 120 s, 1 de calentamiento, sin carga) y `vfdev regress` contra la línea base. Tres tandas, todas con el daemon apagado en las capturas «off»:

| Tanda | `Engine.ini` del juego | FPS medio (3 capturas «off») | Veredicto de `vfdev regress` |
|---|---|---|---|
| A | de fábrica (sin preset) | 30,49 · 30,53 · 30,51 | **FAIL**: 30,51 contra 42,81 (-28,74 %); 1 % Low 28,15 contra 34,40 (-18,15 %) |
| B | preset UMA aplicado con el botón «Aplicar Preset APU» de la GUI | 42,49 · 42,73 · 42,71 | **PASS**: 42,71 contra 42,81 (-0,23 %, límite -1,00 %); 1 % Low 38,68 contra 34,40 (+12,45 %) |
| C | preset UMA (copia del `Engine.ini` que dejó la GUI en la tanda B), **SCSKiller cerrado** y su `d3d11.dll` fuera de la carpeta | 43,17 · 43,22 · 43,16 | **PASS**: 43,17 contra 42,81 (+0,84 %, límite -1,00 %); 1 % Low agrupado 39,46 contra 34,40 (+14,70 %) |

En la tanda B las capturas con el daemon encendido dieron 43,13 · 43,07 · 43,05 FPS (`Hardware: Independent Flip`); las «off» salieron en `Hardware: Legacy Flip`, como la línea base. En la tanda C, 43,63 · 43,79 · 43,68 con el daemon encendido (Independent Flip) y las «off» otra vez en Legacy Flip.

> **Nota (2026-11-15, fase G, versión 0.2.0):** el botón «Aplicar Preset APU» de esta medición ya no existe. En la pantalla Juegos de la GUI nueva los botones son «Aplicar perfil», «Volver al archivo original» y «Me fue mal en este juego». Las tandas de arriba se hicieron con la GUI anterior; en esta carpeta no hay una repetición con la nueva. El texto original no se cambia.

## Qué significa
- La línea base se tomó con el preset puesto. Sin él, la escena queda limitada por la GPU (el overlay del equipo marcaba GPU al 100 % y 30 FPS; `FrameRateLimit=60` en `GameUserSettings.ini`, así que no es un tope del juego).
- Con el preset aplicado por la propia GUI, la escena vuelve a la línea base. Eso comprueba que «Aplicar Preset APU» sigue funcionando sobre un juego real.

## Límites
- Cada tanda son 3 capturas de **un solo lanzamiento** del juego, no 3 lanzamientos.
- **SCSKiller en las tandas A y B.** La línea base dice «sin SCSKiller»; en A y B su `d3d11.dll` volvió a la carpeta del juego a las 02:23:20 porque el programa seguía abierto. La tanda C se repitió con el programa cerrado, el archivo renombrado y comprobado 15 s después: el resultado es el mismo (PASS, +0,84 % en FPS medio), así que SCSKiller no explica la diferencia. El 1 % Low queda por encima de la línea base en B y en C (+12 % y +15 %); no se investigó por qué.
- La tanda C aplicó el preset copiando el `Engine.ini` de la tanda B, no con el botón de la GUI.
- Windows 10.0.26300; PresentMon 2.6 según `docs/measurements/2026-11-ab-daemon.md`.
- No se probó nada sobre Zenless Zone Zero.
