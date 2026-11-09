# Fase 1b: configuración segura

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Las fases 5 y G guardan en `config.json` las reglas de la persona (qué frenar, excepciones, umbrales). Hoy ese archivo no aguanta eso, y además es un riesgo, porque lo lee un proceso de administrador:

| Fallo | Evidencia |
|---|---|
| Se lee con una ruta **relativa a la carpeta de trabajo** (`config/config.json` y, si no, `config.json`), elegida con una heurística | `src/app/main.cpp:43-48`. `RelaunchAsAdmin` no fija la carpeta de trabajo (`src/windows/privileges.cpp:69-89`) |
| Se lee buscando cada clave como texto en todo el archivo, sin anidación; un valor no numérico toma los dígitos de la clave siguiente | `src/app/config.cpp:26-50` |
| Al guardar se **vacía** el archivo y solo se escriben las claves conocidas: se borra lo que la persona escribió a mano | `src/app/config.cpp:91` |
| Un archivo corrupto vuelve a los valores de fábrica sin avisar | `src/app/config.cpp` |
| No se valida nada: `cooldown_seconds = 0` trae de vuelta el fallo "sin pausa" de la fase 4, y `poll_interval_ms = 0` llega a `SetWaitableTimer` | `src/app/daemon_loop.cpp:84-85` |

**Qué se hace:**
- **Una sola ruta,** junto al ejecutable (`GetModuleFileNameW`). Se quita la segunda opción. Al arrancar se comprueba que solo administradores y SYSTEM pueden escribir en esa carpeta; si no, se avisa y se usan los valores de fábrica.
- **Lector JSON real** (json.h, sección 3c), con un campo `version` para poder migrar un archivo viejo.
- **Guardar sin perder nada:** se conservan las claves desconocidas y se escribe en un archivo temporal que luego reemplaza al original (`MoveFileEx`), así un corte a mitad no deja el archivo roto.
- **Validación en Rust** (`policy/config_rules.rs`), con ejemplos: rangos para cada número (la pausa entre purgas y el intervalo nunca pueden ser 0) y una lista de procesos de Windows que no se pueden añadir como juego ni como excepción.
- Si el archivo está corrupto: se avisa en la GUI, se usan los valores de fábrica y **no se sobrescribe** el archivo hasta que la persona decida.

**Terminado cuando:**
- hay pruebas con un archivo corrupto, vacío, de una versión vieja y con una clave desconocida: no se pierde nada y se avisa;
- un proceso de Windows puesto como juego se rechaza, y un umbral fuera de rango también;
- guardar desde la GUI y reiniciar lee el mismo archivo, se arranque desde donde se arranque.
