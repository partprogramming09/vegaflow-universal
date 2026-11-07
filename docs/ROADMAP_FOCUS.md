# Hoja de Ruta y Enfoque de Ingeniería — VegaFlow Universal

**Fecha de Actualización:** 2026-11-07  
**Estado General:** Base Universal Consolidada y Verificada (31/31 Tests C++20 + 15/15 Tests Rust Pasados)  
**Documento Maestro:** Índice Estratégico y Hoja de Ruta Técnica

---

## 1. Base Completada: Fase Actual Consolidada

VegaFlow Universal ha alcanzado la madurez de su núcleo de optimización y arquitectura de software, validado en hardware real y suites sintéticas:

### 1.1. Arquitectura Modular Limpia y Pipeline de Compilación
- **Desacoplamiento Estructural:** El código base se organiza en cuatro dominios modulares estrictos:
  - [`include/core/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/core) y [`src/core/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/core): Hardware profiler, llamadas indocumentadas NTDLL, privilegios de seguridad y telemetría de sistema.
  - [`include/optimizers/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/optimizers) y [`src/optimizers/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/optimizers): Orquestación de CPU, optimizadores gráficos y de temporizador NT, DXVK, escalador FSR 1.0/NIS, gestión de memoria UMA y presets de motores.
  - [`include/security/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/security) y [`src/security/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/security): Subsistema Zero-Trust `SandboxGuard` y telemetría matemática de frametimes.
  - [`include/gui/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/gui) y [`src/gui/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/gui): Ventana nativa Win32/GDI en modo Slate/Zinc Dark, pestañas y componentes.
- **Automatización de Build:** Script [`scripts/build.ps1`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/scripts/build.ps1) con detección inteligente del compilador MinGW-w64 `g++` (en PATH o en Scoop), flags C++20 con enlace estático total (`-static -static-libgcc -static-libstdc++`) y ejecución opcional de pruebas con `-RunTests`.
- **Control de Versiones y Limpieza:** Directorio `build/` libre de residuos (`*.o`, `*.old*`, logs) y `.gitignore` estandarizado.

