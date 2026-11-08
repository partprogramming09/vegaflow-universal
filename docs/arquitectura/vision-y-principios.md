# Arquitectura de Alto Rendimiento, Validación Empírica y Patrón Universal de Aislamiento de CPU/GPU

> **VegaFlow Universal — Runtime Híbrido C++20 + Rust (`staticlib` FFI)**  
> **Caso de Estudio en Motor Comercial (*Octopath Traveler II* — Unreal Engine 4 HD-2D)**  
> **Análisis Universal de Arquitectura: AMD Ryzen (Monolítico / Multi-CCD / X3D) vs. Intel Core (Homogéneo / Híbrido P+E) en configuraciones iGPU, dGPU y eGPU**

---

## Resumen Ejecutivo e Invariantes del Sistema

**VegaFlow Universal** es una solución de ingeniería a nivel de sistema diseñada para resolver la degradación de rendimiento (*frame stuttering*, caída de percentiles `1% Low` y *cache eviction*) en videojuegos ejecutados bajo el sistema operativo Windows. El proyecto implementa una arquitectura desacoplada donde el orquestador de sistema Win32/DXGI está desarrollado en **C++20** y el motor de evaluación de políticas críticas opera en **Rust** compilado como biblioteca estática (`staticlib`, `panic = "abort"`, `lto = true`, `opt-level = 3`), comunicados mediante una interfaz binaria de aplicación (ABI C) de cero sobrecarga (*zero-allocation*).

### Invariantes del Runtime Demostrados en Pruebas Automatizadas
* **Consumo de Memoria Privada (*Private Working Set*):** `<= 2.00 MB` en régimen estacionario (el presupuesto de diseño permitía hasta `15.00 MB`).
* **Sobrecarga de CPU en Reposo (*Zero-Overhead Idle*):** `0.00%` sostenido; el daemon hiberna mediante `WaitForSingleObjectEx` y timers de alta precisión NT.
* **Conjunto de Pruebas Automatizadas:** `21/21` tests unitarios deterministas en Rust (`vegaflow-core`, 22 en un Mac) y `33/33` tests de regresión y cobertura en C++20 pasando al 100%.
* **Reversibilidad Garantizada (`RestoreAll`):** Cada modificación de afinidad, clase de prioridad, estado `EcoQoS` o claves en el Registro de Windows (`HKCU\Software\VegaFlow\RegBackup`) se restaura estrictamente al estado de fábrica al cerrar el daemon o terminar la sesión de juego.
* **Seguridad Anti-Cheat en Tiempo Constante (*Zero-Tamper Boundary*):** Detección determinista de firmas de controladores anti-cheat (*HoYoKProtect*, *EasyAntiCheat*, *BattlEye*, *Vanguard*) con aislamiento de ruta en UTF-16 sobre pila (`stack-only`), impidiendo cualquier inyección o modificación sobre ejecutables protegidos.

---

# PARTE I: Arquitectura Híbrida C++20 + Rust (`staticlib` FFI) y Daemon Zero-Overhead

## 1.1 El Problema Físico de Raíz: Contención de Silicio en Cargas Mixtas

En sistemas contemporáneos donde conviven aplicaciones en primer plano (juegos 3D, simuladores) y múltiples procesos en segundo plano (navegadores basados en Chromium con decenas de hilos, plataformas de chat como Discord, clientes de distribución digital e indexadores de Windows), el planificador estándar del sistema operativo (*Windows NT Scheduler*) optimiza el reparto global de cuotas de tiempo (*quantums*) mediante balanceo simétrico de carga.

Este comportamiento provoca un fenómeno físico destructivo sobre la memoria caché y el despachador de gráficos:

