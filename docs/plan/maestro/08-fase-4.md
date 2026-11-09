# Fase 4: decidir cuándo liberar memoria

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** El daemon libera memoria de los procesos de fondo cuando el juego la necesita. Hoy las reglas están repetidas en C++ y en Rust (el umbral del 85 % está escrito en más de diez sitios, ver el criterio de terminado). Además, el guardián de memoria compartida no tiene pausa entre purgas.

**Flujo de hoy (en cada vuelta, cada 1,5 s):**

```
+---------------------------------------------+
| C++: medir RAM libre, standby y VRAM        |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: ¿poca RAM o VRAM al 85 %?              |
| ¿y standby >= 1500 MB?                      |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: ¿pasaron 90 s desde la última purga?   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: ¿el juego lee menos de 15 MB/s?        |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Purga                                       |
+---------------------------------------------+
```

El guardián UMA (en Rust) va por otro camino y **no tiene pausa:** puede vaciar memoria cada 1,5 s mientras dure la presión.

**Fallos confirmados:**
- **Sin pausa en el guardián.** Vacía la memoria de los procesos de fondo una y otra vez, y eso puede causar tirones.
- **La velocidad de disco del juego se mide mal.** Solo se toma cuando se cumplen las otras condiciones, así que el pico de 15 MB/s queda diluido en minutos.
- **El cálculo de la memoria libre cambia de sentido.** Si falla la consulta a Windows, "libre" pasa a incluir la memoria en caché.
- **La purga está escrita tres veces:** dos en C++ y una en Rust, que además llama a `ntdll` desde el núcleo, contra la regla de "núcleo sin I/O" del ADR.

**Flujo nuevo:** C++ mide (memoria, VRAM, lectura de disco en cada vuelta). Rust recibe las medidas y la hora y decide en un solo sitio, con una sola pausa para los dos caminos. La **acción** de purgar se queda en una sola copia en C++, detrás de un puerto `ports/memory_control.hpp`, y se saca la llamada a `ntdll` del núcleo Rust.

**Terminado cuando:**
- hay una sola función de decisión con pruebas para cada regla (umbrales, pausa, disco);
- el umbral del 85 % aparece en un solo sitio. Hoy está escrito en muchos: `memory_manager.cpp:179`, `uma_memory.rs:117`, `hardware_profiler.cpp:290`, `:309`, `:328` y `:348`, `hardware_profiler.hpp:51`, `reports.cpp:168` y la GUI (`gui_tabs.cpp:434`, `:436`, `:484` y `:491`), que debe leerlo del núcleo;
- el guardián UMA solo actúa si la GPU del juego es integrada (dato de la fase 3);
- la purga respeta la lista de fábrica de "No tocar" (OBS, Discord y las llamadas), aunque las reglas configurables lleguen en la fase 5;
- en el laptop, con un juego, no hay más de una purga cada 90 s.

## Evidencia (commit `61eed56`)

**Fase 4 (`src/optimize/memory_manager.cpp`)**
- Reglas: `:175-235`. El 85 %: ver la lista completa en el criterio de la fase 4.
- Disco medido después de las salidas tempranas: `:184` y `:197` van antes de `:206`.
- Guardián sin pausa: `:408-467`.
- "Libre" que incluye la caché: `:103`.
- Purga repetida: `:261-270` y `:322-331` frente a `uma_memory.rs:193-208`.
