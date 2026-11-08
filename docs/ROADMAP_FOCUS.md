# Hoja de Ruta y Enfoque de Ingeniería — VegaFlow Universal

**Fecha de Actualización:** 2026-11-08  
**Estado General:** Base Universal Consolidada y Verificada (31/31 Tests C++20 + 15/15 Tests Rust Pasados)  
**Documento Maestro:** [`docs/ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md`](ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md)

---

## 1. Base Completada: Fase Actual Consolidada

VegaFlow Universal ha alcanzado la madurez de su núcleo de optimización y arquitectura de software, validado en hardware real y suites sintéticas:

### 1.1. Arquitectura Modular Limpia y Pipeline de Compilación
- **Desacoplamiento Estructural:** El código base se organiza en cuatro dominios modulares estrictos:
  - [`include/core/`](../include/core) y [`src/core/`](../src/core): Hardware profiler, llamadas indocumentadas NTDLL, privilegios de seguridad y telemetría de sistema.
  - [`include/optimizers/`](../include/optimizers) y [`src/optimizers/`](../src/optimizers): Orquestación de CPU, optimizadores gráficos y de temporizador NT, DXVK, escalador FSR 1.0/NIS, gestión de memoria UMA y presets de motores.
  - [`include/security/`](../include/security) y [`src/security/`](../src/security): Subsistema Zero-Trust `SandboxGuard` y telemetría matemática de frametimes.
  - [`include/gui/`](../include/gui) y [`src/gui/`](../src/gui): Ventana nativa Win32/GDI en modo Slate/Zinc Dark, pestañas y componentes.
- **Automatización de Build:** Script [`scripts/build.ps1`](../scripts/build.ps1) con detección inteligente del compilador MinGW-w64 `g++` (en PATH o en Scoop), flags C++20 con enlace estático total (`-static -static-libgcc -static-libstdc++`) y ejecución opcional de pruebas con `-RunTests`.
- **Control de Versiones y Limpieza:** Directorio `build/` libre de residuos (`*.o`, `*.old*`, logs) y `.gitignore` estandarizado.

### 1.2. Motor Universal de Clasificación y Guards (`ApuCapabilityGuard`)
Especificado en [`docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md`](superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md) e implementado en [`src/core/hardware_profiler.cpp`](../src/core/hardware_profiler.cpp):
1. **Tier 1: Entry UMA (`Survival Guard`):**
   - Destinado a APUs con $\le$ 8–9 GB de RAM o $\le$ 4 núcleos físicos (Vega 3/6/8 básica, Athlon Mobile, Intel Gen 9–11 UHD).
   - Impone techo rígido de textura en Unreal Engine (1536 a 2048 MB) para evitar caídas OOM, limita DXVK a 3 hilos de compilación, teselación a 8x y techo STAPM a 3000 MHz.
2. **Tier 2: Mid Vega UMA (`Sweet-Spot Guard`):**
   - Diseñado para APUs GCN 5 / Vega con 12–20 GB RAM o CPUs Zen 2/Zen 3 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - Preserva exactamente los parámetros óptimos del banco de pruebas original: Pool VRAM a 4096 MB, 6 hilos dedicados para DXVK, teselación a 16x, desactivación de `AsyncCompute` en DX12 y STAPM a 3400 MHz.
3. **Tier 3: Advanced UMA (`Do-No-Harm Guard`):**
   - Para APUs modernas RDNA 2, RDNA 3 y RDNA 3.5 (Radeon 680M, 780M, 890M, Strix Point) e Intel Xe/Arc UMA o configuraciones con $\ge$ 22 GB de RAM.
   - **Do-No-Harm Guard:** Preserva el cómputo asíncrono activo (`D3D12.AsyncCompute=1` y `r.RDG.AsyncCompute=1`), no restringe los relojes de CPU (`cpu_stapm_cap_mhz = 0`) y asigna pool UMA generoso de 6144 a 8192 MB con teselación hasta 32x.
4. **Tier 4: Discrete / Custom (`Pass-Through Guard`):**
   - Operación transparente y segura en configuraciones con GPU dedicada (>2 GB VRAM dedicada).