### 1.2. Motor Universal de Clasificación y Guards (`ApuCapabilityGuard`)
Especificado en [`docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md) e implementado en [`src/core/hardware_profiler.cpp`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/core/hardware_profiler.cpp):
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
Implementado en el crate estático [`rust/vegaflow-core/`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/rust/vegaflow-core) y enlazado sin sobrecarga vía C ABI ([`include/core/rust_bridge.h`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/core/rust_bridge.h)):
- **Evaluador de Presión UMA (`src/uma_memory.rs`):** Funciones `vf_rust_evaluate_uma_pressure` y `vf_rust_evaluate_nt_memory_lists`. Algoritmos deterministas sin asignación dinámica de memoria en el heap (Zero-Allocation), verificando saturación local DXGI, reservas del sistema operativo y listas Standby de NT.
- **Topología L3 y Gobernanza de CPU (`src/cpu_governor.rs`):** Función `vf_rust_partition_topology_l3` para aislar hilos del juego en la caché L3 principal para arquitecturas simétricas o heterogéneas (Zen 2/3/4/5 y procesadores híbridos), junto con la máquina de estados e histéresis `vf_rust_evaluate_background_confinement` (umbrales de 0.50 y 0.25 núcleos lógicos).
- **Scanner Anti-Cheat y Blindaje de Archivos (`src/sandbox_core.rs`):** Detección formal y sin falsos positivos de firmas de Anti-Cheat de kernel (`vf_rust_contains_anticheat_signature`) y protección estricta contra modificaciones accidentales a binarios de arranque del sistema (`vf_rust_is_critical_system_file`).
- **Seguridad Incondicional:** Compilación estática con LTO completo, `panic = "abort"`, y verificación formal mediante 15 pruebas unitarias internas.

---

## 2. Próximos Pilares de Enfoque (Qué haremos a continuación)

Con el núcleo algorítmico y los guards de silicio plenamente consolidados, los siguientes esfuerzos de ingeniería se concentrarán en cuatro pilares complementarios:

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

---

### Pilar 1: Integración Visual del `ApuCapabilityGuard` y Telemetría DX11/DX12 en la GUI
**Objetivo:** Elevar la instrumentación visual en la GUI nativa Win32/GDI ([`src/gui/gui_window.cpp`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/gui/gui_window.cpp), [`src/gui/gui_tabs.cpp`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/gui/gui_tabs.cpp)) para que el usuario visualice la toma de decisiones del motor en tiempo real.

- **Componentes a Implementar:**
  1. **Badge de Tier y Guard Activo en el Dashboard:** Mostrar en la tarjeta de Gráficos/UMA el Tier detectado (`Tier 1: Entry UMA`, `Tier 2: Mid Vega`, `Tier 3: Advanced UMA`, `Tier 4: Discrete`) junto con el badge cromático de política (`Survival Guard`, `Sweet-Spot Guard`, `Do-No-Harm Guard`).
  2. **Barra Dinámica de Presupuesto DXGI 1.4:** Medidor gráfico del consumo local de VRAM UMA en porcentaje, iluminándose en ámbar/rojo con alerta si rebasa el umbral de presión del **85%**.
  3. **Indicador de Intervenciones Reactivas:** Contador acumulativo de intervenciones del `EvaluateUmaPressureGuard` (limpiezas de working set y mitigaciones no invasivas).
  4. **Panel de Especialización DX11 vs DX12 en Pestaña Gráfica:** Detalle de hilos DXVK activos, estado de teselación AMD (8x/16x/32x) y protección de `AsyncCompute`.

---

### Pilar 2: Perfiles Dinámicos por Juego y Detección Automática de Cuello de Botella
**Objetivo:** Desarrollar un subsistema adaptativo dentro de [`FrameTelemetryMonitor`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/security/frame_telemetry.hpp) y [`ProcessDetector`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/include/core/process_detector.hpp) que identifique si el juego está limitado por CPU (*CPU-Bound*) o por GPU (*GPU-Bound*).

- **Mecanismos Técnicos:**
  1. **Análisis de Dispersión y Jitter de Frametimes:** Correlacionar la desviación estándar de intervalos de fotogramas con la utilización de CPU Sets para diagnosticar contención de hilos principales de renderizado.
  2. **Auto-Ajuste Dinámico del Escalado FSR 1.0 / NIS:**
     - Si el juego es *GPU-Bound* severo: Activar o elevar automáticamente el escalado espacial para reducir la carga de sombreadores en la APU.
     - Si el juego es *CPU-Bound*: Priorizar la frecuencia STAPM de la CPU y elevar la agresividad de `EcoQoS` en procesos secundarios.
  3. **Base de Datos Local de Perfiles (`config/game_profiles.json`):** Persistencia de ajustes optimizados por ejecutable (resolución de sombras, teselación, modo FSR) sin requerir reconfiguración manual por el usuario.

---

### Pilar 3: Topología Híbrida para APUs de Nueva Generación (`CpuOrchestrator`)
**Objetivo:** Adaptar el subsistema de afinidad y asignación de CPU Sets ([`src/optimizers/cpu_orchestrator.cpp`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/src/optimizers/cpu_orchestrator.cpp)) para arquitecturas de núcleos heterogéneos y múltiples islas de silicio.

- **Arquitecturas Objetivo:**
  - **AMD Strix Point / Kraken Point / Fire Range:** Núcleos densos Zen 5 combinados con núcleos compactos Zen 5c (con diferentes latencias de interconexión y partición de caché L3).
  - **Intel Core Ultra (Meteor Lake / Arrow Lake / Lunar Lake):** P-Cores de alto rendimiento, E-Cores de eficiencia y LP E-Cores en el SoC tile.
- **Estrategia de Asignación:**
  1. Identificar mediante `GetLogicalProcessorInformationEx(RelationProcessorCore)` los núcleos con mayor frecuencia máxima y acceso directo a la caché L3 principal.
  2. Confinar los hilos críticos de renderizado D3D/DXVK y simulación del juego exclusivamente a los núcleos de alto rendimiento con L3 completa, aislando tareas de fondo y drivers secundarios en los núcleos compactos/eficientes.

---

### Pilar 4: Telemetría In-Game Zero-Hook sobre DirectFlip
**Objetivo:** Proveer una superposición en pantalla (*HUD / Overlay*) ultraligera que informe FPS, 1% Lows, 0.1% Lows, consumo de presupuesto UMA y estado del Guard, **sin requerir inyección de DLLs ni hooking de APIs gráficas**.

- **Principio de Diseño:**
  - Ventana Win32 perimetral transparente de tipo *click-through* (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST`).
  - Renderizado por GDI doble búfer o DirectWrite desacoplado a 30 o 60 Hz, con impacto de tiempo de cuadro inferior a **0.2 ms**.
  - **100% Inmune a Sanciones Anti-Cheat:** Al operar fuera del espacio de direcciones del proceso del videojuego, garantiza seguridad total en títulos protegidos como Zenless Zone Zero, Genshin Impact o títulos con EAC/BattlEye.

---

## 3. Matriz de Prioridad y Secuencia de Ejecución

| Hito | Pilar | Impacto en Usuario | Complejidad Técnica | Dependencias Previas |
| :--- | :--- | :--- | :--- | :--- |
| **Fase 1** | **Pilar 1: GUI ApuGuard** | Alto (Visibilidad de estado y alertas) | Media (Win32/GDI) | Base consolidada (`ApuCapabilityGuard` actual) |
| **Fase 2** | **Pilar 2: Auto-Tuning CPU/GPU** | Muy Alto (Fluidez adaptativa) | Alta (Heurísticas de telemetría) | `FrameTelemetryMonitor` + `MemoryManager` |
| **Fase 3** | **Pilar 4: Overlay Zero-Hook** | Muy Alto (Experiencia in-game) | Media-Alta (Win32 Layered Window) | DirectFlip monitor + `FrameTelemetryMonitor` |
| **Fase 4** | **Pilar 3: Topología Híbrida** | Alto (Nuevas plataformas APU) | Alta (Win32 Processor Topology Ex) | `CpuOrchestrator` modular actual |

---

## 4. Enlaces de Referencia Técnica
- **Diseño del Sistema de Guards:** [`docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md)
- **Plan de Implementación Completado:** [`docs/superpowers/plans/2026-11-01-apu-dx11-dx12-universal-guards.md`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/docs/superpowers/plans/2026-11-01-apu-dx11-dx12-universal-guards.md)
- **Script de Compilación y Verificación:** [`scripts/build.ps1`](file:///C:/Users/testuser/OneDrive/Escritorio/proyectos/direct/scripts/build.ps1)
