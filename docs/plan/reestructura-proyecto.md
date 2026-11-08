# Reestructura del proyecto: ¿pasar todo a Rust?, estructura de archivos y qué se commitea

Fecha: 2026-11-08. Estado: propuesta, nada de esto está aplicado. Los números salen de `git ls-files`, `wc -l`, `grep` y de un experimento en una copia del crate Rust; lo que no se midió se dice como tal en "Límites".

## 1. La decisión sobre Rust

**Concepto.** Hoy el proyecto ya tiene un reparto con nombre: Rust *decide* (reglas puras) y C++ *actúa* (llamadas a Windows). En inglés se llama *functional core, imperative shell*. La pregunta es si conviene mover también las acciones a Rust; la respuesta con estos datos es que no, y que sí conviene mover a Rust más *decisiones*.

### Capa de ingeniería (con datos)

| Medida | Valor |
| --- | --- |
| C++ de producto (`src/` + `include/`) | 14 964 líneas en 16 `.cpp`, 15 `.hpp` y un `.h`; tests aparte: 2 107 |
| Rust (`rust/vegaflow-core`) | 1 102 líneas, 0 dependencias (`Cargo.lock` de 7 líneas), 15 tests |
| Frontera entre los dos | 7 funciones `vf_rust_*` y 5 estructuras de datos planos |
| Llamadas distintas a Windows en C++ | 178 (estimación por patrón de nombres) |
| Banderas de la línea de comandos | 23 |
| Pruebas de C++ | 31; varias cambian registro, plan de energía o prioridades de verdad y un guardián final los restaura |

Dónde está la dependencia de Windows (llamadas totales por archivo, estimación por patrón):

| Archivo | Líneas | Llamadas | Qué es |
| --- | --- | --- | --- |
| `optimizers/graphics_optimizer.cpp` | 2 302 | 347 | Registro, plan de energía, MMCSS: fontanería |
| `main.cpp` | 1 947 | 116 | Arranque, banderas, bucle del daemon |
| `gui/*` (3 archivos) | 1 997 | 331 | GUI Win32/GDI |
| `optimizers/fsr_scaler.cpp` | 1 136 | 61 | Compilación HLSL y D3D |
| `security/sandbox_guard.cpp` | 696 | 20 | Sobre todo política de rutas: casi lógica pura |
| `core/hardware_profiler.cpp` | 632 | 12 | Clasificador de Tiers: casi lógica pura |
| `optimizers/steam_engine_optimizer.cpp` | 1 529 | 16 | Lógica de motores y copias de seguridad |

Dónde está la dependencia de Windows en Rust: solo `uma_memory.rs` declara `extern "system"` de `ntdll` (2 funciones). `cpu_governor.rs` y `sandbox_core.rs` son puros.

**Por qué no un port completo:**

1. **Reinicia la evidencia.** Las cifras medidas (+40% de Octopath, el coste de 19 a 24% de confinar al juego, las 42 filas de cobertura) son de este binario. Un port completo obliga a repetirlas todas.
2. **No quita el viaje al laptop.** Hoy compilar y probar necesita el laptop. Con todo en Rust se podría compilar desde el Mac, pero las pruebas que cambian registro y plan de energía seguirían necesitando Windows.
3. **Es reescribir la parte que no es lógica.** `graphics_optimizer`, `main` y la GUI suman 6 246 líneas y 794 llamadas a Windows. En Rust seguirían siendo llamadas `unsafe` (con `windows-sys`), sin ganar seguridad de memoria donde casi no hay memoria propia que gestionar.
4. **El rendimiento no es el motivo.** El daemon duerme y usa menos de 2 MB; lo que se mide es la planificación de Windows, no el lenguaje.

**Por qué sí más Rust, y dónde:** un daemon con privilegios que lee rutas y escribe en el registro gana donde hay lógica de seguridad pura y un fallo cuesta caro. Regla para decidir qué pasa a Rust: que sea **lógica pura, que un fallo sea caro, que se pueda probar en el Mac y que cruce la frontera con datos planos** (el patrón que ya existe).

### Experimento (evidencia)

El comando `cargo test` del crate hoy **no enlaza en el Mac**: `ld: library 'ntdll' not found` (comprobado el 2026-11-08). En una copia, con `#[cfg(windows)]` sobre el bloque `extern "system"` de `uma_memory.rs` y dos funciones de reemplazo que devuelven -1 fuera de Windows, el resultado fue **15 de 15 pruebas en verde en el Mac, en 0.01 s**. En Windows el código compilado no cambia porque la rama `cfg(windows)` es la misma de hoy. Falta confirmar eso compilando en el laptop.

