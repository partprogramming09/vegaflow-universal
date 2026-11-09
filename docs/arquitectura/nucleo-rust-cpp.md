# Núcleo híbrido C++20 + Rust

## 1.3 Arquitectura del Puente Híbrido C++20 ↔ Rust (`libvegaflow_core.a`)

Para garantizar que el propio software de optimización no introduzca latencia ni compita por la memoria del sistema, toda la lógica algorítmica y de evaluación en tiempo real fue desarrollada en **Rust**, compilada como `staticlib` sin dependencia de la biblioteca estándar de C runtime (CRT) en el bucle caliente, operando exclusivamente sobre estructuras POD (*Plain Old Data*) `#[repr(C)]` ubicadas en la pila:

```
┌─ Capa de Sistema C++20 (Win32 / DXGI / GUI) ───────────────────────────────────────────────┐
│ main.cpp                Bucle de servicio y despacho                                       │
│ hardware_profiler.cpp   Topología Win32 y detección                                        │
│ cpu_orchestrator.cpp    Gestión de Handles de proceso                                      │
│ memory_manager.cpp      Presupuesto DXGI 1.4                                               │
│ sandbox_guard.cpp       Validación perimetral                                              │
└────────────────────────────────────────────────────────────────────────────────────────────┘
        │
        ▼  hardware_profiler, cpu_orchestrator, memory_manager y sandbox_guard ──► rust_bridge.h
┌─ Puente C ABI #[repr(C)] ──────────────────────────────────────────────────────────────────┐
│ rust_bridge.h   Estructuras POD de tamaño fijo                                             │
└────────────────────────────────────────────────────────────────────────────────────────────┘
        │
        ▼  rust_bridge.h ──► ffi.rs (todas las vf_rust_*) ──► reglas en policy/ y llamadas NT en nt_memory.rs
┌─ Núcleo Determinista en Rust (rust/vegaflow-core) ─────────────────────────────────────────┐
│ cpu_governor.rs   • vf_rust_partition_cpu_topology()                                       │
│                   • vf_rust_eval_bg_confinement()                                          │
│ uma_memory.rs     • vf_rust_evaluate_uma_pressure()                                        │
│ nt_memory.rs      • vf_rust_query_nt_memory_lists(), vf_rust_purge_nt_standby_list()       │
│ sandbox_core.rs   • vf_rust_is_critical_system_filename()                                  │
│                   • vf_rust_has_anticheat_signature()                                      │
│ apu_tier.rs       • vf_rust_classify_apu_tier()                                            │
│ path_policy.rs    • vf_rust_classify_blacklisted_path()                                    │
└────────────────────────────────────────────────────────────────────────────────────────────┘
        │
        ▼  nt_memory.rs ──► NT1  |  cpu_orchestrator.cpp ──► NT2 (llamada directa desde C++)
┌─ Kernel NT (ntdll.dll / kernel32.dll) ─────────────────────────────────────────────────────┐
│ NT1  NtQuerySystemInformation(Class 80)                                                    │
│      NtSetSystemInformation(Purge Standby)                                                 │
│ NT2  SetProcessDefaultCpuSets                                                              │
│      SetProcessInformation(EcoQoS)                                                         │
│      NtSetInformationProcess(I/O VeryLow)                                                  │
└────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Módulos del Núcleo Rust:
* **[`cpu_governor.rs`](../../rust/vegaflow-core/src/policy/cpu_governor.rs):** Recibe la lista plana de identificadores de CPU Sets del sistema, calcula los índices de pertenencia a complejos de núcleos y determina si un proceso ruidoso debe ingresar o salir del estado de confinamiento mediante un algoritmo de histéresis sin asignaciones dinámicas.
* **[`uma_memory.rs`](../../rust/vegaflow-core/src/policy/uma_memory.rs) y [`nt_memory.rs`](../../rust/vegaflow-core/src/nt_memory.rs):** `nt_memory.rs` enlaza directamente contra `ntdll.dll` (solo en Windows: en otros sistemas esas llamadas devuelven error y el núcleo se puede compilar y probar igual) invocando `NtQuerySystemInformation(SystemMemoryListInformation)`; `uma_memory.rs` es solo la regla. Evalúa en microsegundos si el subsistema de memoria compartida supera el umbral crítico (`>= 85%` de presupuesto o `< 1536 MB` de RAM física disponible), gobernando la purga selectiva de la lista de espera (*Standby List*) únicamente cuando el bus de disco opera en reposo (`< 15 MB/s`).
* **[`apu_tier.rs`](../../rust/vegaflow-core/src/policy/apu_tier.rs):** Decide el Tier de la APU (1 a 4) a partir de la arquitectura de memoria, la generación de la GPU, la RAM y los núcleos físicos. Es una decisión pura sobre números: el mismo archivo de vectores (`tests/vectors/apu_tier.tsv`) lo comprueban Rust y la prueba 32 de C++.
* **[`path_policy.rs`](../../rust/vegaflow-core/src/policy/path_policy.rs):** Decide por qué una ruta canónica está prohibida (raíz de disco, archivos críticos de arranque, carpetas de Windows, Common Files, System Volume Information) o si está permitida por la blacklist. Trabaja con UTF-16 ya en minúsculas, tal como lo tiene C++, así que no cambia mayúsculas ni puede discrepar con texto no ASCII. Se comprueba con `tests/vectors/sandbox_blacklist.tsv`.
* **[`sandbox_core.rs`](../../rust/vegaflow-core/src/policy/sandbox_core.rs):** Recorre la ruta en bytes UTF-8 (tiempo proporcional a su longitud) y responde sí o no: si contiene la firma de un anti-trampas comercial (`vf_rust_has_anticheat_signature`) o si el nombre del archivo es uno de arranque del sistema (`vf_rust_is_critical_system_filename`). Quien decide bloquear es `SandboxGuard` en C++.

---

## 1.4 Ciclo de Vida del Daemon y Gobernanza de Procesos

El diagrama de secuencia describe el ciclo de vida continuo del servicio desde su arranque en sesión elevada hasta la terminación de la aplicación:

```
Participantes:  U = Usuario / Sistema        D = Daemon C++20 (main.cpp)
                R = Núcleo Rust (vegaflow-core)   K = Kernel Windows NT / Driver GPU
                G = Proceso de Juego 3D

 1  U ──► D  Inicia servicio vegaflow_daemon.exe
 2  D ──► R  vf_rust_partition_cpu_topology(topología lógica)
 3  R ──► D  Partición calculada: Game Sets [0..9] | Background [10..11]
 4  D ──► K  Crea respaldo en Registro (RegBackup) y configura EPP

 ┌─ loop: cada 1.5 s en reposo (0.00% CPU, 1.99 MB RAM) ───────────────────────────────────┐
 │  5  D ──► K  Monitorea ventana en primer plano (GetForegroundWindow)                    │
 └─────────────────────────────────────────────────────────────────────────────────────────┘

 6  U ──► G  Ejecución del Juego (ej. Octopath Traveler II)
 7  D ──► R  vf_rust_has_anticheat_signature(ruta del binario)
 ┌─ alt: presencia de Anti-Cheat (ej. HoYoKProtect / BattlEye) ────────────────────────────┐
 │  8  R ──► D  Retorna LOCKDOWN -> Se deshabilitan escrituras en disco                    │
 ├─ else: binario sin Anti-Cheat ──────────────────────────────────────────────────────────┤
 │  8  R ──► D  Perfil estándar -> Aplica Preset optimizado de motor                       │
 └─────────────────────────────────────────────────────────────────────────────────────────┘

 ┌─ loop: durante la sesión de juego ──────────────────────────────────────────────────────┐
 │  9  D ──► R  vf_rust_eval_bg_confinement(telemetría de CPU de procesos)                 │
 │  ┌─ alt: proceso de fondo supera umbral (CPU >= 0.5) ────────────────────────────────┐  │
 │  │ 10  R ──► D  Confinamiento requerido                                              │  │
 │  │ 11  D ──► K  Aplica Afinidad 0xC00 + EcoQoS + I/O VeryLow                         │  │
 │  ├─ else: proceso de fondo cae bajo umbral (CPU < 0.25) ─────────────────────────────┤  │
 │  │ 10  R ──► D  Liberación permitida                                                 │  │
 │  │ 11  D ──► K  Restaura Afinidad global y Prioridad normal                          │  │
 │  └───────────────────────────────────────────────────────────────────────────────────┘  │
 │  12  D ──► R  vf_rust_evaluate_uma_pressure()                                           │
 │  ┌─ opt: presión de memoria crítica (>85% y disco inactivo) ─────────────────────────┐  │
 │  │ 13  R ──► K  NtSetSystemInformation (Purga Standby List)                          │  │
 │  └───────────────────────────────────────────────────────────────────────────────────┘  │
 └─────────────────────────────────────────────────────────────────────────────────────────┘