```mermaid
flowchart TD
    subgraph Silicio_Sin_VegaFlow["❌ ARQUITECTURA SIN AISLAMIENTO: Contención en Caché L3, Bus de Memoria y Presupuesto Térmico"]
        direction TB
        subgraph CPU_Cores["Complejo de Núcleos CPU (Caché L3 Unificada o Particionada)"]
            Core0["Núcleos Principales 0-2<br/>🎮 Hilos del Juego (GameThread / RenderThread)<br/>⚡ Envenenados por hilos de Chrome/Discord"]
            Core1["Núcleos Secundarios 3-5<br/>🌐 Workers de fondo, Indexador, Telemetría<br/>⚡ Migración constante entre núcleos"]
        end
        GPU["🎨 Procesador Gráfico (iGPU UMA / dGPU PCIe / eGPU TB4)<br/>Hambriento esperando listas de comandos D3D11/D3D12"]
        BUS["🚌 Bus de Interconexión (Infinity Fabric / Ring Bus / Enlace PCIe)"]
        MEM["💾 Subsistema de Memoria RAM / VRAM<br/>Líneas de caché desalojadas forzando accesos a memoria lenta"]
    end

    Core0 <-->|"Desalojos constantes de L3 (Cache Thrashing)"| BUS
    Core1 <-->|"Interrupciones de hilo y robo de cuotas (Quantum Preemption)"| BUS
    GPU <-->|"Latencia de despacho de draw calls elevada"| BUS
    BUS <--> MEM

    style Silicio_Sin_VegaFlow fill:#2b1818,stroke:#ef4444,stroke-width:2px,color:#f3f4f6
    style CPU_Cores fill:#3b1f1f,stroke:#f87171,color:#f3f4f6
    style Core0 fill:#451a1a,stroke:#f87171,color:#f3f4f6
    style Core1 fill:#451a1a,stroke:#f87171,color:#f3f4f6
    style GPU fill:#3f2e18,stroke:#f59e0b,color:#f3f4f6
    style BUS fill:#1f2937,stroke:#9ca3af,color:#f3f4f6
    style MEM fill:#1f2937,stroke:#9ca3af,color:#f3f4f6
```

### Mecanismos de Degradación del Rendimiento
1. **Contaminación y Desalojo de Caché L3 (*Cache Thrashing*):**  
   Cuando un proceso de fondo se planifica en un núcleo donde operaba el `GameThread`, desaloja de las líneas de caché L2 y L3 las estructuras críticas del motor de juego (árboles de nodos de escena, datos de animación esquelética y descriptores de vértices). Mientras que una lectura de caché L3 toma típicamente entre **`10 y 14 ns`**, acudir a memoria física RAM DDR4/DDR5 penaliza la transacción con una latencia de **`70 a 95 ns`** (un incremento de orden de magnitud **~7x–8x**).
2. **Preempción de Cuotas de CPU (*Quantum Preemption*):**  
   Los controladores de gráficos de bajo nivel requieren que el hilo de renderizado despache secuencias ininterrumpidas de comandos hacia el hardware. Cuando el planificador interrumpe el contexto para atender una ráfaga JavaScript de un navegador, el pipeline de la GPU se vacía temporalmente (*GPU Starvation*), manifestándose como caídas abruptas en los percentiles `1% Low` y `0.1% Low`.
3. **Presupuesto Energético y Térmico Compartido (*Power/Thermal Budget Throttling*):**  
   Tanto en procesadores móviles como en procesadores de escritorio, la unidad de administración de energía (SMU de AMD o PCU de Intel) reparte un presupuesto fijo de vatios (ej. `15W–45W` en APU, `65W–125W` en desktop). Cada vatio consumido por hilos de fondo a alta frecuencia reduce el margen térmico disponible para sostener las frecuencias *Turbo Boost* en los núcleos que ejecutan el juego.

---

## 1.2 Las 4 Barreras del Kernel de Windows NT Implementadas en VegaFlow

VegaFlow erradica la contención de silicio erigiendo **cuatro barreras simultáneas** implementadas mediante llamadas directas a las interfaces de programación nativas de Windows NT:

