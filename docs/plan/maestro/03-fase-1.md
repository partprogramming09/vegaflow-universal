# Fase 1: seguridad de archivos (lista blanca de `SandboxGuard`)

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Antes de escribir un archivo, el daemon pregunta "¿puedo escribir aquí?". La lista negra (lo prohibido) ya está en Rust. La lista blanca (lo permitido) sigue en C++ y tiene fallos reales.

**Flujo de hoy, cuando el daemon quiere escribir un archivo:**

```
+---------------------------------------------+
| Llega una ruta (ej. un Engine.ini)          |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Normalizar la ruta                          |
| (quitar "..", barras, mayúsculas)           |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Lista negra (Rust): ¿es de Windows, la raíz |
| o un archivo de arranque? Si sí, se niega   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| ¿Hay anti-cheat? Si sí, se niega            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Lista blanca (C++): ¿está en una carpeta    |
| permitida? Si no, se niega                  |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Se escribe el archivo                       |
| (sin volver a mirar la ruta)                |
+---------------------------------------------+
```

**Fallos confirmados en la lista blanca:**

| # | Fallo | Ejemplo | Gravedad |
|---|---|---|---|
| 1 | Permite la **carpeta padre** del daemon | El daemon está en `C:\ProgramData\VegaFlow`, así que permite todo `C:\ProgramData`, incluida la carpeta de inicio automático de Windows | **Alta** |
| 2 | Escribe aunque la carpeta se cambie **entre la comprobación y la escritura** | Una carpeta de Steam (el usuario puede escribir en ella) se cambia por un enlace a `System32` justo después de la comprobación | **Alta** |
| 3 | Acepta `\steamapps\common\` **en cualquier parte** de la ruta | Basta con crear `C:\Program Files\X\steamapps\common\` para que el daemon escriba dentro | Media |
| 4 | Permite la **carpeta de trabajo** | Si el daemon arranca con la carpeta de trabajo en `C:\`, permite casi todo el disco | Media |
| 5 | Compara texto, no carpetas | `...\AppData\Local` también acepta `...\AppData\LocalOtraCosa` | Baja |
| 6 | Crea carpetas **antes** de preguntar | El editor de `.ini` llama a `create_directories` sin pasar por la comprobación | Baja |

Hay un caso que se comprueba en el laptop antes de decidir nada: si las rutas con el prefijo `\\?\` pueden saltarse la lista negra.

**Flujo nuevo:**

```
+---------------------------------------------+
| Llega una ruta                              |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: normalizar y reunir las carpetas       |
| permitidas (LOCALAPPDATA, temp, Steam...)   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Rust: una sola decisión                     |
| lista negra -> anti-cheat -> lista blanca   |
| carpeta por carpeta. Sin carpeta padre ni   |
| de trabajo. Steam solo bajo bibliotecas     |
| reales (las de libraryfolders.vdf)          |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| C++: abrir el archivo y comprobar la ruta   |
| REAL ya abierta (sin enlaces por medio).    |
| Si cambió, se cierra y se niega             |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Se escribe                                  |
+---------------------------------------------+
```

**Por qué cada cambio:**
- Una sola decisión en Rust, para que las tres reglas (negra, anti-cheat y blanca) se prueben juntas con los mismos ejemplos.
- Carpeta por carpeta, para que `Local` y `LocalOtraCosa` sean distintas.
- Sin carpeta padre ni carpeta de trabajo, porque el daemon solo necesita escribir en su propia carpeta, en las de los juegos y en temp.
- Steam solo en bibliotecas reales: Steam guarda la lista de sus bibliotecas en `libraryfolders.vdf`, y se acepta solo lo que esté dentro de ellas. El proyecto ya lee ese archivo en C++ (`src/optimize/steam/steam_library.cpp`). Ese lector pasa a Rust (`policy/steam_library.rs`), porque ahora decide la seguridad: es dato de fuera y en Rust se ataca con proptest y fuzzing. No se usa una librería ajena (sección 3c).
- Comprobar la ruta ya abierta: es la única forma de cerrar la carrera entre comprobar y escribir. Esto lo hace Windows, así que va en C++.

**Sondas en el laptop (2026-11-10, `ValidatePath` del commit `f59fc17`, escritura):**

| Ruta probada | Resultado hoy | Qué significa |
|---|---|---|
| `%TEMP%\enlace\drivers\etc\x.txt`, con `enlace` un junction (`mklink /J`) a `C:\Windows\System32` | **Permitida** | La normalización no resuelve junctions: el fallo nº 2 existe sin necesidad de carrera |
| `C:\vfprobe\outside.txt` con el ejecutable en `C:\vfprobe\bin` | Permitida | Fallo nº 1 confirmado |
| `C:\vfprobe\cwd\x.txt` con la carpeta de trabajo en `C:\vfprobe\cwd` | Permitida | Fallo nº 4 confirmado |
| `C:\Program Files\X\steamapps\common\y.ini` | Permitida | Fallo nº 3 confirmado |
| `%LOCALAPPDATA%OtraCosa\x.txt` | Permitida | Fallo nº 5 confirmado |
| `\\?\C:\Windows\System32\x.dll`, `\\?\UNC\...`, `\\.\C:\...`, `\\?\GLOBALROOT\...`, `\\localhost\C$\...` | Negada | El prefijo no salta la lista negra, pero la normalización lo deja roto (`C:\?\C:\...`): una ruta `\\?\` legítima también se niega |
| `C:\PROGRA~1\COMMON~1\x.dll` (nombre corto 8.3) | Negada solo por la lista blanca | La lista negra no ve los nombres cortos; mirar la ruta real ya abierta lo cubre |

Por eso la comprobación de la ruta ya abierta (`GetFinalPathNameByHandleW`, que devuelve nombres largos y sin enlaces) es la que cierra los fallos 2 y los nombres cortos, y la normalización debe quitar `\\?\` y negar `\\?\UNC\`, `\\.\` y `GLOBALROOT`.

**Pruebas nuevas:**
- un caso por cada fallo de la tabla, que hoy debe fallar;
- casos permitidos que deben seguir funcionando (juego de Steam, LOCALAPPDATA, temp);
- un enlace creado a mano en el laptop.

**Heredado de R2 (hecho el 2026-11-10):** `DxvkManager` lee `dxvk.conf` por el puerto, ya no crea carpetas ni borra sin él (solo mira metadatos y compara DLL directamente); `SandboxGuard` pasó a `windows/` y `check_layers.sh` ya no tiene la excepción `windows/` → `safety/`; la Test 37 cubre también la purga de `EvaluateAndOptimize`. La prueba de que `run_cycle` pasa la política estática se aplazó a la fase 3 (ver `07-fase-3.md`).

**Terminado cuando:**
- los 6 fallos tienen su prueba y pasan;
- el Test 17 **se cambia a propósito**: hoy da por permitida una biblioteca de Steam inventada (`D:\SteamLibrary\...`, `tests/test_main.cpp:743`), que es justo el fallo nº 3. Pasa a usar una biblioteca listada en un `libraryfolders.vdf` de prueba. El Test 33 y los que escriben a través de `SandboxGuard` (13, 16, 22 y 24) siguen pasando;
- un juego real de Steam se sigue pudiendo optimizar.

## Evidencia (commit `61eed56`)

**Fase 1 (`src/safety/sandbox_guard.cpp`)**
- Flujo de escritura: ruta vacía en `:366-371`, normalización en `:105-142`, lista negra en `:388` (llama a `vf_rust_classify_blacklisted_path` en `:185`), anti-cheat en `:396-410`, lista blanca en `:414`, escritura en `:443` y `:451`.
- Carpeta padre: `:210-215`. El daemon se instala en `C:\ProgramData\VegaFlow` (`scripts/setup_tasks.ps1:15`) y corre como administrador (`:42`, `RunLevel Highest`). La lista negra solo bloquea los hijos directos de la raíz (`rust/vegaflow-core/src/path_policy.rs:83-92`).
- Carpeta de trabajo: `:218-221`. Las tareas no fijan la carpeta de trabajo (`setup_tasks.ps1:48`).
- Steam en cualquier parte: `:224-227`. Comparación de texto: `:36-44`; la normalización quita la barra final en `:137-139`.
- Carrera entre comprobar y escribir: no se vuelve a comprobar nada entre `:422` y `:451`.
- Carpetas creadas antes de comprobar: `src/optimize/steam/engine_presets.cpp:116`.
- Pruebas actuales: Test 17 (`tests/test_main.cpp:708-822`) y Test 33 (`:2113-2144`). Ninguna prueba un caso negativo de la lista blanca.
