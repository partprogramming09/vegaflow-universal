# Fase 6: quitar la medición de fotogramas del daemon

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Decidido el 2026-11-09.** VegaFlow no es un medidor: la GUI no muestra FPS (fase G) y la captura del daemon tenía fallos (cuenta como fotograma eventos que no lo son, mide el escritorio entero cuando no hay ETW y da cifras con una sola muestra; evidencia en el anexo). Arreglarla costaría más que quitarla. Para medir en desarrollo ya están `scripts/bench.ps1` y PresentMon.

**Qué se quita:** la captura ETW (`src/safety/frame_telemetry.*`), el Test 7, la parte de FPS de `--monitor` (que sigue mostrando RAM y VRAM; `cli.cpp:90`), su uso en `main.cpp`, `daemon_loop`, `run_cycle`, `gui_mode` y `monitor` (comprobado con una búsqueda de `FrameTelemetry` el 2026-11-09), su línea en `build.ps1` y los campos de FPS de `session_reports.log`.

**Terminado cuando:**
- no queda código de captura de fotogramas en el daemon y la suite pasa sin el Test 7;
- `--monitor` sigue funcionando con RAM y VRAM, sin FPS, y su ayuda ya no menciona FPS;
- el daemon en reposo sigue por debajo de 15 MB (debería bajar).

## Evidencia (commit `61eed56`)

**Fase 6 (`src/safety/frame_telemetry.cpp`)**
- Eventos 39/40/42: `:52-59`.
- Modo de reserva: `:419` y `:455-468`.
- Sin mínimo de muestras: `:265`. Los scripts exigen 100 (`scripts/bench.ps1:97`).
- La fórmula coincide con `scripts/bench.ps1:46-52` y `scripts/remote/vfframes.py:24-29`.

**Fase 6, números de evento**
- El código cuenta 39, 40 y 42 y los comenta como "Present_Stop" y "MPO_Start": `src/safety/frame_telemetry.cpp:52-59`.