```mermaid
flowchart TD
    subgraph Barreras_VegaFlow["✅ ARQUITECTURA AISLADA: Las 4 Barreras de Contención del Kernel de Windows NT"]
        direction TB
        subgraph Zona_Juego["Espacio de Cómputo Prioritario (Núcleos 0 a 4)"]
            J_Cores["🎮 Hilos Críticos del Motor de Juego<br/>• GameThread / RenderThread / Audio / RHI<br/>• Libre de desalojos de Caché L3<br/>• Frecuencia Turbo sostenida a máxima cota (+138 a +290 MHz)"]
        end
        subgraph Jaula_Fondo["Jaula de Sacrificio Determinista (Último Núcleo Físico - Núcleo 5 / Afinidad 0xC00)"]
            BG_Cores["🔒 Procesos de Fondo Ruidosos (Chrome, Discord, Launchers)<br/>1️⃣ SetProcessDefaultCpuSets / Afinidad 0xC00<br/>2️⃣ EcoQoS: SetProcessInformation (ProcessPowerThrottling)<br/>3️⃣ I/O NT: NtSetInformationProcess (IoPriorityVeryLow = 0)<br/>4️⃣ Memory Priority: MEMORY_PRIORITY_LOW (2)"]
        end
        subgraph Barrera_Driver["Barrera D3D/Driver en Registro NT"]
            REG["4️⃣ FlipQueueSize = 1 + PCIe ASPM = 0<br/>• Cero cuadros encolados en cola de presentación<br/>• Cero transiciones L0s/L1 de micro-reposo en bus PCIe"]
        end
    end

    Zona_Juego ==>|"Despacho ininterrumpido de Draw Calls"| Barrera_Driver
    Jaula_Fondo -.->|"Ahorro térmico de 3W a 5W devuelto al juego"| Zona_Juego

    style Barreras_VegaFlow fill:#11221c,stroke:#10b981,stroke-width:2px,color:#f3f4f6
    style Zona_Juego fill:#133329,stroke:#34d399,color:#f3f4f6
    style J_Cores fill:#172a45,stroke:#3b82f6,color:#f3f4f6
    style Jaula_Fondo fill:#2d2312,stroke:#f59e0b,color:#f3f4f6
    style BG_Cores fill:#3f2e18,stroke:#f59e0b,color:#f3f4f6
    style Barrera_Driver fill:#1f2430,stroke:#8b5cf6,color:#f3f4f6
    style REG fill:#241d33,stroke:#a855f7,color:#f3f4f6
```

### Detalle de las 4 Barreras:
1. **Barrera 1 — Confinamiento Espacial de CPU (`SetProcessDefaultCpuSets` / `SetProcessAffinityMask`):**  
   El orquestador en [`cpu_orchestrator.cpp`](../../src/optimize/cpu_orchestrator.cpp) y el evaluador en [`cpu_governor.rs`](../../rust/vegaflow-core/src/cpu_governor.rs) analizan la topología física. En lugar de encerrar al juego (lo cual le restaba rendimiento multihilo), **el juego se mantiene libre en todos los núcleos disponibles**, mientras que los procesos clasificados como ruidosos que superan el umbral de actividad (`>= 0.5` procesadores lógicos) se confinan estrictamente al último núcleo físico (hilos lógicos 10 y 11, máscara `0xC00` en procesadores de 6 núcleos / 12 hilos). La liberación se rige por **histéresis** determinista (liberación solo si desciende de `0.25`), previniendo vibración en el planificador.
2. **Barrera 2 — Eficiencia Energética Forzada (`EcoQoS` Power Throttling):**  
   Mediante la API `SetProcessInformation(ProcessPowerThrottling)` con la bandera `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`, el kernel de Windows fuerza a la CPU a ejecutar dichos procesos en sus estados de energía más eficientes (*Energy Performance Preference* - EPP alto), reduciendo el calor generado y transfiriendo presupuesto térmico al juego.
3. **Barrera 3 — Prioridad de Entrada/Salida y Paginación NT (`NtSetInformationProcess`):**  
   A través de la llamada nativa en `ntdll.dll`, VegaFlow degrada la prioridad de E/S de los procesos de fondo a `IoPriorityVeryLow` (nivel 0) y su prioridad de asignación de páginas a `MEMORY_PRIORITY_LOW` (nivel 2). Cualquier transacción de disco o fallo de página generado por el fondo cede el paso incondicionalmente a las operaciones del motor de juego.
4. **Barrera 4 — Reducción de Latencia de Cola y Desactivación de Ahorro PCIe (`FlipQueueSize` y `ASPM`):**  
   En la clave de registro del adaptador de pantalla, VegaFlow fija `FlipQueueSize = 1`, impidiendo que el controlador de video acumule múltiples cuadros pendientes de renderizado (eliminando la latencia de entrada), y establece `ASPM = 0` para impedir que el enlace PCIe entre en estados de suspensión transitoria (*L0s/L1*).

---


---

## VegaFlow Universal — Developer & Architecture Guide