### 1.3. Especialización Dual DirectX 11 y DirectX 12
- **DirectX 11:** Despliegue seguro de `DXVK-GPLAsync` adaptado al recuento de núcleos, limitador inteligente de teselación AMD (8x/16x/32x) y cola de baja latencia `FlipQueueSize=1`.
- **DirectX 12:** Guard dinámico de colas de cómputo asíncrono (`AsyncCompute`), presupuestos optimizados de sombras en cascada (CSM) y streaming proporcional de pools de texturas UMA en motores Unreal y Unity.

### 1.4. Runtime DXGI Pressure Guard y Blindaje Anti-Cheat
- **Monitoreo en Tiempo Real (`EvaluateUmaPressureGuard`):** Sondaje del presupuesto DXGI 1.4 (`IDXGIAdapter3`). Si el consumo local alcanza el **85%** o la memoria física libre es inferior a **1536 MB**, mitiga proactivamente mediante recorte de conjuntos de trabajo secundarios sin pausas perceptibles.
- **Zero-Trust Lockdown (`SandboxGuard`):** Aislamiento de rutas canónicas, whitelist estricta y bloqueo total ante firmas de kernel anti-cheat (HoYoKProtect, Vanguard, EAC, BattlEye, ACE).
- **Cobertura de Pruebas:** **31 de 31 pruebas unitarias e integradas C++20 y 15 de 15 pruebas Rust pasando en verde** (`TEST RESULTS: 31 PASSED, 0 FAILED`).

### 1.5. Núcleo Híbrido Rust FFI (`vegaflow-core`)
Implementado en el crate estático [`rust/vegaflow-core/`](../rust/vegaflow-core) y enlazado sin sobrecarga vía C ABI ([`include/core/rust_bridge.h`](../include/core/rust_bridge.h)):
- **Evaluador de Presión UMA (`src/uma_memory.rs`):** Funciones `vf_rust_evaluate_uma_pressure` y `vf_rust_evaluate_nt_memory_lists`. Algoritmos deterministas sin asignación dinámica de memoria en el heap (Zero-Allocation), verificando saturación local DXGI, reservas del sistema operativo y listas Standby de NT.
- **Topología L3 y Gobernanza de CPU (`src/cpu_governor.rs`):** Función `vf_rust_partition_topology_l3` para aislar hilos del juego en la caché L3 principal para arquitecturas simétricas o heterogéneas (Zen 2/3/4/5 y procesadores híbridos), junto con la máquina de estados e histéresis `vf_rust_evaluate_background_confinement` (umbrales de 0.50 y 0.25 núcleos lógicos).
- **Scanner Anti-Cheat y Blindaje de Archivos (`src/sandbox_core.rs`):** Detección formal y sin falsos positivos de firmas de Anti-Cheat de kernel (`vf_rust_contains_anticheat_signature`) y protección estricta contra modificaciones accidentales a binarios de arranque del sistema (`vf_rust_is_critical_system_file`).
- **Seguridad Incondicional:** Compilación estática con LTO completo, `panic = "abort"`, y verificación formal mediante 15 pruebas unitarias internas.

---

## 2. Auditoría Universal y Hoja de Ruta Pendiente (AMD + Intel + iGPU + dGPU + eGPU)

A partir de la auditoría exhaustiva detallada en [`docs/ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md`](ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md) (PARTE IV), se han registrado **5 brechas críticas en el código actual** que deben resolverse para garantizar soporte transversal sin degradaciones en ningún hardware:

### Las 5 Brechas Universales Registradas:
1. **Intel Híbrido (P+E) en Ring Bus (`cpu_governor.rs`):** Actualmente asigna todos los E-Cores al fondo (8 a 16 hilos), provocando inundación del Ring Bus y desalojo de Smart Cache L3. **Solución:** Confinar el fondo a un cluster acotado de 2 a 4 E-Cores finales.
2. **Preset Unreal Engine Lossless para dGPU/eGPU (`main.cpp` / `steam_engine_optimizer.cpp`):** Aplica recortes de calidad visual (`SSR=0`, `AO=0`, `Shadow Distance=0.6`) incluso en GPUs dedicadas Tier 4. **Solución:** Modo Lossless Ultra para dGPU y amortiguación de transferencias PCIe (`AmortizeCPUToGPUCopy=1`) para eGPU.
3. **Detección de Bus Gráfico eGPU vs dGPU (`hardware_profiler.cpp`):** Trata igual un enlace PCIe x16 interno y un túnel Thunderbolt/USB4 x4. **Solución:** Detectar tipo de interconexión (`GpuInterconnectType`) y aplicar políticas dedicadas (`PCIe ASPM = 0`, Core 0 libre para DMA host, `FlipQueueSize = 1`).
4. **AMD Multi-CCD (12C/16C) y 3D V-Cache (`cpu_governor.rs`):** En topologías dual-CCD reparte el juego en ambos CCDs, cruzando el puente Infinity Fabric (`~75 ns`). **Solución:** Asignar hilos preferentes del juego exclusivamente al `CCD 0` (con 3D V-Cache) y confinar el fondo en `CCD 1`.
5. **Escalabilidad a 128 Hilos y Drivers Multi-Vendor (`rust_bridge.h` / `graphics_optimizer.cpp`):** Límite rígido de 64 procesadores lógicos y dependencia de claves de registro AMD UMD. **Solución:** Elevar `VF_MAX_LOGICAL_PROCESSORS` a 128 y aplicar directivas universales NT independientemente del fabricante de la GPU.