14  U ──► G  Cierre del Juego
15  D ──► K  RestoreAll() -> Revierte afinidades, prioridades, energía y registro
```

---


---

## 1.6 Doble Capa: Explicación de Ingeniería Profunda vs. Explicación Sencilla

### Capa 1: Perspectiva de Ingeniería Profunda
En la prueba sintética, confinar el juego a un subconjunto de núcleos (*Core Pinning* del juego) restaba un **-18.9%** de rendimiento debido a que los hilos auxiliares de compilación y despacho D3D11 quedaban asfixiados. La arquitectura óptima es **asimétrica**: mantener el juego con acceso libre a todos los núcleos físicos y reubicar de manera forzada únicamente a los intrusos en el último complejo físico. Al confinar los 6 workers a los hilos lógicos 10 y 11, la caché L3 asociada a los núcleos 0 a 4 permanece libre de desalojos forzados, permitiendo que la tasa de aciertos (*Cache Hit Rate*) supere el 96%, reduciendo los ciclos de parada de ejecución (*Stall Cycles*) en la tubería de despacho de comandos de la GPU.

### Capa 2: Explicación Sencilla y Cotidiana (La Autopista de 6 Carriles y los Chefs de Cocina)
* **La Autopista de 6 Carriles:** Imagina una autopista de 6 carriles donde un automóvil deportivo (el juego) necesita viajar a 100 km/h sin frenar. Sin regulación, una docena de camiones de carga pesada y furgonetas de reparto (navegadores, actualizaciones, tareas del sistema) circulan zigzagueando por todos los carriles. Aunque el deportivo sea potente, se verá obligado a frenar en seco cada vez que un camión se le cruce en su carril. VegaFlow actúa como un policía de tránsito de alta velocidad: no le pone límites al vehículo prioritario, pero **obliga a todos los camiones pesados a circular exclusivamente por el carril lento de la extrema derecha**. Con los otros 5 carriles despejados, el vehículo rueda a máxima velocidad sin interrupciones.
* **El Restaurante y los Chefs:** Un procesador de 6 núcleos funciona como 6 cocineros en la misma cocina. Si los clientes de una fiesta ruidosa entran a la cocina y le piden favores a los 6 cocineros a la vez, los cocineros pierden las comandas del plato principal y la comida sale con retraso (los tirones en pantalla). VegaFlow asigna a **un solo cocinero en una barra auxiliar para atender a los invitados ruidosos**, dejando a los otros 5 cocineros dedicados por completo a servir el banquete del juego.

---


---

## Especificación de Diseño: Fusión Híbrida Rust + C++20 para Control de Memoria UMA/APU, Integración MCP `metal-gpu-trace` y Flujo TDD

**Fecha:** 2026-11-06  
**Rama Base:** `feat/ab-automation` (`ff73341`)  
**Hardware de Referencia:** AMD Ryzen 5 4600H (6C/12T Zen 2, 2x CCX de 8 MB L3) + Radeon RX Vega 6 (GCN 5.0, UMA 16 GB DDR4 Dual-Channel)  
**Estado:** Validado en Brainstorming

---

### 1. Contexto y Motivación

En arquitecturas APU con memoria unificada (UMA), la CPU y la iGPU comparten el mismo bus DDR4 de 128 bits (~38–42 GB/s). En el daemon de VegaFlow Universal, mantener una huella de memoria privada ultra-baja (`< 15.0 MB`, actualmente ~2.5 MB) y `0.0%` de uso de CPU en reposo es un requisito físico indispensable para no contaminar la caché L3 ni competir por el ancho de banda del bus Infinity Fabric.

La migración progresiva hacia una **arquitectura híbrida Rust + C++20** mediante enlace estático (`staticlib`, `x86_64-pc-windows-gnu`) permite:
1. **Cero Asignaciones en Heap (Zero-Allocation Hot Path):** Evaluar presión de memoria UMA (`EvaluateUmaPressureGuard`), histéresis de confinamiento de CPU (`CpuOrchestrator`), particionamiento topológico de islas L3/CCX (`PartitionCpuSetsFromTopology`) y validación de firmas Anti-Cheat (`SandboxGuard`) utilizando arreglos en pila (*stack arrays*), tipos escalares `#[repr(C)]` y contadores atómicos sin invocar al asignador dinámico en cada ciclo del daemon.
2. **Seguridad de Memoria y Frontera FFI Blindada:** `panic = "unwind"` con `ffi_guard` en cada función exportada, validación estricta de punteros/longitudes antes de construir slices y ausencia total de excepciones a través de `extern "C"`. Si el cuerpo de una función falla (un error de programación), `ffi_guard` captura el pánico en la frontera y devuelve la respuesta cautelosa de esa función: denegar la ruta, asumir que hay anti-cheat, no confinar el fondo, no purgar memoria, Tier 1. Así un fallo en Rust nunca cruza a C++ ni mata el daemon. El crate se niega a compilar con `panic = "abort"`.
3. **Telemetría y Control en Laptop Integrados:** Registrar el servidor MCP `metal-gpu-trace` (`C:\dev\metal-gpu-trace\metal_trace_mcp.py` con `PresentMon.exe`) en `~/.gemini/config/mcp_config.json` y verificar la salud del equipo con el paquete de trabajos `scripts/remote/jobs/doctor.ps1`.
4. **Desarrollo Guiado por Pruebas (TDD Estricto):** Todo módulo en Rust se construye bajo el ciclo **RED $\to$ GREEN $\to$ REFACTOR** con `cargo test` y se verifica de extremo a extremo en la suite de integración `vegaflow_tests.exe` (`31 PASSED, 0 FAILED`).

