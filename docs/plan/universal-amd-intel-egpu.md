# Plan universal: de APU especializado a motor para AMD, Intel, iGPU, dGPU y eGPU

Brechas del código actual para otro hardware, perfiles propuestos y tres tareas pendientes. Es la versión verificada contra el código; el archivo `PARTE_IV_plan_original_sin_verificar.md` (en el historial de git, commit `d59739f`, carpeta `docs/archivo/`) conserva el texto original de Antigravity (Gemini, 2026-11-07), sin verificar.

Estado al 2026-11-08, verificado contra la rama `integ/rust-hybrid-ab` (commit `cb0e67c`): las 5 brechas siguen abiertas y ninguna de las 3 tareas está implementada. Punto de partida: `cargo test` 15/15 y suite C++ 31/31 en el laptop de pruebas (Ryzen 5 4600H). Todo lo medido en este proyecto es de un solo equipo: nada de este plan está medido en Intel híbrido, Ryzen multi-CCD o X3D, GPU dedicada ni eGPU.

## 1. Las 5 brechas del código actual

Las líneas son aproximadas y corresponden al commit indicado.

| # | Brecha | Dónde | Qué hace hoy | Cambio propuesto |
| --- | --- | --- | --- | --- |
| 1 | Intel híbrido (12ª a 14ª gen y Core Ultra) | `rust/vegaflow-core/src/cpu_governor.rs`, rama híbrida (~l.101-128) | Todos los núcleos de menor eficiencia (8 a 16 E-Cores en un i7 o i9) van al fondo, y los P-Cores al juego | Con 4 o más E-Cores, confinar el fondo a los últimos 2 (si hay 4 o menos) o a los últimos 4 (si hay 8 o más); dejar P-Cores y el resto de E-Cores al juego |
| 2 | Preset de Unreal en GPU dedicada o externa | `src/app/main.cpp` (~l.698-710) y `src/optimize/steam_engine_optimizer.cpp` (~l.781-802) | `ApplyEngineUmaPreset` se aplica a cualquier juego Unreal sin mirar el tier; escribe `r.AOQuality=0`, `r.SSR.Quality=0` y `r.Shadow.DistanceScale=0.6` | Tres perfiles: UMA (el actual), dGPU interna sin pérdida visual y eGPU sin pérdida visual con amortiguación de copias |
| 3 | Interconexión gráfica | `src/detect/hardware_profiler.hpp` y `src/detect/hardware_profiler.cpp` (`DetectGpu`) | Solo mira `DedicatedVideoMemory > 2 GB`; no distingue iGPU más dGPU, ni un PCIe interno de una eGPU por Thunderbolt o USB4 | Añadir `GpuInterconnectType`: `UMA_Internal`, `PCIe_Internal_Discrete` y `External_eGPU_Tunnel` |
| 4 | AMD multi-CCD y 3D V-Cache | `cpu_governor.rs` (~l.192-251) | Con 2 islas L3 simétricas de 6 o más núcleos toma un solo núcleo para el fondo y deja ambos CCD al juego | Ver el riesgo de la sección 3 antes de implementar |
| 5 | Más de 64 hilos y drivers de varios fabricantes | `src/bridge/rust_bridge.h` (`VF_MAX_LOGICAL_PROCESSORS = 64`) y `src/optimize/graphics_optimizer.cpp` | Límite rígido de 64 procesadores lógicos; parte de los ajustes de gráficos depende de claves del driver AMD (`UMD`) | Subir el límite a 128 sin memoria dinámica; comprobar que las directivas de Windows funcionan sin `UMD` y reportar el fabricante real (AMD, Intel o NVIDIA) |

## 2. Perfiles propuestos

### CPU

| Topología | Fondo | Juego |
| --- | --- | --- |
| Monolítica de 4 a 8 núcleos (AMD o Intel) | Último núcleo físico | Todo lo demás |
| AMD con dos CCX de 6 u 8 núcleos (como el 4600H) | Último núcleo del segundo CCX | Primer CCX intacto |
| AMD multi-CCD de 12 o 16 núcleos, y X3D | Últimos núcleos del CCD 1 | Ver el riesgo de la sección 3 |
| Intel híbrido (P-Cores y E-Cores) | Solo los últimos 2 a 4 E-Cores, con EcoQoS | P-Cores y el resto de E-Cores |

### Política gráfica por interconexión

