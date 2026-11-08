# VegaFlow Universal

Daemon de Windows en C++20, con núcleo híbrido en Rust, que optimiza juegos en APUs AMD (banco de pruebas: Ryzen 5 4600H con Radeon Vega 6). Aísla los procesos de fondo, aplica perfiles según el nivel de la APU, usa DXVK-GPLAsync y registra los frametimes. No inyecta nada en juegos con anti-cheat.

![Lenguaje](https://img.shields.io/badge/C%2B%2B20%20%2B%20Rust-blue.svg) ![Plataforma](https://img.shields.io/badge/Windows%2010%20%7C%2011%20x64-0078D6.svg) ![Pruebas](https://img.shields.io/badge/pruebas-31%20C%2B%2B%20%7C%2015%20Rust-success.svg)

## Por qué existe

En una APU la CPU y la iGPU comparten la RAM y el presupuesto térmico, y el sistema está pensado para escritorios con GPU dedicada. VegaFlow actúa sobre cinco problemas:

1. **Memoria compartida (UMA):** los procesos de fondo compiten con la iGPU por el mismo bus DDR4.
2. **Falso límite de 512 MB de VRAM:** las BIOS lo reportan y los motores lo toman al pie de la letra.
3. **Sin GPL en los drivers AMD de Vega:** los shaders se compilan en el hilo del juego; DXVK-GPLAsync los saca a hilos de fondo.
4. **Composición del DWM en ventana:** la copia intermedia añade latencia; el modo Flip la evita.
5. **Presupuesto térmico compartido (STAPM):** si la CPU consume de más, la GPU baja su reloj.

## Qué hace

- **Aislamiento de fondo:** confina los procesos de fondo (afinidad, EcoQoS, prioridad de I/O) para dejar la CPU y el disco al juego.
- **Perfiles por Tier:** `ApuCapabilityGuard` clasifica el equipo en 4 niveles y no aplica a uno lo calibrado para otro.
- **Anti-cheat:** si detecta un controlador anti-cheat, `SandboxGuard` bloquea toda escritura y no toca archivos del juego.
- **Reversible:** cada cambio guarda una copia (`.vegaflow.bak`, o el valor original del registro); `--restore-registry` los devuelve todos.
- **Ligero:** sin frameworks pesados; GUI Win32/GDI y menos de 15 MB en reposo.

El detalle de cada función está en [`docs/guias/uso-y-funciones.md`](docs/guias/uso-y-funciones.md).

## Resultados medidos

Las cifras salen de PresentMon y están con su método y sus límites en [`docs/mediciones/`](docs/mediciones/); qué falta medir, en [`estado-de-verificacion.md`](docs/mediciones/estado-de-verificacion.md). En resumen: en Octopath Traveler II el preset UMA del `Engine.ini` dio +40% de FPS medio en el mundo (captura no intercalada y causa no aislada); en Zenless Zone Zero el daemon no cambió el FPS (58.57 a 58.89, sin efecto) y no hubo expulsión del anti-cheat.

## Compilar y usar

Solo en Windows, con MinGW-w64 g++ 13+ y cargo con el objetivo `x86_64-pc-windows-gnu`:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```

Guías: [compilar y probar](docs/guias/compilar-y-probar.md) y [uso del daemon](docs/guias/uso-y-funciones.md).

## Estructura del repositorio

```
src/ + include/     código del daemon en C++20, espejo una de otra
  core/               privilegios, hardware, procesos, SystemInfo
  optimizers/         las palancas: gráficos, memoria, CPU, DXVK, motores
  security/           SandboxGuard y telemetría de cuadros
  gui/                ventana Win32/GDI
rust/vegaflow-core/ reglas puras en Rust (presión UMA, topología, confinamiento, firmas)
tests/              la suite de 31 pruebas de C++
scripts/            compilar, medir y operar el laptop de pruebas (ver scripts/README.md)
assets/dxvk/        DLL de DXVK que el daemon instala en los juegos
config/             valores por defecto del daemon
docs/               toda la documentación (ver docs/README.md)
```

## Documentación

Empieza por [`docs/README.md`](docs/README.md): una tabla "¿Qué quieres hacer?" te lleva al documento que necesitas. Lo que falta por hacer está en [`docs/plan/`](docs/plan/).

## Seguridad

Nunca se inyectan DLLs ni se modifican archivos de juegos con anti-cheat. Toda operación de disco y procesos pasa por `SandboxGuard`, y toda función que modifica el sistema acepta `dry_run`.

## Licencia / License

Software libre bajo la **GNU GPL v3 o posterior** (ver [`LICENSE`](LICENSE)): puedes usarlo, estudiarlo, modificarlo y compartirlo. Si distribuyes una versión modificada, debes publicar también su código fuente con la misma licencia.

Free software under the **GNU GPL v3 or later** (see [`LICENSE`](LICENSE)): you may use, study, modify and share it. If you distribute a modified version, you must also publish its source code under the same license.