> **Guía Maestra de Arquitectura, Ingeniería de Sistemas y Estándares de Desarrollo**  
> *VegaFlow Universal — High-Performance APU & Gaming Optimization Framework*

---

### 1. Filosofía del Proyecto y Principios de Ingeniería

VegaFlow Universal nace de un principio fundamental de ingeniería de software y hardware: **no se trata de copiar código de terceros ni aplicar "tweaks" milagrosos a ciegas, sino de comprender con rigor técnico de raíz cómo funcionan el sistema operativo Windows NT, el subsistema gráfico DXGI/DWM y la arquitectura de hardware del procesador para optimizar el consumo de recursos y maximizar el rendimiento real de los videojuegos.**

```
+-----------------------------------------------------------------------------------+
|                        FILOSOFÍA DE DESARROLLO VEGAFLOW                           |
+-----------------------------------------------------------------------------------+
| 1. Rigor Técnico y Matemático: Cada cambio se fundamenta en especificaciones      |
|    oficiales de Microsoft, AMD y el estándar Vulkan/DirectX.                      |
| 2. Cero Overhead en Reposo: Menos de 15 MB de RAM privada (Private Working Set)   |
|    y 0.0% de uso de CPU mientras no haya un juego en ejecución activa.            |
| 3. Reversibilidad Total: Ninguna alteración es destructiva. Copias de seguridad    |
|    inmediatas (.vegaflow.bak, *.original.bak) y comandos de reversión completos.  |
| 4. Integridad y Seguridad Anti-Cheat: Respeto absoluto a los sistemas de juego    |
|    limpio. Cero inyecciones o alteraciones en títulos con protección kernel.     |
+-----------------------------------------------------------------------------------+
```

#### Mandamientos del Desarrollador en VegaFlow:
- **Medición empírica antes de aseverar:** Ninguna optimización se considera válida sin evidencia medible (percentiles 1% Lows, 0.1% Lows, frametime delta y pruebas de estrés).
- **No reinventar el subsistema del kernel:** Usar las APIs nativas de Windows NT (`NtSetTimerResolution`, `NtSetSystemInformation`, `SetProcessDefaultCpuSets`, `SetProcessInformation`) en lugar de hacks o drivers inestables.
- **Transparencia hacia el usuario:** Todo cambio en registros o archivos del motor se registra en logs detallados y es revertible con un solo comando.

---


### 7. Instrucciones para Asistentes y Desarrolladores

1. **Nunca omitir verificaciones en seco (dry_run):** Todas las funciones que modifican el sistema (`ApplyEngineUmaPreset`, `DeployDxvk`, `SetMpoDisabled`, etc.) deben disponer de un parámetro `dry_run = false` para pruebas unitarias sin impacto real.
2. **Preservar el Zero-Overhead:** No añadir dependencias de frameworks masivos (Qt, WxWidgets, Boost). La GUI debe seguir siendo puramente Win32/GDI nativa para garantizar `< 15 MB` de consumo de RAM.
3. **Manejo de rutas con espacios y Unicode:** Usar siempre `std::wstring` para interactuar con la API Win32 y convertir limpiamente con `Utf8ToWide` / `WideToUtf8`.
4. **Respaldar antes de escribir:** Cualquier modificación a un archivo `.ini`, `.config` o DLL debe ir precedida de la creación de un archivo `.vegaflow.bak` o `.original.bak`.

---


---

## Base consolidada

### 1. Base Completada: Fase Actual Consolidada

VegaFlow Universal ha alcanzado la madurez de su núcleo de optimización y arquitectura de software, validado en hardware real y suites sintéticas:

#### 1.1. Arquitectura Modular Limpia y Pipeline de Compilación
- **Desacoplamiento Estructural:** El código base se organiza en dominios modulares por función, con cada `.hpp` junto a su `.cpp`:
  - [`src/detect/`](../../src/detect): hardware, topología de CPU y detección de juegos.
  - [`src/optimize/`](../../src/optimize): Orquestación de CPU, optimizadores gráficos y de temporizador NT, DXVK, escalador FSR 1.0/NIS, gestión de memoria UMA y presets de motores.
  - [`src/safety/`](../../src/safety): Subsistema Zero-Trust `SandboxGuard` y telemetría matemática de frametimes.
  - [`src/ui/`](../../src/ui): Ventana nativa Win32/GDI en modo Slate/Zinc Dark, pestañas y componentes.
  - [`src/windows/`](../../src/windows): llamadas indocumentadas NTDLL y privilegios de seguridad.
  - [`src/app/`](../../src/app) y [`src/bridge/`](../../src/bridge): punto de entrada del daemon y contrato C++ ↔ Rust.