| Interconexión | Qué se aplica |
| --- | --- |
| iGPU integrada (UMA) | Guardián de RAM y VRAM compartida, preset de sombras actual y purga de Standby List |
| dGPU dedicada interna (PCIe) | Sin recortes visuales: solo `r.ShaderPipelineCache.Enabled=1`, `r.ShaderPipelineCache.AsyncCompileRate=32` y `r.GTSyncType=1`; sin tocar el pool de VRAM |
| eGPU (Thunderbolt, USB4, OCuLink) | Sin recortes visuales, con `r.Streaming.AmortizeCPUToGPUCopy=1`, `r.Streaming.MaxNumTexturesToStreamPerFrame=40` y los mismos de shaders; `PCIe ASPM = 0`, núcleo 0 libre para las interrupciones del controlador y `FlipQueueSize = 1` |

## 3. Riesgos a resolver antes de implementar

- **La brecha 4 choca con lo medido aquí.** Confinar al juego a un subconjunto de hilos costó entre 19% y 24% de FPS en el equipo de pruebas, y por eso es una opción apagada por defecto (`restrict_game_cpu_sets`); además el principio del proyecto es dejar al juego sin restricciones y confinar solo al fondo. Aislar al juego en el CCD 0 es una práctica conocida en X3D, pero aquí no está medida. Debe entrar como opción apagada, o como preferencia y no como confinamiento, hasta medirla en un equipo X3D.
- **Las brechas 1, 2, 3 y 5 son razonamiento de arquitectura, no mediciones.** Los argumentos sobre el Ring Bus, la latencia entre CCD (~75 ns) o el ancho de banda del cable son hipótesis. La regla del plan de medición es que una palanca sin efecto medido no va por defecto: o pasa la puerta de evidencia, o queda opcional.
- **Con un solo equipo solo se puede verificar lo determinista.** Las reglas de partición se prueban simulando topologías en `cargo test`, como se hizo con las de 4, 6, 8 y 12 hilos; el rendimiento real en otro hardware queda sin medir.

## 4. Tareas

### Tarea 1 (pendiente): núcleo Rust, topologías universales de CPU

Archivos: `src/bridge/rust_bridge.h`, `rust/vegaflow-core/src/cpu_governor.rs`, `rust/vegaflow-core/src/lib.rs`.

1. Subir `VF_MAX_LOGICAL_PROCESSORS` de 64 a 128.
2. Regla para Intel híbrido con 4 o más E-Cores: fondo en los últimos 2 (si hay 4 o menos) o 4 (si hay 8 o más); con 1 o 2 E-Cores, usar esos para el fondo.
3. Regla para AMD con 2 o más islas L3 de 6 o más núcleos físicos: sujeta al riesgo de la sección 3.
4. Pruebas con `cargo test`: i7 o i9 híbrido de 8 P-Cores y 8 E-Cores (24 hilos), Ryzen 9 de 12 y 16 núcleos con dos CCD, y todas las topologías actuales intactas (4600H, 5500U, Zen 4 más Zen 4c, 2C/4T, 4C/8T, 8C/16T).

### Tarea 2 (pendiente): detección y políticas de GPU en C++20

Archivos: `src/detect/hardware_profiler.hpp`, `src/detect/hardware_profiler.cpp`, `src/optimize/steam_engine_optimizer.cpp`, `src/app/main.cpp`.

1. `GpuInterconnectType` y detección de varias GPU (iGPU más dGPU o eGPU) con DXGI, el registro PCI y las propiedades del dispositivo.
2. Un tier o indicador para eGPU (`is_external_egpu`, `preserve_ultra_quality`).
3. Separar `GenerateUnrealUmaBlock` en los tres perfiles de la sección 2, sin cambiar el bloque UMA actual.

### Tarea 3 (pendiente): pruebas multi-hardware y documento maestro

Archivos: `tests/test_main.cpp` y [`../arquitectura/patron-universal.md`](../arquitectura/patron-universal.md).

1. Prueba 32 que verifique de forma determinista 6 perfiles: APU monolítica (5500U), APU dual-CCX (4600H) y Zen 4 más Zen 4c, AMD dual-CCD o X3D, Intel híbrido (8P+8E y 8P+16E), PC con dGPU (preset sin recortes) y portátil con eGPU (`AmortizeCPUToGPUCopy=1`, `ASPM=0`).
2. Documentar lo implementado en `docs/arquitectura/`.

## 5. Cómo se verifica

- `cargo test --manifest-path rust/vegaflow-core/Cargo.toml` (en el Mac no corre: el crate enlaza `ntdll`).
- `scripts/build.ps1 -RunTests`, solo en Windows; criterio: 0 pruebas fallidas.
- `scripts/tweaks_state.ps1` después de la corrida: ningún valor fuera de fábrica.


---

## Pilares pendientes: GUI, perfiles dinámicos y telemetría

### 3. Próximos Pilares de Enfoque (GUI, Perfiles Dinámicos y Telemetría)

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

