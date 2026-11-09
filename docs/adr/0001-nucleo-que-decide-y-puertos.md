# ADR 0001: Rust decide, C++ actúa a través de puertos

**Lector:** personas. Fecha: 2026-11-08. Estado: **propuesta** (pendiente de aprobación).

## Contexto

VegaFlow es un daemon de Windows que corre como administrador. Está escrito en C++20 (unas 15 000 líneas) con un núcleo en Rust (el 7,5 %). No usa frameworks y lo mantiene una sola persona. El diagnóstico del 2026-11-08 encontró cinco problemas:

1. **Las decisiones están mezcladas con las llamadas a Windows** dentro de la misma función. Por ejemplo, `EvaluateAndOptimize` decide y purga en el mismo sitio, y la lista blanca decide y además lee variables del sistema. Así, una regla no se puede probar sin tocar el sistema real.
2. **Clases hechas solo de funciones estáticas y sin interfaces:** `GraphicsOptimizer` tiene 36 miembros estáticos, `SandboxGuard` 21 y no hay ni una función `virtual`. No hay forma de cambiar el registro real por uno de prueba. Por eso las pruebas cambian el registro y el plan de energía de verdad, y un guardián final los restaura.
3. **Dependencia circular entre `detect/` y `optimize/`.** `system_info.cpp` incluye optimizadores, y los optimizadores incluyen `hardware_profiler.hpp` para usar sus tipos (`ApuOptimizationPolicy`).
4. **Todas las pruebas están en un solo archivo:** `tests/test_main.cpp`, con 2 157 líneas, 31 pruebas activas y un único `main`.
5. **La orquestación está repartida.** `run_cycle.cpp` (el caso de uso "sesión de juego") vive en `app/` junto a la CLI y la GUI. Además, `frame_telemetry` está en `safety/` aunque mide FPS.

## Decisión

Una **arquitectura hexagonal ligera**, también llamada "núcleo que decide, carcasa que actúa":

```
+-------------------------------------------------------+
| Entradas: CLI, GUI, bucle del daemon (src/app)        |
+-------------------------------------------------------+
                           |
                           v
+-------------------------------------------------------+
| Casos de uso (src/session): sesión de juego,          |
| perfil de motor, restaurar, informes                  |
| Solo hablan con puertos y con el núcleo               |
+-------------------------------------------------------+
                |                          |
                v                          v
+---------------------------+  +---------------------------+
| Núcleo Rust (decide)      |  | Puertos (src/ports):      |
| rust/vegaflow-core        |  | interfaces C++ pequeñas:  |
| reglas puras, sin I/O     |  | archivos, reloj, procesos,|
| (la purga UMA sale de     |  | registro, energía, equipo,|
| aquí en la fase 4)        |  | memoria, configuración,   |
|                           |  | diario                    |
+---------------------------+  +---------------------------+
                                             |
                                             v
                               +---------------------------+
                               | Adaptadores Windows       |
                               | (src/windows): Win32,    |
                               | ntdll, ETW, DXGI          |
                               +---------------------------+
```

Las reglas de la decisión:

- **Las dependencias apuntan hacia dentro.** Los adaptadores conocen los puertos, y los casos de uso conocen los puertos y el núcleo. El núcleo no conoce a nadie.
- **Se crea un puerto solo donde una prueba necesita un sustituto:** archivos (con la comprobación de la ruta ya abierta), reloj, control de procesos, registro, energía, sonda del equipo (GPU, disco, energía, versión de Windows), memoria (medir y purgar), configuración, y diario de cambios. No se crea una interfaz por cada clase.
- **Las clases estáticas se convierten en objetos** que reciben sus puertos en el constructor. Todo se arma en un solo sitio (`main`, la llamada *composition root*).
- **Los tipos compartidos** (perfil de hardware, política, juego detectado) pasan a `src/model/`. Esto corta el ciclo entre `detect/` y `optimize/`.
- **Pruebas en dos niveles:**
  - unitarias de los casos de uso con puertos falsos, que no tocan el sistema;
  - de integración por adaptador, que se corren en el laptop.

  `test_main.cpp` se reparte por módulo. Hecho en la fase R1 (2026-11-09): `tests/integration/`, un archivo por
  módulo, con el `main` propio (no doctest) para conservar el orden y la salida; doctest se valorará con las pruebas
  unitarias de R2.

## Alternativas

| Opción | A favor | En contra |
|---|---|---|
| **No hacer nada** | Sin riesgo ni esfuerzo | Siguen las pruebas sobre el registro real, el ciclo y las reglas imposibles de probar sin tocar el sistema |
| **Solo mover carpetas** | Barato y mecánico | No arregla nada de fondo: las decisiones siguen mezcladas con Windows |
| **Hexagonal ligera** (elegida) | Reglas en Rust probadas con ejemplos, casos de uso probables sin tocar el sistema, sin ciclos | Pide unas 10 interfaces nuevas, todas pequeñas y cambiar las clases estáticas por objetos |
| **Hexagonal completa** (una interfaz por cada servicio y DI en todas partes) | Máxima flexibilidad | Mucho código repetido para una persona; viola KISS y YAGNI |
| **Todo en Rust** | Un solo lenguaje | Reescribe 15 000 líneas, la GUI incluida, y obliga a repetir todas las mediciones |

## Consecuencias

- Las mejoras se hacen **por piezas** (patrón *strangler*), dentro de las fases del plan [`docs/plan/maestro/`](../plan/maestro/README.md). En cada fase, la decisión pasa al núcleo y la acción se pone detrás de su puerto.
- Mover archivos y cambiar lógica van siempre en **commits distintos**.
- La regla "las dependencias apuntan hacia dentro" la comprueba un script en el CI (`scripts/check_layers.sh`). La cabecera del puente C++ ↔ Rust se genera desde Rust con cbindgen, para que los dos lados no se separen.
- El consumo en reposo tiene que seguir por debajo de 15 MB. Las llamadas virtuales cuestan nanosegundos, así que no lo afectan, pero se mide al final.
- **Se revisa esta decisión** si hacen falta más de 12 puertos en total, o una sola fase necesita más de 2 nuevos, o si el consumo en reposo pasa de 15 MB.
- **Revisado el 2026-11-09:** se llegó a 11 puertos al añadir configuración y diario de cambios, y bajó a 10 al quitar la captura de fotogramas (fase 6). La decisión se mantiene porque cada puerto sustituye una prueba que tocaba el sistema real. Nuevo límite: 12.
