# Base consolidada

## 1. Base Completada: Fase Actual Consolidada

VegaFlow Universal ha alcanzado la madurez de su núcleo de optimización y arquitectura de software, validado en hardware real y suites sintéticas:

### 1.1. Arquitectura Modular Limpia y Pipeline de Compilación
- **Desacoplamiento Estructural:** El código base se organiza en cuatro dominios modulares estrictos:
  - [`include/core/`](../../include/core) y [`src/core/`](../../src/core): Hardware profiler, llamadas indocumentadas NTDLL, privilegios de seguridad y telemetría de sistema.
  - [`include/optimizers/`](../../include/optimizers) y [`src/optimizers/`](../../src/optimizers): Orquestación de CPU, optimizadores gráficos y de temporizador NT, DXVK, escalador FSR 1.0/NIS, gestión de memoria UMA y presets de motores.
  - [`include/security/`](../../include/security) y [`src/security/`](../../src/security): Subsistema Zero-Trust `SandboxGuard` y telemetría matemática de frametimes.
  - [`include/gui/`](../../include/gui) y [`src/gui/`](../../src/gui): Ventana nativa Win32/GDI en modo Slate/Zinc Dark, pestañas y componentes.
- **Automatización de Build:** Script [`scripts/build.ps1`](../../scripts/build.ps1) con detección inteligente del compilador MinGW-w64 `g++` (en PATH o en Scoop), flags C++20 con enlace estático total (`-static -static-libgcc -static-libstdc++`) y ejecución opcional de pruebas con `-RunTests`.
- **Control de Versiones y Limpieza:** Directorio `build/` libre de residuos (`*.o`, `*.old*`, logs) y `.gitignore` estandarizado.

### 1.2. Motor Universal de Clasificación y Guards (`ApuCapabilityGuard`)
Especificado en [`spec-guards-apu-dx11-dx12.md`](spec-guards-apu-dx11-dx12.md) e implementado en [`src/core/hardware_profiler.cpp`](../../src/core/hardware_profiler.cpp):
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
Implementado en el crate estático [`rust/vegaflow-core/`](../../rust/vegaflow-core) y enlazado sin sobrecarga vía C ABI ([`include/core/rust_bridge.h`](../../include/core/rust_bridge.h)):
- **Evaluador de Presión UMA (`src/uma_memory.rs`):** Funciones `vf_rust_evaluate_uma_pressure` y `vf_rust_evaluate_nt_memory_lists`. Algoritmos deterministas sin asignación dinámica de memoria en el heap (Zero-Allocation), verificando saturación local DXGI, reservas del sistema operativo y listas Standby de NT.
- **Topología L3 y Gobernanza de CPU (`src/cpu_governor.rs`):** Función `vf_rust_partition_topology_l3` para aislar hilos del juego en la caché L3 principal para arquitecturas simétricas o heterogéneas (Zen 2/3/4/5 y procesadores híbridos), junto con la máquina de estados e histéresis `vf_rust_evaluate_background_confinement` (umbrales de 0.50 y 0.25 núcleos lógicos).
- **Scanner Anti-Cheat y Blindaje de Archivos (`src/sandbox_core.rs`):** Detección formal y sin falsos positivos de firmas de Anti-Cheat de kernel (`vf_rust_contains_anticheat_signature`) y protección estricta contra modificaciones accidentales a binarios de arranque del sistema (`vf_rust_is_critical_system_file`).
- **Seguridad Incondicional:** Compilación estática con LTO completo, `panic = "abort"`, y verificación formal mediante 15 pruebas unitarias internas.

---

