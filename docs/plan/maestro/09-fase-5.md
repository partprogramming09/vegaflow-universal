# Fase 5: elegir qué procesos frenar

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Mientras se juega, VegaFlow frena los procesos de fondo (navegadores, Discord, launchers) para dejar CPU y disco al juego. Elegir mal significa frenar lo que no toca o no devolver algo a la normalidad.

**Flujo de hoy:**

```
+---------------------------------------------+
| Cada 1,5 s: ¿qué ventana está delante?      |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| ¿Es un juego? (se excluye una lista de      |
| procesos del sistema por nombre)            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Buscar procesos "ruidosos" por nombre       |
| (chrome.exe, discord.exe, steam.exe...)     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Frenarlos: EcoQoS, disco muy bajo,          |
| memoria baja y núcleos aparte               |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Al cerrar el juego: devolver todo           |
+---------------------------------------------+
```

**Fallos confirmados:**

| Fallo | Qué pasa |
|---|---|
| Al cambiar de juego no se devuelve el anterior | El juego viejo se queda con prioridad alta |
| Se guarda el número de proceso (PID) y nunca se limpia | Al restaurar se puede tocar otro proceso que heredó ese número |
| Apps delante que usan la GPU (Slack, Teams, Spotify) | Se toman por juego y reciben prioridad alta |
| Solo se mira el nombre del `.exe` | Cualquier programa llamado `discord.exe` se frena |
| OBS y Discord se frenan | Puede cortar una grabación o la voz |

**Flujo nuevo:**
- **Ante la duda, no tocar:** además de la lista de firmas, si hay señales genéricas de un anti-cheat (servicios o drivers conocidos cargados), el juego se trata como protegido. Un falso positivo solo deja un juego sin optimizar; uno negativo rompe la promesa del producto. Qué señales son fiables está **sin verificar**.
- **Rust** recibe el nombre y la ruta del proceso, y las reglas de la persona, y responde el tipo ("sistema", "juego posible" o uno de los tipos de programa) y la acción (frenar, solo ahorro o no tocar), con una tabla de ejemplos.
- **C++** guarda cada proceso frenado con su hora de inicio, para no confundirlo con otro que herede el número, y devuelve el juego anterior al cambiar de juego.

**Decidido (2026-11-08): cada persona elige, y OBS y Discord no se frenan por defecto.**

- **Tipos de programa (clasificación nueva):** grabación y transmisión, llamadas y voz, mensajería, navegadores, tiendas y lanzadores, música y sincronización en la nube. Hoy `process_detector.cpp:445-491` es una lista plana con 5 comentarios que mezcla llamadas con mensajería y grabación con música, así que hay que clasificar de nuevo; la tabla va en `tests/vectors/process_class.tsv`.
- **Tres acciones por tipo:** "Frenar" (ahorro de energía, disco y memoria en prioridad baja y núcleo apartado, como hoy en `cpu_orchestrator.cpp:301-316`), "Solo ahorro" y "No tocar".
- **Por defecto:** grabación y llamadas, "No tocar" (para no cortar una grabación ni la voz); música, "Solo ahorro"; el resto, "Frenar".
- **Excepciones por programa** (por ejemplo, "Telegram: no tocar") y **juegos añadidos a mano** (ya existe `custom_game_exes` en `config.json`).
- Los procesos de Windows nunca se tocan, y eso no se puede cambiar.
- **"Me fue mal en este juego" (adoptado el 2026-11-09):** un botón por juego que lo excluye al momento y lo anota en el diario, sin cifras.
- **Tipo nuevo "Trabajo pesado"** (Blender, DaVinci Resolve, Premiere, HandBrake, compiladores): con el modo "Jugar y trabajos pesados", recibe el mismo trato que un juego. Sin medir: apagado por defecto (sección 1b).
- Todo se guarda en `config.json`, con la fase 1b ya hecha. Rust recibe la configuración junto con el nombre y la carpeta del proceso, y devuelve la acción; así la regla sigue siendo una sola y se prueba con ejemplos.

**Terminado cuando:**
- la tabla de ejemplos pasa en Rust y en C++, incluidas las excepciones y un `discord.exe` falso en otra carpeta, que no se reconoce como Discord;
- con la configuración de fábrica, OBS y Discord no se frenan;
- al cambiar de juego, el anterior vuelve a la normalidad;
- al restaurar no se toca ningún proceso que no se frenó.

## Evidencia (commit `61eed56`)

**Fase 5**
- Cambio de juego sin devolver el anterior: `src/app/run_cycle.cpp:76-80`.
- PID que no se limpia: `src/optimize/cpu_orchestrator.cpp:345` y `:400-440`.
- La ventana de delante no exige DLL de gráficos: `src/detect/process_detector.cpp:299-302`.
- Lista de ruidosos: `:445-512`.