#### Pilar 1: Integración Visual del `ApuCapabilityGuard` y Telemetría DX11/DX12 en la GUI
**Objetivo:** Elevar la instrumentación visual en la GUI nativa Win32/GDI ([`src/ui/gui_window.cpp`](../../src/ui/gui_window.cpp), [`src/ui/gui_tabs.cpp`](../../src/ui/gui_tabs.cpp)) para que el usuario visualice la toma de decisiones del motor en tiempo real.

- **Componentes a Implementar:**
  1. **Badge de Tier y Guard Activo en el Dashboard:** Mostrar en la tarjeta de Gráficos/UMA el Tier detectado (`Tier 1: Entry UMA`, `Tier 2: Mid Vega`, `Tier 3: Advanced UMA`, `Tier 4: Discrete`, `Tier 5: External eGPU`).
  2. **Barra Dinámica de Presupuesto DXGI 1.4:** Medidor gráfico del consumo local de VRAM UMA en porcentaje, iluminándose en ámbar/rojo con alerta si rebasa el umbral de presión del **85%**.
  3. **Indicador de Intervenciones Reactivas:** Contador acumulativo de intervenciones del `EvaluateUmaPressureGuard`.
  4. **Panel de Especialización DX11 vs DX12 en Pestaña Gráfica:** Detalle de hilos DXVK activos, estado de teselación AMD (8x/16x/32x) y protección de `AsyncCompute`.

#### Pilar 2: Perfiles Dinámicos por Juego y Detección Automática de Cuello de Botella
**Objetivo:** Desarrollar un subsistema adaptativo dentro de [`FrameTelemetryMonitor`](../../src/safety/frame_telemetry.hpp) y [`ProcessDetector`](../../src/detect/process_detector.hpp) que identifique si el juego está limitado por CPU (*CPU-Bound*) o por GPU (*GPU-Bound*).

#### Pilar 3: Topología Híbrida para APUs de Nueva Generación (`CpuOrchestrator`)
**Objetivo:** Adaptar el subsistema de afinidad y asignación de CPU Sets ([`src/optimize/cpu_orchestrator.cpp`](../../src/optimize/cpu_orchestrator.cpp)) para arquitecturas de núcleos heterogéneos (Zen 5/Zen 5c, Intel Core Ultra) conectándose con las nuevas reglas universales de `vegaflow-core`.

#### Pilar 4: Telemetría In-Game Zero-Hook sobre DirectFlip
**Objetivo:** Proveer una superposición en pantalla (*HUD / Overlay*) ultraligera que informe FPS, 1% Lows, 0.1% Lows, consumo de presupuesto UMA y estado del Guard, **sin requerir inyección de DLLs ni hooking de APIs gráficas** (100% Anti-Cheat Seguro).

---

### 4. Matriz de Prioridad y Secuencia de Ejecución

| Hito | Fase / Pilar | Impacto en Usuario | Complejidad Técnica | Dependencias Previas |
| :--- | :--- | :--- | :--- | :--- |
| **Inmediato** | **Soporte Universal (Tareas 1, 2 y 3)** | Crítico (Compatibilidad Intel Híbrido, Multi-CCD y dGPU/eGPU) | Alta (Rust FFI + C++20 DXGI/Bus) | Base consolidada (`ApuCapabilityGuard` y `vegaflow-core`) |
| **Fase 1** | **Pilar 1: GUI ApuGuard** | Alto (Visibilidad de estado y alertas) | Media (Win32/GDI) | Soporte Universal consolidado |
| **Fase 2** | **Pilar 2: Auto-Tuning CPU/GPU** | Muy Alto (Fluidez adaptativa) | Alta (Heurísticas de telemetría) | `FrameTelemetryMonitor` + `MemoryManager` |
| **Fase 3** | **Pilar 4: Overlay Zero-Hook** | Muy Alto (Experiencia in-game) | Media-Alta (Win32 Layered Window) | DirectFlip monitor + `FrameTelemetryMonitor` |
| **Fase 4** | **Pilar 3: Topología Híbrida Avanzada** | Alto (Nuevas plataformas APU) | Alta (Win32 Processor Topology Ex) | `CpuOrchestrator` modular actual |

---

### 5. Enlaces de Referencia Técnica
- **Visión e invariantes:** [`vision-y-principios.md`](../arquitectura/vision-y-principios.md) y [`patron-universal.md`](../arquitectura/patron-universal.md)
- **Diseño del Sistema de Guards:** [`hardware-y-tiers.md`](../arquitectura/hardware-y-tiers.md)
- **Plan de implementación completado:** `docs/archivo/2026-11-01-apu-dx11-dx12-universal-guards.md` (en el historial de git, commit `d59739f`)
- **Script de Compilación y Verificación:** [`scripts/build.ps1`](../../scripts/build.ps1)
