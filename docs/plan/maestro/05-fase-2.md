# Fase 2: editor de `.ini` de juegos (Unreal y Unity)

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** VegaFlow cambia el `Engine.ini` de los juegos Unreal y el `boot.config` de los juegos Unity. Lo correcto es que siempre se pueda volver al original exacto. Hoy hay casos en que el original se pierde.

**Flujo de hoy (Unreal):**

```
+---------------------------------------------+
| Buscar los Engine.ini del juego             |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Copiar el original a .vegaflow.bak          |
| (si la copia falla, sigue igual)            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Leer el ini y quitar el bloque VegaFlow     |
| anterior                                    |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Borrar 3 claves en todo el archivo          |
| (también fuera del bloque)                  |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Añadir el bloque VegaFlow al final          |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Escribir el archivo                         |
+---------------------------------------------+
```

**Flujo de hoy al restaurar:**

```
+---------------------------------------------+
| ¿Existe el .vegaflow.bak?                   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Copiar el .bak sobre el ini                 |
| (no se mira si la copia salió bien)         |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Borrar el .bak                              |
| (aunque la copia haya fallado: se pierde    |
| el original)                                |
+---------------------------------------------+
```

**Fallos confirmados:**

| Fallo | Qué pasa | Gravedad |
|---|---|---|
| Se borra el `.bak` aunque falle la copia | Se pierde el original del juego | **Alta** |
| Se escribe aunque falle la copia de seguridad | El cambio queda sin vuelta atrás | **Alta** |
| Falta el marcador de cierre del bloque | Se borra todo desde el bloque hasta el final del archivo | **Alta** |
| Archivo en UTF-16 (Unreal lo usa si hay acentos u otros caracteres) | Se mezclan dos codificaciones y el archivo queda corrupto | Media |
| BOM al principio (unos bytes invisibles de algunos editores) | No reconoce `[SystemSettings]` y la duplica. Hoy solo afecta a `ApplyApuShadowConfig`, que solo usan las pruebas | Baja |
| Finales de línea mezclados (CRLF y LF) | Restaurar no deja el archivo idéntico | Baja |
| Unity con `clave = valor` (con espacios) o claves repetidas | Duplica la clave o deja la vieja activa | Baja |
| Dos tipos de copia (`.bak` y `.vegaflow.bak`) | La restauración depende del orden | Baja |

**Flujo nuevo:**

```
+---------------------------------------------+
| C++: buscar el ini y comprobar la ruta      |
| (fase 1)                                    |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: copiar a .vegaflow.bak                 |
| Si falla, SE PARA y no toca nada            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Rust: texto viejo -> texto nuevo            |
| Respeta la codificación, el BOM y los       |
| finales de línea. Solo toca su bloque.      |
| Si falta un marcador, devuelve un error     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: escribir el texto nuevo                |
+---------------------------------------------+
```

Al restaurar se copia el `.bak`, se comprueba que el resultado sea idéntico byte a byte y solo entonces se borra el `.bak`.

**Por qué en Rust:** la mezcla de textos es la parte delicada y es pura (entra un texto y sale otro). En Rust se prueba con muchos archivos de ejemplo: UTF-16, BOM, CRLF, sin marcador o aplicado dos veces. La copia y la escritura son de Windows y se quedan en C++.

Las funciones de sombras (`ApuShadowConfig`) hoy solo las usan las pruebas. Se pasan a Rust igual, porque comparten el mismo lector de `.ini`.

**Terminado cuando:**
- aplicar y restaurar deja el archivo **idéntico byte a byte** con los archivos de ejemplo de `tests/vectors/ini/`: UTF-8, UTF-8 con BOM, UTF-16, CRLF, LF, finales mezclados, sin marcador de cierre, ya aplicado, y el caso en que el `.ini` no existía (la copia `Empty Backup`, `engine_presets.cpp:309`);
- aplicar dos veces da el mismo resultado que aplicar una;
- si falla la copia de seguridad, el juego queda intacto;
- se prueba con un juego Unreal real en el laptop.

## Evidencia (commit `61eed56`)

**Fase 2**
- `src/optimize/steam/engine_presets.cpp`: copia sin comprobar en `:120-127`; marcador que falta en `:148`; borrado de 3 claves en todo el archivo en `:158-167`; `.bak` borrado tras una copia sin comprobar en `:312-318` y en `:393-399` (Unity).
- `src/optimize/steam/apu_shadow_config.cpp`: BOM en `:137`; normalización a LF en `:125` y `:202`.
- Unity: claves en `:246-258`.
- Pruebas: Test 13 (`:400-501`) y Test 24 (`:1380-1510`). No hay pruebas de Unity, UTF-16, BOM, CRLF ni de aplicar dos veces.