### Plan de Ingeniería Pendiente de Implementación:
- [ ] **Tarea 1 (Rust `vegaflow-core`):** Topologías Universales de CPU (Intel Ring-Safe, AMD Multi-CCD/X3D, soporte de hasta 128 hilos) — *PENDIENTE DE IMPLEMENTACIÓN*.
- [ ] **Tarea 2 (C++20 HardwareProfiler & EngineOptimizer):** Detección de bus eGPU vs dGPU vs iGPU y presets Unreal Engine Lossless — *PENDIENTE DE IMPLEMENTACIÓN*.
- [ ] **Tarea 3 (C++20 `vegaflow_tests`):** Suite de pruebas determinista multi-hardware para los 6 perfiles universales (Prueba 32) — *PENDIENTE DE IMPLEMENTACIÓN*.

---

## 3. Próximos Pilares de Enfoque (GUI, Perfiles Dinámicos y Telemetría)

Concluida la fase de soporte universal de silicio, los desarrollos posteriores continuarán en los siguientes pilares de producto:

```
+--------------------------------------------------------------------------------------------------+
|                           PRÓXIMOS PILARES DE ENFOQUE DE INGENIERÍA                              |
+--------------------------------------------------------------------------------------------------+
|                                                                                                  |
|   +--------------------------+    +--------------------------+    +--------------------------+   |
|   |         PILAR 1          |    |         PILAR 2          |    |         PILAR 3          |   |
|   |    Integración Visual    |    |   Perfiles Dinámicos &   |    |    Topología Híbrida     |   |
|   |    ApuGuard en la GUI    |    |  Detección Cuello Bot.   |    |  Zen 4c/5c & Core Ultra  |   |
|   +--------------------------+    +--------------------------+    +--------------------------+   |
|   | - Badges de Tier (1..4)  |    | - Ratio Frametime CPU/GPU|    | - Separación P/E-Cores   |   |
|   | - Modo Guard activo      |    | - Auto-tuning FSR 1.0/NIS|    | - Aislamiento en L3 pura |   |
|   | - Barra DXGI con alerta  |    | - Presets por juego auto |    | - Soporte Strix / Arrow  |   |
|   | - Contador intervención  |    | - Zero-user intervention |    | - CPU Sets heterogéneos  |   |
|   +--------------------------+    +--------------------------+    +--------------------------+   |
|                                                |                                                 |
|                                   +--------------------------+                                   |
|                                   |         PILAR 4          |                                   |
|                                   |    Telemetría In-Game    |                                   |
|                                   |   Zero-Hook DirectFlip   |                                   |
|                                   +--------------------------+                                   |
|                                   | - Overlay click-through  |                                   |
|                                   | - Coste < 0.2ms GPU/CPU  |                                   |
|                                   | - 100% Anti-Cheat Seguro |                                   |
|                                   | - Métricas en tiempo real|                                   |
|                                   +--------------------------+                                   |
+--------------------------------------------------------------------------------------------------+
```

### Pilar 1: Integración Visual del `ApuCapabilityGuard` y Telemetría DX11/DX12 en la GUI
**Objetivo:** Elevar la instrumentación visual en la GUI nativa Win32/GDI ([`src/gui/gui_window.cpp`](../src/gui/gui_window.cpp), [`src/gui/gui_tabs.cpp`](../src/gui/gui_tabs.cpp)) para que el usuario visualice la toma de decisiones del motor en tiempo real.