- **Automatización de Build:** Script [`scripts/build.ps1`](../../scripts/build.ps1) con detección inteligente del compilador MinGW-w64 `g++` (en PATH o en Scoop), flags C++20 con enlace estático total (`-static -static-libgcc -static-libstdc++`) y ejecución opcional de pruebas con `-RunTests`.
- **Control de Versiones y Limpieza:** Directorio `build/` libre de residuos (`*.o`, `*.old*`, logs) y `.gitignore` estandarizado.

#### 1.2. Motor Universal de Clasificación y Guards (`ApuCapabilityGuard`)
Especificado en [`hardware-y-tiers.md`](hardware-y-tiers.md) e implementado en [`src/detect/hardware_profiler.cpp`](../../src/detect/hardware_profiler.cpp):
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

#### 1.3. Especialización Dual DirectX 11 y DirectX 12
- **DirectX 11:** Despliegue seguro de `DXVK-GPLAsync` adaptado al recuento de núcleos, limitador inteligente de teselación AMD (8x/16x/32x) y cola de baja latencia `FlipQueueSize=1`.
- **DirectX 12:** Guard dinámico de colas de cómputo asíncrono (`AsyncCompute`), presupuestos optimizados de sombras en cascada (CSM) y streaming proporcional de pools de texturas UMA en motores Unreal y Unity.

#### 1.4. Runtime DXGI Pressure Guard y Blindaje Anti-Cheat
- **Monitoreo en Tiempo Real (`EvaluateUmaPressureGuard`):** Sondaje del presupuesto DXGI 1.4 (`IDXGIAdapter3`). Si el consumo local alcanza el **85%** o la memoria física libre es inferior a **1536 MB**, mitiga proactivamente mediante recorte de conjuntos de trabajo secundarios sin pausas perceptibles.
- **Zero-Trust Lockdown (`SandboxGuard`):** Aislamiento de rutas canónicas, whitelist estricta y bloqueo total ante firmas de kernel anti-cheat (HoYoKProtect, Vanguard, EAC, BattlEye, ACE).
- **Cobertura de Pruebas:** **31 de 31 pruebas unitarias e integradas C++20 y 15 de 15 pruebas Rust pasando en verde** (`TEST RESULTS: 31 PASSED, 0 FAILED`).

#### 1.5. Núcleo Híbrido Rust FFI (`vegaflow-core`)
Implementado en el crate estático [`rust/vegaflow-core/`](../../rust/vegaflow-core) y enlazado sin sobrecarga vía C ABI ([`src/bridge/rust_bridge.h`](../../src/bridge/rust_bridge.h)):
- **Evaluador de Presión UMA (`rust/vegaflow-core/src/uma_memory.rs`):** Funciones `vf_rust_evaluate_uma_pressure` y `vf_rust_evaluate_nt_memory_lists`. Algoritmos deterministas sin asignación dinámica de memoria en el heap (Zero-Allocation), verificando saturación local DXGI, reservas del sistema operativo y listas Standby de NT.
- **Topología L3 y Gobernanza de CPU (`rust/vegaflow-core/src/cpu_governor.rs`):** Función `vf_rust_partition_topology_l3` para aislar hilos del juego en la caché L3 principal para arquitecturas simétricas o heterogéneas (Zen 2/3/4/5 y procesadores híbridos), junto con la máquina de estados e histéresis `vf_rust_evaluate_background_confinement` (umbrales de 0.50 y 0.25 núcleos lógicos).
- **Scanner Anti-Cheat y Blindaje de Archivos (`rust/vegaflow-core/src/sandbox_core.rs`):** Detección formal y sin falsos positivos de firmas de Anti-Cheat de kernel (`vf_rust_contains_anticheat_signature`) y protección estricta contra modificaciones accidentales a binarios de arranque del sistema (`vf_rust_is_critical_system_file`).
- **Seguridad Incondicional:** Compilación estática con LTO completo, `panic = "abort"`, y verificación formal mediante 15 pruebas unitarias internas.

---