---

### 2. Arquitectura del Subsistema Híbrido (`rust/vegaflow-core`)

```
+-----------------------------------------------------------------------------------+
|                        BINARIO ÚNICO (vegaflow_daemon.exe)                        |
|                    Enlace Estático MinGW-w64 (g++ 16.2 + Rust)                    |
+-----------------------------------------------------------------------------------+
|  Capa C++20 (GUI Win32/GDI, DXGI/D3D COM, Registro Windows, Orquestación Win32)   |
|    - MemoryManager        --->  src/bridge/rust_bridge.h  --->  uma_memory.rs   |
|    - HardwareProfiler     --->  (Contrato #[repr(C)] FFI)   --->  cpu_governor.rs |
|    - CpuOrchestrator      --->  (Cero Heap / Zero-Copy)     --->  cpu_governor.rs |
|    - SandboxGuard         --->                              --->  sandbox_core.rs |
+-----------------------------------------------------------------------------------+
|  Crate Rust: rust/vegaflow-core (staticlib + rlib, panic="unwind", opt-level=3)   |
|    - Llamadas nativas NTDLL (NtQuerySystemInformation / NtSetSystemInformation)   |
|    - Cálculos deterministas en pila sin std::vector / std::string en bucle activo |
+-----------------------------------------------------------------------------------+
```