### Capa sencilla

Piensa en una cocina. Rust es el recetario: dice cuánta sal, cuándo parar, qué ingrediente no se toca. C++ son las manos que abren la nevera y encienden el fuego. El recetario se puede revisar en cualquier escritorio, sin cocina. Las manos solo se prueban en la cocina. Pasar todo a Rust sería cambiar también las manos por otras nuevas: hay que volver a comprobar que todo se cocina igual. Lo que conviene es **escribir más recetas en el recetario** (las reglas de seguridad y de clasificación) y dejar las manos como están.

### Antes y después

| | Antes | Después (opción 1) |
| --- | --- | --- |
| Probar las reglas de Rust | Solo en el laptop (no enlaza en el Mac) | En el Mac, en menos de 1 s |
| Política de rutas de `SandboxGuard` | C++, probada con el sistema real (Test 17) | Rust, con los mismos vectores y pruebas unitarias en el Mac; C++ solo canonicaliza y hace el I/O |
| Clasificador de Tiers | C++ dentro de `hardware_profiler` | Rust, con Test 25 como referencia de paridad |
| Acciones (registro, energía, GUI, FSR) | C++ | C++ (sin cambios) |

### Opciones

| Opción | Qué incluye | Riesgo |
| --- | --- | --- |
| **1. Núcleo puro y más decisiones en Rust (recomendada)** | Aislar `ntdll`; pasar a Rust la política de `SandboxGuard` y el clasificador de Tiers; GUI y optimizadores en C++ | Bajo: cada paso tiene su prueba de paridad existente |
| 2. Sin cambios de Rust | Solo estructura de archivos y política de commits | Ninguno; el Rust sigue sin probarse en el Mac |
| 3. Port completo | Todo a Rust, GUI incluida | Alto: reescribe 15 000 líneas, las 23 banderas, las 31 pruebas y obliga a repetir las mediciones |

## 2. Paridad: qué define "completo" (método `scope-inventory`)

Fuentes de verdad para cualquier cambio de la opción 1, en orden de confianza:

| Fuente | Qué cubre | Estado en la propuesta |
| --- | --- | --- |
| Las 23 banderas de `main.cpp` | Interfaz de línea de comandos | Igual (no se toca) |
| `include/core/rust_bridge.h` (7 funciones, 5 estructuras) | Contrato C++ ↔ Rust | Igual; solo se añaden funciones, no cambia ninguna |
| Test 17 (SandboxGuard) y Test 25 (Tiers) | Comportamiento actual de lo que se movería | Es la medida: el Rust nuevo debe dar las mismas respuestas con los mismos vectores |
| 31 pruebas de C++ y 15 de Rust | Regresión | Igual: deben seguir en 31/31 y 15/15 (más las nuevas) |
| `scripts/tweaks_state.ps1` antes y después | Que no queden valores fuera de fábrica | Igual |
| Filas de `.claude/PRUEBAS.md` ("Cobertura", 42) | Qué está cubierto | Igual; las filas de SandboxGuard y Tiers pasan a cubrirse también con Rust |

Falta: ninguna de las dos funciones nuevas (política de rutas y Tiers) tiene hoy un archivo de vectores independiente; el primer paso es extraerlos de Test 17 y Test 25.

## 3. Estructura de archivos propuesta

| Ruta | Hoy | Propuesta | Estado |
| --- | --- | --- | --- |
| `CLAUDE.md`, `README.md`, `.gitignore` | versionados | igual | Igual |
| `docs/` (4 carpetas, 16 documentos) | versionado | igual | Igual |
| `rust/vegaflow-core/` | lib con `ntdll` en `uma_memory.rs` | `ntdll` detrás de `cfg(windows)`; módulos de política de rutas y Tiers | Distinto |
| `src/`, `include/` | C++ de acciones y GUI | igual; `sandbox_guard` y `hardware_profiler` llaman al Rust para decidir | Igual salvo las dos llamadas |
| `tests/test_main.cpp` | 31 pruebas | igual | Igual |
| `scripts/` | 24 `.ps1`, 4 `.py` | igual | Igual |
| `scripts/fetch_dxvk.ps1` | no existe | descarga DXVK con versión y sha256 fijos a `assets/dxvk/dxvk-2.5.3/` | Falta |
| `assets/dxvk/` | 26 MB de DLL de terceros, versionados | ignorado; lo llena `fetch_dxvk.ps1` en la misma ruta (el código ya busca ahí) | Distinto |
| `config/config.json` | valores por defecto, versionado | igual | Igual |
| `CMakeLists.txt` | solo lo usaba el CI que se quitó; `build.ps1` no lo usa | borrar | Distinto |
| `.agents/skills/` | versionado | igual (pendiente) | Igual |
| `scripts/remote/archive/` | `vfjob` retirado el 2026-11-05 | borrar el 2026-11-12 si no hubo problemas | Pendiente |

