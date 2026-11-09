# Arquitectura, Rust y librerías

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

## 2. La regla para elegir qué pasa a Rust

Una pieza pasa a Rust si cumple las tres condiciones:

1. **Decide** algo, o **lee datos que no controlamos** (rutas, textos de juegos, datos del hardware).
2. Si falla, **rompe algo del usuario** o **falsea una medición**.
3. Entra y sale con **datos simples** (texto y números).

Lo que solo llama a Windows (registro, energía, GUI, ETW, DXGI, aplicar prioridades) se queda en C++. Que se pueda probar en el Mac no cuenta: el producto es de Windows y la prueba que vale es la del laptop.

## 3. Lo que ya está en Rust (hecho el 2026-11-08)

Son 6 módulos, el 7,5 % del código y 26 pruebas (contadas en el Mac): el guardián de memoria UMA, el reparto de núcleos, el confinamiento de procesos de fondo, las firmas de anti-cheat, la **lista negra** de rutas y el clasificador de Tiers.

Otras cosas que ya existen y que el plan no repite:
- **GUI:** la insignia del guardián UMA, la alerta de presión DXGI al 85 % y el contador de intervenciones. Es la mitad del antiguo "Pilar 1".
- **EcoQoS para el fondo:** ya se aplica a los procesos frenados (`cpu_orchestrator.cpp:301-306`). En Intel híbrido, EcoQoS manda esos hilos a los E-cores por sí solo (ver la fase 7).
- **Opción `restrict_game_cpu_sets`:** viene apagada. Con eso, el juego no se restringe a ningún núcleo; solo se confina el fondo.

## 3b. Arquitectura y estructura de destino

Esta es la decisión del [ADR 0001](../../adr/0001-nucleo-que-decide-y-puertos.md): **Rust decide; C++ actúa a través de puertos.** Un puerto es una interfaz pequeña (por ejemplo, "escribir un archivo de forma segura") que se puede cambiar por una versión falsa en las pruebas.

**Carpetas de hoy y de destino:**

```
HOY                                     DESTINO
src/app/       entradas + caso de uso   src/app/      solo entradas: main, cli, daemon, gui_mode, monitor
                                        src/session/  casos de uso: sesión de juego, perfil de motor,
                                                      restaurar, informes
src/detect/    detección + tipos        src/model/    tipos compartidos (hardware, política, juego)
src/optimize/  decide + actúa           src/ports/    interfaces: archivos, procesos, registro,
                                                      energía, memoria, equipo, configuración, diario, reloj
src/safety/    sandbox + FPS            src/safety/   la parte de decisión pasa a Rust y la de I/O a windows/
src/windows/   ntdll y privilegios      src/windows/  adaptadores Windows: registro, procesos, ETW,
                                                      DXGI, ntdll, privilegios, energía, archivos
src/bridge/    rust_bridge.h            src/bridge/   sin cambios
src/ui/        GUI                      src/ui/       GUI (sin cambios)
rust/.../src/  6 módulos planos         rust/.../src/policy/  una regla por archivo
                                        rust/.../src/ffi.rs   todas las vf_rust_* juntas
tests/test_main.cpp (2 157 líneas)      tests/unit/          casos de uso con puertos falsos
                                        tests/integration/   adaptadores, en el laptop
                                        tests/vectors/       ejemplos compartidos C++/Rust
```

**Por qué cada carpeta:**
- `session/`: hoy la "sesión de juego" está mezclada con la CLI y la GUI. Separada, se puede probar sin abrir ventanas.
- `model/`: corta la dependencia circular entre `detect/` y `optimize/`, que hoy comparten tipos.
- `ports/` y adaptadores en `windows/`: así las pruebas de los casos de uso dejan de tocar el registro y el plan de energía reales. Esas pruebas pasan a usar puertos falsos, y las que tocan Windows de verdad quedan en `tests/integration/`.
- `windows/` y `bridge/` no se renombran: renombrar no arregla nada y obligaría a tocar los scripts de compilación sin motivo.
- `rust/.../policy/`: cada regla en su archivo, con su tabla de ejemplos.

### Quién puede depender de quién

Es como los permisos de un edificio: cada planta puede bajar a las de abajo, pero nunca subir. Si una pieza de abajo necesitara a una de arriba, se formaría otra vez un ciclo como el de hoy entre `detect/` y `optimize/`.