#### 2.1. Configuración del Crate (`rust/vegaflow-core/Cargo.toml`)
- **Nombre del paquete:** `vegaflow-core` (`lib` name: `vegaflow_core`).
- **Tipos de crate:** `["staticlib", "rlib"]` (`rlib` para ejecutar `cargo test` nativamente y `staticlib` para generar `libvegaflow_core.a` hacia `g++`).
- **Perfil Release:**
  - `opt-level = 3`
  - `lto = true`
  - `codegen-units = 1`
  - `panic = "unwind"` (cada función exportada va envuelta en `ffi_guard`; el crate no compila con `abort`)
  - `strip = false` (el linker de `g++` gestiona los símbolos finales sin romper el archivo `.a`).
- **Cero dependencias externas pesadas:** Interacción directa con `ntdll.dll` mediante bloques `extern "system"` en Rust puro, evitando inflar tiempos de compilación o tamaño de binario.

---

### 3. Contrato FFI (`src/bridge/rust_bridge.h`) y Módulos en Rust

La cabecera `rust_bridge.h` se escribe a mano (no se genera con cbindgen: se decidió el 2026-11-09 para no añadir otra herramienta). Por eso la prueba C++ **Test 34** compara, struct por struct, el tamaño, la alineación y la posición de cada campo que ve C++ (`sizeof`, `alignof`, `offsetof`) con los que informa Rust (`vf_rust_bridge_layout`). Además compara el tamaño de cada campo, así que también detecta un tipo cambiado que cabe en el relleno (por ejemplo, un `bool` que pasa a `uint32_t`). Si alguien añade, quita, reordena o cambia de tipo un campo de la lista en un solo lado, la suite falla en vez de leer basura en tiempo de ejecución. Al añadir un struct nuevo al puente hay que añadirlo en los dos lados de esa prueba.