- **Componentes a Implementar:**
  1. **Badge de Tier y Guard Activo en el Dashboard:** Mostrar en la tarjeta de Gráficos/UMA el Tier detectado (`Tier 1: Entry UMA`, `Tier 2: Mid Vega`, `Tier 3: Advanced UMA`, `Tier 4: Discrete`, `Tier 5: External eGPU`).
  2. **Barra Dinámica de Presupuesto DXGI 1.4:** Medidor gráfico del consumo local de VRAM UMA en porcentaje, iluminándose en ámbar/rojo con alerta si rebasa el umbral de presión del **85%**.
  3. **Indicador de Intervenciones Reactivas:** Contador acumulativo de intervenciones del `EvaluateUmaPressureGuard`.
  4. **Panel de Especialización DX11 vs DX12 en Pestaña Gráfica:** Detalle de hilos DXVK activos, estado de teselación AMD (8x/16x/32x) y protección de `AsyncCompute`.

### Pilar 2: Perfiles Dinámicos por Juego y Detección Automática de Cuello de Botella
**Objetivo:** Desarrollar un subsistema adaptativo dentro de [`FrameTelemetryMonitor`](../include/security/frame_telemetry.hpp) y [`ProcessDetector`](../include/core/process_detector.hpp) que identifique si el juego está limitado por CPU (*CPU-Bound*) o por GPU (*GPU-Bound*).

### Pilar 3: Topología Híbrida para APUs de Nueva Generación (`CpuOrchestrator`)
**Objetivo:** Adaptar el subsistema de afinidad y asignación de CPU Sets ([`src/optimizers/cpu_orchestrator.cpp`](../src/optimizers/cpu_orchestrator.cpp)) para arquitecturas de núcleos heterogéneos (Zen 5/Zen 5c, Intel Core Ultra) conectándose con las nuevas reglas universales de `vegaflow-core`.

### Pilar 4: Telemetría In-Game Zero-Hook sobre DirectFlip
**Objetivo:** Proveer una superposición en pantalla (*HUD / Overlay*) ultraligera que informe FPS, 1% Lows, 0.1% Lows, consumo de presupuesto UMA y estado del Guard, **sin requerir inyección de DLLs ni hooking de APIs gráficas** (100% Anti-Cheat Seguro).

---

## 4. Matriz de Prioridad y Secuencia de Ejecución

| Hito | Fase / Pilar | Impacto en Usuario | Complejidad Técnica | Dependencias Previas |
| :--- | :--- | :--- | :--- | :--- |
| **Inmediato** | **Soporte Universal (Tareas 1, 2 y 3)** | Crítico (Compatibilidad Intel Híbrido, Multi-CCD y dGPU/eGPU) | Alta (Rust FFI + C++20 DXGI/Bus) | Base consolidada (`ApuCapabilityGuard` y `vegaflow-core`) |
| **Fase 1** | **Pilar 1: GUI ApuGuard** | Alto (Visibilidad de estado y alertas) | Media (Win32/GDI) | Soporte Universal consolidado |
| **Fase 2** | **Pilar 2: Auto-Tuning CPU/GPU** | Muy Alto (Fluidez adaptativa) | Alta (Heurísticas de telemetría) | `FrameTelemetryMonitor` + `MemoryManager` |
| **Fase 3** | **Pilar 4: Overlay Zero-Hook** | Muy Alto (Experiencia in-game) | Media-Alta (Win32 Layered Window) | DirectFlip monitor + `FrameTelemetryMonitor` |
| **Fase 4** | **Pilar 3: Topología Híbrida Avanzada** | Alto (Nuevas plataformas APU) | Alta (Win32 Processor Topology Ex) | `CpuOrchestrator` modular actual |

---

## 5. Enlaces de Referencia Técnica
- **Documento Maestro de Arquitectura y Patrón Universal:** [`docs/ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md`](ARQUITECTURA_RENDIMIENTO_Y_PATRON_UNIVERSAL.md)
- **Diseño del Sistema de Guards:** [`docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md`](superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md)
- **Plan de Implementación Completado:** [`docs/superpowers/plans/2026-11-01-apu-dx11-dx12-universal-guards.md`](superpowers/plans/2026-11-01-apu-dx11-dx12-universal-guards.md)
- **Script de Compilación y Verificación:** [`scripts/build.ps1`](../scripts/build.ps1)