| Carpeta | Puede incluir | Nunca incluye |
|---|---|---|
| `app/` (entradas) | `session/`, `ui/`, `model/`, `windows/` (solo para armar todo en `main`) | — |
| `ui/` (GUI) | `session/`, `model/` | `windows/` directamente, salvo Win32 para dibujar |
| `session/` (casos de uso) | `model/`, `ports/`, `bridge/` | `windows/`, `<windows.h>` |
| `ports/` (interfaces) | `model/` | todo lo demás |
| `windows/` (adaptadores) | `ports/`, `model/`, `bridge/` | `session/`, `app/`, `ui/` |
| `model/` (tipos) | la librería estándar y, mientras haga falta, los tipos básicos de `<windows.h>` (`HWND`, `ULONG`) | `ports/`, `windows/`, `session/`, `app/`, `ui/` |
| `detect/`, `optimize/`, `safety/` (lo viejo, mientras dure la migración) | lo mismo que hoy, salvo el ciclo: `detect/` ya no incluye `optimize/` | `session/`, `app/`, `ui/` |
| `rust/` (núcleo) | nada del C++ | — |

Hoy `DetectedGameProcess` guarda un `HWND` y `CpuTopologyProfile` usa `ULONG`. Cambiarlos por tipos neutros ya es cambiar lógica, así que en R1 se permiten y se quitan cuando cada pieza pasa por su fase. Las carpetas viejas (`detect/`, `optimize/`, `safety/`) desaparecen a medida que sus piezas van a `windows/`, `session/` o al núcleo; en R3 ya no existen.

**Cómo se hace cumplir:** un script de unas 30 líneas (`scripts/check_layers.sh`) lee los `#include` de cada carpeta y falla si alguno rompe la tabla. Corre en el CI de GitHub. Sin él, la regla se olvida con el tiempo.

### Un módulo = el mismo nombre en todas las capas

Para "modularizar" no hacen falta más carpetas, sino que cada capacidad se llame igual en cada capa y se encuentre de un vistazo:

| Capacidad | Regla (Rust) | Puerto | Adaptador Windows | Caso de uso | Pruebas y ejemplos |
|---|---|---|---|---|---|
| Acceso a archivos | `policy/file_access.rs` | `ports/file_system.hpp` | `windows/safe_file_system.cpp` | todos | `tests/vectors/file_access.tsv` |
| Config. de juegos | `policy/ini_merge.rs` | `ports/file_system.hpp` | — | `session/engine_profile.cpp` | `tests/vectors/ini/` |
| Perfil del equipo (GPU, Tier y resto) | `policy/device_profile.rs` | `ports/device_probe.hpp` | `windows/device_probe.cpp` (DXCore, SetupAPI y demás) | `session/game_session.cpp` | `tests/vectors/device_profiles.tsv` |
| Memoria | `policy/memory_purge.rs` | `ports/memory_probe.hpp` (medir) y `ports/memory_control.hpp` (purgar) | `windows/memory.cpp` | `session/game_session.cpp` | `tests/vectors/memory_purge.tsv` |
| Procesos | `policy/process_class.rs` | `ports/process_control.hpp` | `windows/process_control.cpp` | `session/game_session.cpp` | `tests/vectors/process_class.tsv` |
| Núcleos de CPU | `policy/cpu_partition.rs` | `ports/process_control.hpp` | `windows/cpu_sets.cpp` | `session/game_session.cpp` | `tests/vectors/cpu_topology.tsv` |
| Ajustes de sistema | — | `ports/registry.hpp` | `windows/registry.cpp` | `session/tweaks.cpp` | pruebas de integración |
| Energía | — | `ports/power.hpp` | `windows/power.cpp` | `session/tweaks.cpp` | pruebas de integración |
| Configuración | `policy/config_rules.rs` (rangos y validación) | `ports/config_store.hpp` | `windows/config_file.cpp` | todos | `tests/vectors/config/` |
| Diario de cambios | — | `ports/change_journal.hpp` | `windows/change_journal.cpp` | `session/restore.cpp` | pruebas de integración |
| Bibliotecas de Steam | `policy/steam_library.rs` | `ports/file_system.hpp` | — | `session/engine_profile.cpp` | `tests/vectors/steam_library/` |

Así, quien quiera cambiar "cómo se eligen los procesos" sabe que toca `process_class` en cada capa y nada más.

## 3c. Librerías y herramientas

