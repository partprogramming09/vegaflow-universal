# Fase 2b: diario de cambios

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Si el daemon se cierra a la fuerza (`Stop-Process -Force`, un cuelgue o un corte de luz), los cambios se quedan aplicados: los CPU Sets, EcoQoS y `Win32PrioritySeparation` siguen como los dejó (medido en `docs/mediciones/2026-11-juegos-reales.md:90`). La persona no lo sabe. Además, el Historial de la GUI necesita una fuente de datos: hoy solo existe una lista en memoria (`src/app/gui_mode.cpp:59`) y `RegBackup`, que guarda el valor original pero no cuándo ni por qué.

**Qué se hace:**
- **Antes** de cada cambio se escribe en disco qué se va a cambiar y su valor original; **después** se marca como hecho. La escritura es atómica (archivo temporal más renombrar) y pasa por el puerto de archivos.
- Al arrancar, si hay entradas sin cerrar o el último cierre no fue limpio, se ofrece devolverlas.
- **Modo seguro:** tras dos cierres anormales seguidos, VegaFlow arranca sin aplicar nada y lo dice.
- "Devolver todo a como estaba" informa del resultado: "Se devolvieron 7 de 7" o "2 no se pudieron devolver, y así se arreglan a mano".
- El Historial de la GUI lee este diario.
- **Punto de restauración de Windows (opcional, adoptado el 2026-11-09):** antes del primer cambio de registro se ofrece crear uno. Es un complemento, no sustituye al diario. Sin verificar: si funciona con la protección del sistema apagada y cada cuánto deja Windows crear uno.

**Terminado cuando:**
- en el laptop, matar el daemon con `Stop-Process -Force` y volver a arrancarlo ofrece devolver todo, y al aceptar `scripts/tweaks_state.ps1` no muestra ningún valor fuera de fábrica;
- un diario corrupto no impide arrancar: se avisa y se ofrece `--restore-registry`.