## 4. Qué se commitea y qué no

| Categoría | Qué | Regla |
| --- | --- | --- |
| **Sí** | Código (`src/`, `include/`, `rust/`), pruebas, `scripts/`, `config/config.json`, `Cargo.lock` (el crate se enlaza a un ejecutable), `docs/`, `docs/assets/` (12 MB de evidencia), `scripts/remote/baselines/*.json` (datos de la puerta de regresión) | Todo lo que otra persona necesita para compilar, probar y entender |
| **No** | `.claude/`, `.DS_Store`, `__pycache__/`, `build/`, `rust/**/target/`, `*.log`, `.vs/`, `.idea/` | Ya están en `.gitignore` |
| **Se descarga al compilar** | DLL de DXVK (26 MB), con versión y sha256 | Un binario de terceros no se versiona; se baja y se verifica |
| **Se evalúa** | Git LFS para `docs/assets/` | Solo una mención: 12 MB no lo justifican |

**Sobre sacar DXVK del repo:** quitarlo del árbol no reduce el historial (el paquete de git pesa 19 MB y esos binarios quedan dentro). Reescribir el historial es destructivo y lo decides tú; no lo recomiendo con este tamaño. El beneficio real es de higiene: un binario de terceros con una suma de control conocida en vez de un archivo opaco.

**Condición previa, sin comprobar:** de dónde salieron esos DLL. La documentación dice "DXVK-GPLAsync v2.5.3", que es una variante parcheada y no la versión oficial; si no hay una descarga estable de esa variante, `fetch_dxvk.ps1` no se puede escribir y los DLL se quedan en el repo. Esto se resuelve antes de tocar nada.

## 5. Escenarios de aceptación

Se ejecutan de punta a punta al terminar la opción 1.

1. **Rust en el Mac.** Dado el Mac sin Windows, cuando se corre `cargo test` en `rust/vegaflow-core`, entonces pasan 15 de 15 más las pruebas nuevas, y `nm` de la biblioteca compilada para `x86_64-pc-windows-gnu` sigue listando las 7 funciones `vf_rust_*`.
2. **Suite completa.** Dado un clon limpio en el laptop, cuando se ejecuta `build.ps1 -RunTests`, entonces pasan 31 de 31 pruebas de C++ y todas las de Rust, y `tweaks_state.ps1` no muestra valores fuera de fábrica.
3. **Paridad de rutas.** Dado el mismo conjunto de rutas de Test 17, cuando se evalúan con la política en Rust y con la de C++ actual, entonces permitir o denegar y el motivo son idénticos en todos los casos.
4. **Paridad de Tiers.** Dados los perfiles de hardware de Test 25, cuando se clasifican con Rust y con C++, entonces dan el mismo Tier y la misma política.
5. **Clon limpio.** Dado un clon sin los DLL de DXVK, cuando se ejecuta `fetch_dxvk.ps1`, entonces los DLL quedan en `assets/dxvk/dxvk-2.5.3/{x32,x64}` con la suma sha256 fijada y Test 16 pasa; después de compilar y correr, `git status` no muestra archivos sin versionar.

## 6. Orden y límites

Orden: (1) `cfg(windows)` en el crate, probado en el Mac y compilado en el laptop; (2) vectores de Test 17 y Test 25 en archivos propios; (3) política de rutas a Rust; (4) Tiers a Rust; (5) DXVK, solo si se confirma la procedencia; (6) borrar `CMakeLists.txt`.

Límites de este documento:
- Las llamadas a Windows se contaron por patrón de nombres: son una estimación, no un análisis del código.
- No se estimó esfuerzo en horas; el orden va de menor a mayor riesgo.
- El experimento de `cfg(windows)` se hizo en una copia y en el Mac; la compilación para Windows con ese cambio está sin comprobar.
- No se comprobó si algo más usa `CMakeLists.txt` aparte del CI quitado.