#### 3.1. Módulo `uma_memory`: Presión DXGI y Listas de Memoria NT
- **Estructuras `#[repr(C)]`**:
  ```c
  typedef struct VfUmaTelemetryInput {
      uint64_t total_physical_bytes;
      uint64_t free_and_zero_bytes;
      uint64_t standby_bytes;
      uint64_t local_budget_bytes;
      uint64_t local_usage_bytes;
      uint32_t safe_vram_pool_mb;
      bool vram_valid;
  } VfUmaTelemetryInput;

  typedef struct VfUmaGuardDecision {
      double dxgi_budget_utilization;
      bool guard_triggered;
      bool should_trim_background_ws;
      bool should_purge_standby;
      uint32_t total_interventions;
  } VfUmaGuardDecision;

  typedef struct VfNtMemoryListSample {
      uint64_t zero_page_bytes;
      uint64_t free_page_bytes;
      uint64_t modified_page_bytes;
      uint64_t standby_total_bytes;
      bool valid;
  } VfNtMemoryListSample;
  ```
- **Funciones exportadas (`extern "C"`)**:
  - `VfUmaGuardDecision vf_rust_evaluate_uma_pressure(const VfUmaTelemetryInput* input, uint32_t prev_interventions);`
    - Calcula `dxgi_budget_utilization = local_usage_bytes / local_budget_bytes` (si `vram_valid && local_budget_bytes > 0`).
    - Dispara el guard si `dxgi_budget_utilization >= 0.85` o si `free_and_zero_bytes < 1536 * 1024 * 1024`.
    - Incrementa `total_interventions` en `1` cuando `guard_triggered` es verdadero.
    - Determina si debe purgarse la Standby List (`standby_bytes > 512 MB`) y recortar working sets secundarios.
  - `VfNtMemoryListSample vf_rust_query_nt_memory_lists(void);`
    - Invoca directamente `NtQuerySystemInformation(SystemMemoryListInformation /* 80 */, ...)` en Rust y convierte el conteo de páginas (4096 bytes/página) a bytes exactos sin asignar memoria en heap.
  - `bool vf_rust_purge_nt_standby_list(bool dry_run);`
    - Si `dry_run == true`, devuelve `true` sin tocar el kernel. En modo real, invoca `NtSetSystemInformation(SystemMemoryListInformation /* 80 */, &cmd /* MemoryPurgeStandbyList = 4 */, sizeof(ULONG))`.

#### 3.2. Módulo `cpu_governor`: Topología L3/CCX e Histéresis de Confinamiento
- **Estructuras `#[repr(C)]`**:
  ```c
  #define VF_MAX_LOGICAL_PROCESSORS 64

  typedef struct VfCpuCoreEntry {
      uint32_t cpu_set_id;
      uint32_t core_index;
      uint32_t logical_processor_index;
      uint8_t last_level_cache_index;
      uint8_t efficiency_class;
  } VfCpuCoreEntry;

  typedef struct VfTopologyPartitionOut {
      uint32_t game_cpu_set_ids[VF_MAX_LOGICAL_PROCESSORS];
      uint32_t game_count;
      uint32_t background_cpu_set_ids[VF_MAX_LOGICAL_PROCESSORS];
      uint32_t background_count;
      uint32_t physical_cores;
      uint32_t logical_processors;
      bool has_multiple_ccx;
      bool has_smt;
  } VfTopologyPartitionOut;
  ```
