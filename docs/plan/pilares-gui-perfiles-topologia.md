# Pilares pendientes: GUI, perfiles dinámicos y telemetría

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
**Objetivo:** Elevar la instrumentación visual en la GUI nativa Win32/GDI ([`src/gui/gui_window.cpp`](../../src/gui/gui_window.cpp), [`src/gui/gui_tabs.cpp`](../../src/gui/gui_tabs.cpp)) para que el usuario visualice la toma de decisiones del motor en tiempo real.

- **Componentes a Implementar:**
  1. **Badge de Tier y Guard Activo en el Dashboard:** Mostrar en la tarjeta de Gráficos/UMA el Tier detectado (`Tier 1: Entry UMA`, `Tier 2: Mid Vega`, `Tier 3: Advanced UMA`, `Tier 4: Discrete`, `Tier 5: External eGPU`).
  2. **Barra Dinámica de Presupuesto DXGI 1.4:** Medidor gráfico del consumo local de VRAM UMA en porcentaje, iluminándose en ámbar/rojo con alerta si rebasa el umbral de presión del **85%**.
  3. **Indicador de Intervenciones Reactivas:** Contador acumulativo de intervenciones del `EvaluateUmaPressureGuard`.
  4. **Panel de Especialización DX11 vs DX12 en Pestaña Gráfica:** Detalle de hilos DXVK activos, estado de teselación AMD (8x/16x/32x) y protección de `AsyncCompute`.

### Pilar 2: Perfiles Dinámicos por Juego y Detección Automática de Cuello de Botella
**Objetivo:** Desarrollar un subsistema adaptativo dentro de [`FrameTelemetryMonitor`](../../include/security/frame_telemetry.hpp) y [`ProcessDetector`](../../include/core/process_detector.hpp) que identifique si el juego está limitado por CPU (*CPU-Bound*) o por GPU (*GPU-Bound*).

### Pilar 3: Topología Híbrida para APUs de Nueva Generación (`CpuOrchestrator`)
**Objetivo:** Adaptar el subsistema de afinidad y asignación de CPU Sets ([`src/optimizers/cpu_orchestrator.cpp`](../../src/optimizers/cpu_orchestrator.cpp)) para arquitecturas de núcleos heterogéneos (Zen 5/Zen 5c, Intel Core Ultra) conectándose con las nuevas reglas universales de `vegaflow-core`.

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
- **Visión e invariantes:** [`vision-e-invariantes.md`](../arquitectura/vision-e-invariantes.md) y [`patron-universal.md`](../arquitectura/patron-universal.md)
- **Diseño del Sistema de Guards:** [`spec-guards-apu-dx11-dx12.md`](../arquitectura/spec-guards-apu-dx11-dx12.md)
- **Plan de Implementación Completado:** [`2026-11-01-apu-dx11-dx12-universal-guards.md`](../archivo/2026-11-01-apu-dx11-dx12-universal-guards.md)
- **Script de Compilación y Verificación:** [`scripts/build.ps1`](../../scripts/build.ps1)
