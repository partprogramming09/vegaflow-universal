# Fase 7: núcleos de CPU universales

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** El reparto de núcleos (qué núcleos quedan para el fondo) ya está en Rust, pero tiene tres problemas: una copia en C++, un límite de 64 hilos y una regla de Intel que choca con la guía del fabricante.

**Flujo de hoy:**

```
+---------------------------------------------+
| C++: leer la lista de núcleos de Windows    |
| (tipo, caché compartida, núcleo físico)     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| ¿64 hilos o menos?                          |
+---------------------------------------------+
           | sí                     | no
           v                        v
+--------------------+   +--------------------+
| Rust reparte       |   | C++ reparte con    |
|                    |   | una COPIA de la    |
|                    |   | misma regla        |
+--------------------+   +--------------------+
```

**Fallos confirmados:**

| Fallo | Qué pasa |
|---|---|
| Regla copiada en C++ (unas 200 líneas) para más de 64 hilos | Dos versiones que se pueden separar sin que nadie lo note, y ninguna prueba usa más de 64 hilos, así que la copia nunca se ejecuta en la suite |
| Intel híbrido: el fondo va a **todos** los E-cores, sin distinguir los E-cores de bajo consumo (LP) de Core Ultra | Los CPU Sets son afinidad "blanda" y eso encaja con lo que pide Intel (nada de afinidades duras; EcoQoS o afinidad blanda para el fondo). Pero Intel dice que los LP E-cores de Core Ultra están pensados para que Windows los use a su criterio, y la regla actual los mete en el conjunto del fondo |
| Límite de 64 hilos | En Windows 11 los procesos ya pueden usar todos los grupos de procesadores; un Threadripper o un Xeon caen en la copia de C++ |

**Lo que cambia y por qué:**
- **Se borra la copia en C++** y Rust reparte siempre, **dentro del grupo de procesadores** del proceso (ver más abajo). Un grupo tiene como máximo 64 hilos, así que el límite de 64 basta y ya no hace falta subirlo a 128 como decía el plan antiguo. Con cbindgen (fase R1), el límite vive en un solo sitio.
- **Intel híbrido:** lo principal sigue siendo EcoQoS (ya se aplica), que según Microsoft manda el fondo a los núcleos eficientes por sí solo. Los CPU Sets siguen como refuerzo blando, pero **excluyen los LP E-cores** de Core Ultra si se pueden identificar. Cómo identificarlos (por `EfficiencyClass` o por su caché aparte) está **sin verificar**: se decide con ejemplos de topologías reales de Core Ultra antes de escribir la regla. Si no se puede saber con seguridad, la regla de hoy se queda igual.
- La regla del plan antiguo ("los últimos 2 E-cores si hay 4 o menos, los últimos 4 si hay 8 o más") **no se adopta**: no tiene fuente, deja sin definir los casos de 5 a 7 E-cores y no está medida.
- **El juego no cambia:** con `restrict_game_cpu_sets` apagado (lo normal), el juego sigue sin restricciones en cualquier CPU.
- **AMD con dos CCD y X3D:** **no se toca por defecto.** Confinar al juego costó entre un 19 % y un 24 % de FPS en el laptop. Además, AMD gestiona los X3D con su propio driver y la Game Bar, y no hay documentación oficial de cómo convive eso con otra herramienta que fije núcleos. Queda como opción apagada hasta medirlo en un X3D.
- **Más de 64 hilos:** Microsoft avisa de que, con varios grupos, Windows ignora los núcleos asignados que estén en un grupo distinto del del hilo. Por eso la regla debe repartir **dentro del grupo** del proceso. Hace falta añadir el campo `group` a `VfCpuCoreEntry` (Windows ya lo da en `SYSTEM_CPU_SET_INFORMATION`); se prueba con topologías simuladas.

**Terminado cuando:**
- ya no existe la copia en C++, y la suite da el mismo reparto que antes en las topologías actuales (4600H, 5500U, Zen 4 + Zen 4c, 2C/4T, 4C/8T, 8C/16T);
- hay ejemplos simulados de Intel 8P+8E, 8P+16E y Core Ultra, Zen 5 + Zen 5c (Strix Point), Ryzen de 12 y 16 núcleos con dos CCD y una CPU de 2 grupos, y todos pasan;
- lo que no se puede medir en el laptop (Intel, X3D, más de 64 hilos) queda como **sin medir**.

## Evidencia (commit `61eed56`)

**Fase 7 (`rust/vegaflow-core/src/cpu_governor.rs`)**
- Límite de 64: `:2` y `src/bridge/rust_bridge.h:12` (escrito dos veces).
- Intel híbrido, todos los E-cores al fondo: `:103-130`.
- Copia en C++ para más de 64 hilos: `src/detect/hardware_profiler.cpp:426-628`; Rust solo se llama si hay 64 o menos (`:403`).
- Las pruebas de reparto (`tests/test_main.cpp:1676`, `:1718` y `:1850`) no usan más de 64 hilos.
- El juego solo se restringe con `restrict_game_cpu_sets` (apagado): `src/app/config.hpp:25`, usado en `run_cycle.cpp:159`.