- **Funciones exportadas (`extern "C"`)**:
  - `bool vf_rust_partition_cpu_topology(const VfCpuCoreEntry* entries, uint32_t count, VfTopologyPartitionOut* out);`
    - Ejecuta en arreglos de pila de tamaño fijo (`VF_MAX_LOGICAL_PROCESSORS = 64`) las reglas exactas de `HardwareProfiler::PartitionCpuSetsFromTopology`:
      1. Conteo de núcleos físicos únicos, detección de SMT (`logical > physical`) y múltiples islas L3 (`has_multiple_ccx`).
      2. Selección de núcleos de fondo:
         - Topología híbrida (`min_eff != max_eff`): hilos con `efficiency_class < max_eff` van a fondo.
         - Islas L3 asimétricas (`has_multiple_ccx` y la isla secundaria tiene $\le 33\%$ del total de hilos): toda la isla secundaria va a fondo.
         - Islas L3 simétricas o CCX único: si `physical_cores >= 4`, el último núcleo físico completo (todos sus hilos SMT) va a fondo; si `physical_cores < 4`, solo el último procesador lógico (`1` hilo) va a fondo.
      3. Escudo DPC/ISR: si `physical_cores >= 4`, el núcleo físico más bajo (`Core 0`) se excluye de `game_cpu_set_ids` (salvo que dejaría al juego sin hilos).
      4. Ordenación de `game_cpu_set_ids` priorizando la isla L3 principal (`primary_llc`) para minimizar latencia inter-CCX.
  - `bool vf_rust_eval_bg_confinement(double logical_cores_used, bool currently_confined);`
    - Implementa la histéresis empírica de `CpuOrchestrator`:
      - Si `currently_confined`: permanece `true` mientras `logical_cores_used >= 0.25`.
      - Si `!currently_confined`: pasa a `true` cuando `logical_cores_used >= 0.50`.

#### 3.2b. Módulo `apu_tier`: Clasificador de Tiers
- **Función exportada (`extern "C"`)**:
  - `uint8_t vf_rust_classify_apu_tier(const VfTierInput* input);`
    - Devuelve 1 (entrada), 2 (media), 3 (avanzada) o 4 (discreta o a medida). El orden de las comprobaciones importa: primero discreta, luego entrada (RAM hasta 9 GB, hasta 4 núcleos o Intel UHD), luego avanzada (RDNA2/RDNA3+/Intel Xe o 22 GB o más). Una entrada nula devuelve la media.
    - `VfTierInput` es una estructura plana: `total_ram_mb`, `physical_cores` (0 = desconocido), `arch_gen`, `dedicated_vram` e `is_nvidia`.

#### 3.2c. Módulo `path_policy`: Política de Rutas de la Blacklist
- **Función exportada (`extern "C"`)**:
  - `uint32_t vf_rust_classify_blacklisted_path(const uint16_t* path, size_t path_len, const uint16_t* win_dir, size_t win_dir_len, const uint16_t* sys_dir, size_t sys_dir_len);`
    - Devuelve `VF_PATH_ALLOWED` (0) si ninguna regla coincide, o el código de la primera que coincide, en este orden: vacía (1), raíz de disco (2), archivo crítico (3), carpeta de Windows (4), Windows de otra unidad (5), Common Files (6), System Volume Information (7); si el propio clasificador falla devuelve `VF_PATH_INTERNAL_ERROR` (8), que también deniega la ruta. El orden forma parte del contrato.
    - Un directorio de Windows vacío coincide con toda ruta (prefijo vacío), igual que el código anterior de C++; en la práctica nunca llega vacío.