El proyecto se compila con **MinGW (g++)**, no con Visual Studio. El núcleo Rust no tiene dependencias y el daemon debe quedar por debajo de 15 MB. Por eso se prefieren las herramientas que solo se usan al compilar o en las pruebas, porque no entran en el programa final.

| Herramienta | Qué hace | Provecho en VegaFlow | ¿Entra en el programa? | Fase | Decisión |
|---|---|---|---|---|---|
| **cbindgen** (MPL-2.0) | Genera `rust_bridge.h` a partir del código Rust | Hoy el contrato entre C++ y Rust está escrito a mano en los dos lados (`VF_MAX_LOGICAL_PROCESSORS` está repetido). Si un lado cambia y el otro no, se lee memoria equivocada sin dar error | No: corre al compilar, en la máquina que compila | R1 | **Sí.** Si falla en algún caso (su documentación lo avisa), la cabecera se queda escrita a mano y una prueba compara los tamaños de los structs |
| **doctest** (MIT) | Marco de pruebas de un solo archivo | Partir `test_main.cpp` por módulo, correr solo las pruebas de una fase y ver los fallos por nombre. Es el más ligero de los tres que se miraron | Solo en el ejecutable de pruebas | R1 | **Sí.** Su documentación nombra GCC, no MinGW: se confirma al compilarlo en el primer paso de R1 |
| **proptest** (MIT/Apache) | Inventa miles de entradas para romper una regla | Rutas raras (fase 1), `.ini` en UTF-16, con BOM o sin marcador (fase 2) y topologías inventadas (fases 3 y 7) | No (`dev-dependency`) | 1, 2, 3, 7 | **Sí** |
| **cargo-mutants** | Cambia el código a propósito y mira si alguna prueba lo nota | Mide si las pruebas protegen de verdad (por ejemplo, si nadie nota que el 85 % pasó a 86 %) | No, es una herramienta aparte | Al cerrar cada fase | **Sí**, en el laptop o en el CI. No está verificado que funcione en Windows; si no, corre en el CI de Linux |
| **cargo-fuzz** | Lanza entradas aleatorias sin parar | Lo mismo que proptest, durante horas | No | 1 y 2 | **Opcional.** Pide la versión nightly de Rust |
| **RAII propio** (unas 50 líneas en `windows/`) | Cierra solo las claves del registro y los handles aunque haya un error | Hoy se cierran a mano (`RegCloseKey`); en un camino de error se quedan abiertas, y el daemon corre horas | Sí, pero es mínimo | R2 | **Sí**, en lugar de **WIL**: WIL no soporta MinGW (sus mantenedores cerraron la petición en 2023) |
| **json.h** (Unlicense) | Lector de JSON de un solo archivo | `config.cpp` busca las claves de `config.json` como texto; un archivo editado a mano con algo raro se lee mal sin avisar | Sí, pequeño (el tamaño se mide al compilar) | 1b | **Sí**, frente a nlohmann/json, que es mucho más grande |
| **PresentMon (PresentData)** (MIT) | El código de Intel que lee los fotogramas por ETW | Ninguno en el daemon: la fase 6 quita la captura. Se sigue usando PresentMon solo en `scripts/bench.ps1`, para medir en desarrollo | No | — | **No** en el daemon |
| cxx | Puente más completo entre Rust y C++ | Poco: por la frontera solo pasan datos simples. Su CI no prueba MinGW | Sí | — | **No** |
| WIL, krabsetw, GoogleTest | RAII de Windows, ETW y pruebas | — | — | — | **No:** no soportan MinGW o no lo prueban |
| keyvalues-parser | Lector del formato de Steam | Lo usaría la lista blanca | Sí, y arrastra otra librería (`pest`) | — | **No:** sería la primera dependencia ajena justo en la parte que decide la seguridad. El lector de `libraryfolders.vdf` pasa a Rust (`policy/steam_library.rs`) en la fase 1, donde proptest y fuzzing sí pueden atacarlo |
| hwloc | Mapa detallado de la CPU | Ninguno: `GetSystemCpuSetInformation` ya da el tipo de núcleo, la caché compartida y el núcleo físico | Sí, grande | — | **No** |

**El criterio:**
- se adopta lo que no entra en el programa;
- lo que entra tiene que arreglar un fallo real y compilar con MinGW;
- el núcleo que decide la seguridad no lleva código ajeno.