#### 3.3. Módulo `sandbox_core`: Escáner Zero-Allocation de Firmas Anti-Cheat y Rutas Críticas
- **Funciones exportadas (`extern "C"`)**:
  - `bool vf_rust_has_anticheat_signature(const char* utf8_path, size_t len);`
    - Busca de forma case-insensitive en el slice de bytes `[u8]` todas las firmas prohibidas (`easyanticheat`, `battleye`, `beservice`, `vgk.sys`, `vanguard`, `hoyokprotect`, `mhyprot`, `mhypbase`, `anticheatexpert`, `randgrid.sys`, `ace-base`, `acebase`, `blackcipher`, `equ8`, `nprotect`, `gameguard`, `xigncode`, `genshinimpact`, `zenlesszonezero`, `starrail`) sin crear copias en heap.
  - `bool vf_rust_is_critical_system_filename(const char* utf8_name, size_t len);`
    - Verifica nombres críticos de arranque/paginación (`bootmgr`, `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `ntldr`, `bootsect.bak`) en tiempo constante sin asignaciones.

---

### 4. Integración en el Pipeline de Compilación (`scripts/build.ps1`)

1. **Verificación del Target GNU en Rust:**
   - Asegurar que el target `x86_64-pc-windows-gnu` esté instalado (`rustup target add x86_64-pc-windows-gnu`).
2. **Paso de Compilación y Test de Rust en `scripts/build.ps1`:**
   - Antes de invocar `g++`, `scripts/build.ps1` compila `rust/vegaflow-core` con `cargo build --release --target x86_64-pc-windows-gnu --manifest-path rust/vegaflow-core/Cargo.toml`.
   - Si se pasa `-RunTests`, ejecuta primero `cargo test --target x86_64-pc-windows-gnu --manifest-path rust/vegaflow-core/Cargo.toml`.
   - Pasa la ruta de `rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a` a los argumentos de enlace de `vegaflow_daemon.exe` y `vegaflow_tests.exe` antes de las librerías del sistema (`-lntdll -lws2_32 -luserenv -ladvapi32 ...`).

---

### 5. Configuración del Servidor MCP `metal-gpu-trace`

1. **Archivo:** `C:\Users\<usuario>\.gemini\config\mcp_config.json`
2. **Configuración a registrar:**
   ```json
   "metal-gpu-trace": {
     "command": "C:\\dev\\mgt-venv\\Scripts\\python.exe",
     "args": [
       "-I",
       "C:\\dev\\metal-gpu-trace\\metal_trace_mcp.py"
     ],
     "env": {
       "PRESENTMON_EXE": "C:\\ProgramData\\VegaFlow\\tools\\PresentMon.exe",
       "PERF_OUT": "C:\\dev\\perf-runs"
     }
   }
   ```
3. **Verificación:**
   - Prueba de humo por protocolo JSON-RPC (`initialize`, `tools/list` y llamada a herramienta de estado/reporte) verificando que `metal_trace_mcp.py` responde limpiamente en Windows con `PresentMon.exe`.

---

### 6. Criterios de Aceptación y Plan de Verificación (TDD)

1. **Suite Unitaria Rust (`cargo test`):**
   - 100% de tests pasando en `rust/vegaflow-core` (UMA pressure guard, consultas NTDLL, partición de topología 4600H / Zen4c / 4C..8C, histéresis de confinamiento y firmas Anti-Cheat).
2. **Suite Integrada C++20 + Rust (`scripts/build.ps1 -RunTests`):**
   - Los 30 tests existentes más el nuevo **Test 31 (`Hybrid Rust FFI Core: Zero-Allocation UMA Guard, L3 Topology & Anti-Cheat Scanner`)** deben pasar con resultado **`TEST RESULTS: 31 PASSED, 0 FAILED`**.
3. **Invariantes de Hardware y Sistema (`vegaflow-methodology`):**
   - Huella de memoria privada (`Private Working Set`) del daemon y los tests **$< 15.0\text{ MB}$** (objetivo $< 3.5\text{ MB}$).
   - `vegaflow_daemon.exe --status` reportando telemetría coherente.
   - `scripts/remote/jobs/doctor.ps1` limpio (`fuera de fabrica` vacío, registro y plan de energía idénticos antes y después).
