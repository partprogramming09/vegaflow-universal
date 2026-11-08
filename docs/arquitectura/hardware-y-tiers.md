# Hardware APU y Tiers

## 2. Arquitectura de Hardware y Restricciones APU (AMD Ryzen 5 4600H + Radeon RX Vega 6)

VegaFlow está optimizado a nivel de microarquitectura para APUs (Accelerated Processing Units) de AMD, con el banco de pruebas principal centrado en el procesador **AMD Ryzen 5 4600H** (6 núcleos, 12 hilos, arquitectura Zen 2 a 3.0 GHz base / 4.0 GHz boost) acoplado a gráficos integrados **AMD Radeon RX Vega 6** (6 Compute Units / 384 Stream Processors a 1500 MHz, arquitectura GCN 5.0).

```
+----------------------------------------------------------------------------------+
|                     ARQUITECTURA UMA (UNIFIED MEMORY)                            |
|                                                                                  |
|   +-----------------------+              +-----------------------+               |
|   |   CPU Zen 2 (6C/12T)  |              |   iGPU Vega 6 (6 CUs) |               |
|   |  35W-45W Thermal TDP  |              |  384 Stream Processors|               |
|   +-----------+-----------+              +-----------+-----------+               |
|               |                                      |                           |
|               +------------------+-------------------+                           |
|                                  |                                               |
|                    [ Infinity Fabric Bus / IMC ]                                 |
|                                  |                                               |
|                     Canal Dual DDR4-3200 MHz                                     |
|                  Ancho de Banda: ~38 - 42 GB/s                                   |
|                                                                                  |
|      [ Memoria Física Compartida: 16 GB Total ]                                  |
|      - Falso reporte de BIOS: 512 MB "VRAM Dedicada"                             |
|      - Memoria real compartida disponible para GPU: Hasta 8192 MB                |
+----------------------------------------------------------------------------------+
```

### 2.1. El Cuello de Botella de la Memoria UMA (Unified Memory Architecture)
A diferencia de una tarjeta gráfica dedicada que cuenta con su propio bus GDDR6 de 128 o 256 bits (ancho de banda de 200 a 500+ GB/s), la APU comparte la memoria RAM del sistema a través de un bus DDR4 de 128 bits dual-channel con un ancho de banda teórico de ~42 GB/s.
- La CPU y la iGPU **compiten por el mismo ancho de banda**. Si la CPU realiza lecturas excesivas de memoria o compresión en segundo plano, la GPU sufre caídas drásticas de tasa de llenado y framerate.
- **Mitigación VegaFlow:** Aislamiento de procesos secundarios mediante CPU Sets, desactivación de GameDVR y temporización de purgas de memoria únicamente en periodos de bajo I/O de disco (`< 15 MB/s`).

### 2.2. El Falso Límite de 512 MB de VRAM y el Texture Streaming
Por razones de compatibilidad con software legado de 32 bits, las BIOS y controladores de AMD reportan a menudo solo 512 MB de "VRAM Dedicada" (`DXGI_ADAPTER_DESC::DedicatedVideoMemory`).
- **El Problema:** Motores modernos como Unreal Engine 4 y Unreal Engine 5 leen esta propiedad y restringen automáticamente el tamaño del banco de texturas (`r.Streaming.PoolSize`). El resultado es una catástrofe visual: texturas borrosas que nunca terminan de cargar, pop-in constante y parones continuos de CPU mientras el motor descarga y recarga texturas continuamente.
- **La Solución Técnica de VegaFlow:** Desactivar la restricción del motor e instruirlo a utilizar la memoria física real:
  ```ini
  r.Streaming.LimitPoolSizeToVRAM=0
  r.Streaming.PoolSize=3000
  r.Streaming.FullyLoadUsedTextures=1
  r.Streaming.FramesForFullUpdate=1
  r.Streaming.MaxNumTexturesToStreamPerFrame=100
  r.Streaming.Boost=1.5
  ```

### 2.3. Ausencia de GPL (`VK_EXT_graphics_pipeline_library`) en Drivers AMD GCN 5 / Vega
Los controladores oficiales de AMD Adrenalin para la arquitectura Vega no exponen la extensión de Vulkan `VK_EXT_graphics_pipeline_library`.
- En emuladores y capas de traducción estándar como DXVK vanilla, la falta de GPL obliga a compilar los shaders en el hilo de renderizado principal en el momento exacto en que un efecto aparece en pantalla, produciendo parones severos (shader compilation stutter).
- **Solución VegaFlow:** Integración nativa del subsistema `DXVK-GPLAsync` con configuración multihilo para Vega 6:
  ```ini
  dxvk.gpl = True
  dxvk.enableAsync = True
  dxvk.gplAsyncCache = True
  dxvk.numCompilerThreads = 6
  dxgi.maxDeviceMemory = 4096
  dxgi.maxSharedMemory = 8192
  ```
  Esto delega la compilación de pipelines a 6 hilos de CPU en segundo plano, eliminando completamente los congelamientos durante el juego.

### 2.4. Inviabilidad de `AsyncCompute` en 6 Compute Units
`AsyncCompute` permite a GPUs de gran escala ejecutar computación y gráficos de manera concurrente en colas separadas. Sin embargo, en una iGPU con tan solo 6 Compute Units físicas (384 ALUs), el hardware scheduler no dispone de recursos ociosos suficientes. Forzar `AsyncCompute` satura los CUs, añade latencia de context-switching y empeora el frametime.
- **Solución VegaFlow:** En motores Unreal Engine, VegaFlow desactiva explícitamente el cómputo asíncrono para liberar los CUs exclusivamente para la rasterización:
  ```ini
  r.RDG.AsyncCompute=0
  D3D12.AsyncCompute=0
  ```

### 2.5. Balance Térmico CPU/GPU (Presupuesto STAPM de 35W-45W)
En laptops APU, la CPU y la GPU comparten el mismo disipador térmico y el mismo paquete de energía. Si la CPU sube a 4.0 GHz con voltajes de 1.35V consumiendo 35W, la iGPU se ve estrangulada a 600-800 MHz.
- **Solución VegaFlow:** Perfil de energía APU especializado (`SetApuLaptopPowerProfile`):
  - Limita el modo boost a *Efficient Aggressive* y la frecuencia máxima sostenida a 3.2 GHz con 99% throttle cap.
  - La CPU opera holgadamente consumiendo solo 12-14W, dejando 20W de TDP continuo para que la Radeon Vega 6 trabaje a sus 1500 MHz máximos estables sin caídas térmicas.
  - Desactiva PCIe ASPM en modo AC para eliminar latencias en el bus Infinity Fabric.

### 2.6. Motor de Clasificación Universal de 4 Niveles (4-Tier APU Engine) y Safety Guards (`ApuCapabilityGuard`)
Para trascender el soporte exclusivo a Ryzen 5 4600H sin introducir regresiones ni degradar silicios modernos, VegaFlow incorpora un clasificador determinista de 4 niveles (`ApuTier`) gestionado por `HardwareProfiler::BuildOptimizationPolicy()`:

1. **`Tier1_EntryUMA` [Survival Guard]:**
   - **Hardware objetivo:** APUs con RAM $\le$ 8–9 GB o $\le$ 4 núcleos físicos (Vega 3/6/8 básica, Athlon Mobile, Intel Gen 9–11 UHD).
   - **Directivas:** Techo estricto de textura pool VRAM entre 1536 y 2048 MB (`safe_vram_pool_mb`) para evitar cuelgues OOM del sistema; 3 hilos de compilación en DXVK; límite de teselación AMD a 8x; `disable_dx12_async_compute = true`; tope de frecuencia STAPM a 3000 MHz.
2. **`Tier2_MidVegaUMA` [Sweet-Spot Guard]:**
   - **Hardware objetivo:** APUs Vega/GCN5 con 12–20 GB de RAM o procesadores Zen 2/Zen 3 de 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - **Directivas (100% retrocompatible con el testbed):** Pool VRAM a 4096 MB; 6 hilos dedicados de compilación en DXVK-GPLAsync; límite de teselación a 16x; `disable_dx12_async_compute = true`; tope de frecuencia STAPM a 3400 MHz.
3. **`Tier3_AdvancedUMA` [Do-No-Harm Guard]:**
   - **Hardware objetivo:** APUs modernas RDNA2/RDNA3/RDNA3.5 (Radeon 680M, 780M, 890M, Strix Point) e Intel Xe/Arc UMA, o sistemas UMA con $\ge$ 22 GB de RAM.
   - **Directivas ("Do-No-Harm"):** Preserva intacto el cómputo asíncrono moderno (`D3D12.AsyncCompute=1` y `r.RDG.AsyncCompute=1`); **no estrangula la CPU** (`cpu_stapm_cap_mhz = 0`); asigna pool UMA generoso y adaptativo de 6144 a 8192 MB; teselación permitida hasta 32x; 6 a 8 hilos en DXVK.
4. **`Tier4_DiscreteOrCustom` [Pass-Through Guard]:**
   - **Hardware objetivo:** GPUs dedicadas (>2 GB VRAM dedicada) o arquitecturas discretas.
   - **Directivas:** Modo pass-through transparente sin aplicar overrides forzados de UMA ni limitaciones de silicio integrado.

#### Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):
Supervisa en caliente el uso real del presupuesto de memoria local de vídeo (`local_usage_bytes / local_budget_bytes`) mediante DXGI 1.4 (`IDXGIAdapter3`). Si la saturación del presupuesto sobrepasa el **85%** o la memoria física libre cae por debajo del umbral crítico de **1536 MB**, activa el guard defensivo de forma no invasiva, registrando la intervención y recortando el working set de procesos en segundo plano para prevenir paging stalls hacia disco o cierres abruptos por OOM.

---


---

## Especificación de Diseño: Optimización Universal DX11/DX12 para APUs y Sistema de Guards Adaptativos (`ApuCapabilityGuard`)

**Fecha:** 2026-11-01  
**Proyecto:** VegaFlow Universal  
**Estado:** Implementado y Verificado (29/29 Tests Passing)

---

### 1. Problema y Motivación

Aunque **VegaFlow Universal** nació como un framework de optimización para arquitecturas de memoria unificada (UMA), varios subsistemas clave contienen constantes fijas (*hardcoded*) calibradas exclusivamente para el banco de pruebas original (**AMD Ryzen 5 4600H + Radeon RX Vega 6 con 16 GB DDR4 Dual-Channel**):

1. **`DxvkManager` (DirectX 11 -> Vulkan):** Fija `dxvk.numCompilerThreads = 6`, `dxgi.maxDeviceMemory = 4096` y `dxgi.maxSharedMemory = 8192`.
   - En APUs de entrada (`4 núcleos / 8 GB RAM`), asignar 6 hilos de compilación y 4 GB de VRAM provoca saturación de CPU y cierres inesperados por falta de memoria (*Out of Memory*).
   - En APUs avanzadas (`RDNA 2 / RDNA 3 / RDNA 3.5 / Intel Arc` con `8–12 núcleos` y `24–32 GB RAM`), limitar la memoria a 4 GB desaprovecha el hardware.
2. **`SteamEngineOptimizer` (DirectX 11 y DirectX 12 en Motores 3D):** Fuerza `D3D12.AsyncCompute=0`, `r.RDG.AsyncCompute=0`, `r.Streaming.PoolSize=4096`, `r.Shadow.MaxCSMResolution=1024` y `r.VolumetricFog=0` sin distinguir la capacidad de la APU.
   - Mientras que en **GCN 5 / Vega (6–8 CUs)** desactivar `AsyncCompute` en DX12 es indispensable para evitar colas bloqueadas y *context-switching stutter*, en **APUs avanzadas (Radeon 680M/780M/890M con 12–16 WGP/CUs)** desactivar `AsyncCompute` o recortar sombras agresivamente **reduce el rendimiento y la calidad visual** sin necesidad.
3. **`GraphicsOptimizer` y `SystemInfo`:**
   - `SetApuPowerMode` fija el techo de frecuencia de CPU en `3400 MHz` y la teselación en `16x`. En una APU Zen 4 / Zen 5 (cuyo punto de eficiencia térmica ronda los 4.2–4.5 GHz o que cuenta con motores dobles de geometría RDNA3), clavar 3.4 GHz estrangula los FPS en juegos dependientes de CPU.
   - `SystemInfo::GatherGpu()` concatena la cadena `"(Vega 6 / GCN 5.0)"` a cualquier GPU integrada AMD UMA.

**Objetivo:** Transformar el manejo de DirectX 11 y DirectX 12 en un **sistema universal clasificado por Tiers de APU con Guards de Seguridad (`ApuCapabilityGuard`)**, preservando al 100% el comportamiento óptimo en el Ryzen 5 4600H (`Tier2_MidVegaUMA`) y aplicando un **Guard "Do-No-Harm" + Techo Dinámico de Presupuesto UMA** para APUs de entrada y APUs avanzadas.

---

### 2. Arquitectura del Subsistema `ApuCapabilityGuard`

Se introduce una estructura determinista de clasificación y políticas en [`src/detect/hardware_profiler.hpp`](../../src/detect/hardware_profiler.hpp) e [`src/optimize/graphics/graphics_optimizer.hpp`](../../src/optimize/graphics/graphics_optimizer.hpp) que actúa como **única fuente de verdad** para los límites de DX11, DX12, memoria UMA y energía.

#### 2.1. Enumeración de Tiers (`ApuTier`) y Generación de Arquitectura (`GpuArchitectureGen`)

```cpp
enum class ApuTier : uint8_t {
    Tier1_EntryUMA = 1,      // APUs <= 8GB RAM o <= 4 núcleos físicos (Vega 3/6/8 mobile entry, Athlon, UHD)
    Tier2_MidVegaUMA = 2,    // APUs 12-20GB RAM GCN5/Vega o Zen2/Zen3 6C-8C (Ryzen 4600H, 5500U, 5600G/H)
    Tier3_AdvancedUMA = 3,   // APUs RDNA2/RDNA3/RDNA3.5 (680M, 780M, 890M), Intel Arc/Xe, o UMA >= 24GB RAM
    Tier4_DiscreteOrCustom = 4 // Sistemas con dGPU dedicada (> 2GB VRAM dedicada) o arquitectura desconocida
};

enum class GpuArchitectureGen : uint8_t {
    Unknown = 0,
    AMD_GCN5_Vega = 1,       // Radeon RX Vega 3/6/7/8/11 (GCN 5.0 / 5.1)
    AMD_RDNA2 = 2,           // Radeon 660M / 680M / Steam Deck Aerith/Sephiroth
    AMD_RDNA3_Plus = 3,      // Radeon 740M / 760M / 780M / 880M / 890M (RDNA 3 / 3.5)
    Intel_Gen9_11_UHD = 4,   // Intel UHD Graphics (Skylake - Comet Lake)
    Intel_Xe_Arc = 5,        // Intel Iris Xe / Arc Integrated (Tiger Lake - Lunar Lake)
    Discrete_GPU = 6         // Tarjetas gráficas dedicadas (AMD RX / NVIDIA GTX-RTX / Intel Arc dGPU)
};
```

#### 2.2. Reglas Deterministas de Clasificación

1. **Detección de `GpuArchitectureGen` (`HardwareProfiler::DetectHardware`)**:
   - Si `mem_arch == MemoryArchitecture::Dedicated_VRAM` (`dedicated_vram_bytes > 2 GB`): `Discrete_GPU`.
   - Si `vendor == GpuVendor::AMD` y `UMA_Integrated`:
     - Inspecciona la familia/modelo de CPU (`CPUID` hoja `1`) y la descripción del adaptador DXGI:
       - Familia `0x17` (Zen/Zen+/Zen 2: Raven Ridge, Picasso, Renoir, Lucienne) o Familia `0x19` con modelos `0x50..0x5F` (Cezanne / Barcelo): **`AMD_GCN5_Vega`**.
       - Familia `0x19` con modelos `0x40..0x4F` (Rembrandt) o descripción que contenga `"680M"`, `"660M"`, `"610M"`: **`AMD_RDNA2`**.
       - Familia `0x19` con modelos `>= 0x70` (Phoenix, Hawk Point) o Familia `>= 0x1A` (Strix Point / Zen 5) o descripción con `"780M"`, `"760M"`, `"740M"`, `"880M"`, `"890M"`: **`AMD_RDNA3_Plus`**.
       - Fallback para AMD UMA si contiene `"Vega"` o `"Radeon(TM) Graphics"` en familia `0x17`: **`AMD_GCN5_Vega`**.
   - Si `vendor == GpuVendor::Intel` y `UMA_Integrated`:
     - Si la descripción contiene `"Arc"` o `"Iris(R) Xe"`: **`Intel_Xe_Arc`**.
     - En caso contrario: **`Intel_Gen9_11_UHD`**.

2. **Asignación de `ApuTier`**:
   - Si `mem_arch == Dedicated_VRAM` o `vendor == GpuVendor::NVIDIA`: **`Tier4_DiscreteOrCustom`**.
   - Si `total_physical_ram_bytes <= 9ULL * 1024 * 1024 * 1024` (`<= 9 GB`) **o** `physical_cores <= 4` **o** `arch_gen == Intel_Gen9_11_UHD`: **`Tier1_EntryUMA`**.
   - Si `arch_gen == AMD_RDNA2` **o** `arch_gen == AMD_RDNA3_Plus` **o** `arch_gen == Intel_Xe_Arc` **o** `total_physical_ram_bytes >= 22ULL * 1024 * 1024 * 1024` (`>= 22 GB`): **`Tier3_AdvancedUMA`**.
   - En cualquier otro caso UMA (incluyendo Ryzen 5 4600H 6C/12T con 16 GB RAM y Vega 6): **`Tier2_MidVegaUMA`**.

---

### 3. Contrato de Políticas y Guards (`ApuOptimizationPolicy`)

`HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& hw, uint64_t live_dxgi_local_budget_bytes = 0)` devuelve una estructura `ApuOptimizationPolicy` con todos los parámetros acotados por **Hard Guards**:

```cpp
struct ApuOptimizationPolicy {
    ApuTier tier = ApuTier::Tier2_MidVegaUMA;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::AMD_GCN5_Vega;
    std::string tier_label;              // Ej: "Tier 2: Mid Vega UMA [Sweet-Spot Guard]"
    std::string guard_mode_name;         // "Survival Guard", "Sweet-Spot Guard", "Do-No-Harm Guard", "Pass-Through Guard"

    // --- Guard de Memoria UMA (DX11 & DX12) ---
    uint32_t os_reserve_floor_mb = 4096; // Colchón intocable para el kernel NT y page tables
    uint32_t safe_vram_pool_mb = 4096;   // PoolSize para UE4/UE5, DXVK maxDeviceMemory y VRAM Override
    uint32_t safe_shared_pool_mb = 8192; // dxgi.maxSharedMemory en DXVK
    double runtime_pressure_threshold = 0.85; // Disparo del Runtime Guard al 85% del budget DXGI

    // --- Especialización DirectX 11 ---
    uint32_t dxvk_compiler_threads = 6;  // Hilos asíncronos de compilación de shaders
    uint32_t amd_tessellation_cap = 16;  // 8x (Tier1), 16x (Tier2), 32x (Tier3), 64x (Tier4)
    uint32_t max_anisotropy = 4;         // 2x (Tier1), 4x (Tier2), 8x (Tier3), 16x (Tier4)
    bool recommend_dxvk_gplasync = true; // True en GCN5/Vega (sin GPL nativo), False por defecto en RDNA3+ con GPL nativo

    // --- Especialización DirectX 12 ---
    bool disable_dx12_async_compute = true; // True en Tier1/Tier2 (<=8 CUs), False en Tier3/Tier4 (Do-No-Harm Guard)
    uint32_t ue_max_csm_resolution = 1024;  // 512 (Tier1), 1024 (Tier2), 2048 (Tier3/4)
    uint32_t ue_max_shadow_cascades = 2;    // 1 (Tier1), 2 (Tier2), 3 (Tier3), 4 (Tier4)
    bool disable_volumetric_fog = true;     // True en Tier1/2, False en Tier3/4

    // --- Guard Térmico y de Reloj CPU (STAPM) ---
    uint32_t cpu_stapm_cap_mhz = 3400;   // 3000 (Tier1), 3400 (Tier2), 0 = Sin techo fijo en Tier3/4 (Do-No-Harm)
    bool apply_uma_vram_reg_override = true; // False en Tier4 (GPU dedicada)
};
```

#### 3.1. Tabla de Parametrización por Tier

| Parámetro / Guard | `Tier1_EntryUMA` (8GB / <=4C) | `Tier2_MidVegaUMA` (16GB Zen2/3 Vega) | `Tier3_AdvancedUMA` (RDNA2/3/Xe / >=24GB) | `Tier4_DiscreteOrCustom` (dGPU) |
| :--- | :--- | :--- | :--- | :--- |
| **`guard_mode_name`** | `"Survival Guard"` | `"Sweet-Spot Guard"` | `"Do-No-Harm Guard"` | `"Pass-Through Guard"` |
| **`os_reserve_floor_mb`** | `3072 MB` | `4096 MB` | `4096 MB` | `4096 MB` |
| **`safe_vram_pool_mb`** | `clamp(RAM*0.22, 1536, 2048)` | `4096 MB` | `clamp(RAM*0.30, 4096, 8192)` | `4096 MB` (sin override reg) |
| **`safe_shared_pool_mb`** | `4096 MB` | `8192 MB` | `clamp(RAM*0.50, 8192, 16384)` | `8192 MB` |
| **Guard Dinámico DXGI** | Si `live_dxgi_budget > 0`, `min(safe_vram_pool_mb, 80% de live_dxgi_budget)` con suelo de `1536 MB` | Mantiene `4096 MB` (o `80%` si el sistema tiene menos de 12 GB libres) | Si `live_dxgi_budget > 0`, acota para dejar siempre `>= 4096 MB` al SO | N/A |
| **DX11 `dxvk_compiler_threads`** | `clamp(Cores - 1, 1, 3)` | `clamp(Cores, 4, 6)` (`6` en 6C) | `clamp(Cores - 2, 4, 8)` | `clamp(Cores - 2, 4, 8)` |
| **DX11 `amd_tessellation_cap`** | `8` (8x) | `16` (16x) | `32` (32x — evita sobre-teselación 64x sin degradar mallas) | `64` (Nativo) |
| **DX12 `disable_dx12_async_compute`** | `true` (`AsyncCompute=0`) | `true` (`AsyncCompute=0`) | **`false` (Guard protege `AsyncCompute=1`)** | **`false`** |
| **DX12 `ue_max_csm_resolution`** | `512` | `1024` | `2048` | `2048` |
| **CPU `cpu_stapm_cap_mhz`** | `3000 MHz` | `3400 MHz` | **`0 MHz` (Sin cap de reloj fijo; solo Boost Mode 4 Efficient Aggressive)** | `0 MHz` |

---

### 4. Integración en los Módulos Existentes

#### 4.1. `HardwareProfiler` y `SystemInfo`
- En [`src/detect/hardware_profiler.hpp`](../../src/detect/hardware_profiler.hpp) y [`src/detect/hardware_profiler.cpp`](../../src/detect/hardware_profiler.cpp):
  - Añadir `ApuTier`, `GpuArchitectureGen` y `ApuOptimizationPolicy`.
  - Añadir campos `arch_gen` y `apu_tier` a `SystemHardwareProfile`.
  - Implementar `HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0)` y funciones auxiliares `ApuTierToString` y `ArchGenToString`.
- En [`src/detect/system_info.cpp`](../../src/detect/system_info.cpp) e [`src/detect/system_info.hpp`](../../src/detect/system_info.hpp):
  - En `GatherGpu()`: Reemplazar el sufijo fijo `" (Vega 6 / GCN 5.0)"` por el sufijo real según `arch_gen` (ej. mantiene `" (Vega 6 / GCN 5.0)"` cuando es Renoir 4600H / GCN5 para preservar compatibilidad total con los reportes actuales, pero muestra `" (RDNA 2 UMA)"`, `" (RDNA 3/3.5 UMA)"` o `" (Intel Xe/Arc UMA)"` en otros equipos).
  - En `GatherCpu()`: Ajustar `boost_target_ghz` y `stapm_target` según la política (`3.4 GHz` en Tier 2, `3.0 GHz` en Tier 1, `"Dynamic Boost (Efficient Aggressive / Uncapped Guard)"` en Tier 3).
  - Exportar en `SystemInfo::ToJson()` un bloque adicional `"apu_guard"` con `tier`, `guard_mode`, `safe_vram_pool_mb`, `dx12_async_compute_allowed`, `dxvk_compiler_threads` y `tessellation_cap`.

#### 4.2. `DxvkManager` (Especialización DirectX 11 -> Vulkan)
- En [`src/optimize/dxvk_manager.cpp`](../../src/optimize/dxvk_manager.cpp):
  - En `DeployDxvk()` y `ConfigureApuProfile()`: Consultar `HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware())`.
  - Escribir `dxvk.numCompilerThreads`, `dxgi.maxDeviceMemory` y `dxgi.maxSharedMemory` usando los valores de la política (`policy.dxvk_compiler_threads`, `policy.safe_vram_pool_mb`, `policy.safe_shared_pool_mb`).
  - En `ValidateDxvkApuProfile()`: Aceptar como válido cualquier `compilerThreads` entre `1` y `16` que coincida con la política del sistema (en el Ryzen 5 4600H seguirá siendo `6`, pasando el Test 24 sin cambios).

#### 4.3. `SteamEngineOptimizer` (Especialización DirectX 11 y DirectX 12 en Motores)
- En [`src/optimize/steam/`](../../src/optimize/steam/):
  - En `ApplyEngineUmaPreset()`:
    - Consultar la política `ApuOptimizationPolicy`.
    - Si `vram_pool_mb == 0`, usar `policy.safe_vram_pool_mb`. Si el usuario o llamador pasa un `vram_pool_mb` explícito, acotarlo con el Guard si estamos en `Tier1_EntryUMA` para nunca exceder la reserva del SO.
    - Ajustar `r.Shadow.MaxResolution`, `r.Shadow.MaxCSMResolution`, `r.Shadow.CSM.MaxCascades`, `r.VolumetricFog` y `r.MaxAnisotropy` según el Tier.
    - **DirectX 12 Guard:** Si `policy.disable_dx12_async_compute == true` (Tier 1 y Tier 2), escribir `r.RDG.AsyncCompute=0` y `D3D12.AsyncCompute=0`. Si es `false` (**Tier 3 Advanced APU Guard**), escribir `r.RDG.AsyncCompute=1` y `D3D12.AsyncCompute=1` para permitir que los motores RDNA2/RDNA3 aprovechen sus colas de cómputo asíncrono en DX12.

#### 4.4. `GraphicsOptimizer` (Límites de Driver, Teselación y STAPM)
- En [`src/optimize/graphics/`](../../src/optimize/graphics/):
  - En `SetApuPowerMode(ApuPowerMode::GpuPrioritySustained)`:
    - Consultar `policy.cpu_stapm_cap_mhz`.
    - En Tier 1 (`3000 MHz`) y Tier 2 (`3400 MHz`), aplica el techo de frecuencia para ceder presupuesto térmico STAPM a la iGPU.
    - En **Tier 3 (Advanced APU)**, el Guard fija `cpu_max_freq = 0` (sin límite artificial de MHz) pero mantiene `boost_val = 4` (*Efficient Aggressive*) y `aspm_val = 0`, evitando estrangular núcleos Zen 4 / Zen 5 en juegos DX12.
  - En `SetAmdDx11UmaDriverBoost()`:
    - Usar `policy.amd_tessellation_cap` (`8`, `16` o `32`) en lugar de forzar `16` en todas las generaciones.

#### 4.5. Guard Reactivo en Tiempo Real (`MemoryManager::EvaluateUmaPressureGuard`)
- En [`src/optimize/memory_manager.hpp`](../../src/optimize/memory_manager.hpp) y [`src/optimize/memory_manager.cpp`](../../src/optimize/memory_manager.cpp):
  - Añadir estructura `UmaGuardStatus`:
    ```cpp
    struct UmaGuardStatus {
        bool guard_triggered = false;
        double dxgi_budget_utilization = 0.0; // 0.0 a 1.0+
        uint64_t available_os_headroom_mb = 0;
        uint32_t total_interventions = 0;
        std::string active_reason;
    };
    ```
  - Añadir método `EvaluateUmaPressureGuard(const SystemMemoryTelemetry& telem, const ApuOptimizationPolicy& policy, bool dry_run = false)`:
    - Calcula el ratio `local_usage_bytes / local_budget_bytes` y el colchón de RAM disponible (`free_and_zero_bytes + standby_bytes`).
    - Si el uso del presupuesto DXGI supera `policy.runtime_pressure_threshold` (`85%`) o la RAM libre real cae bajo el umbral crítico (`1536 MB`) mientras un juego está activo:
      - Dispara `guard_triggered = true`, incrementa `total_interventions` y ejecuta una mitigación no invasiva (recorte de working set de procesos de fondo / purga controlada si el disco tiene bajo I/O).

---

### 5. Criterios de Aceptación y Plan de Pruebas

1. **Retrocompatibilidad Total en Ryzen 5 4600H (Tier 2):**
   - Los 28 tests previos existentes en `build/vegaflow_tests.exe` más el nuevo Test 29 deben pasar en verde (`29/29 PASS`).
   - En el hardware actual del usuario (Ryzen 5 4600H, 16 GB RAM, Vega 6), el clasificador debe seleccionar `Tier2_MidVegaUMA`, manteniendo `safe_vram_pool_mb = 4096`, `dxvk_compiler_threads = 6`, `amd_tessellation_cap = 16`, `cpu_stapm_cap_mhz = 3400` y `disable_dx12_async_compute = true`.
2. **Nuevo Test 29 (`Test 29: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards`):**
   - Verificar sintéticamente perfiles de hardware para:
     - **Entry APU (4C/8T, 8 GB RAM, Vega 8):** Verifica `Tier1_EntryUMA`, `safe_vram_pool_mb <= 2048`, `dxvk_compiler_threads <= 3`, `disable_dx12_async_compute == true`, `amd_tessellation_cap == 8`.
     - **Mid APU (6C/12T, 16 GB RAM, Vega 6 Renoir):** Verifica `Tier2_MidVegaUMA`, `safe_vram_pool_mb == 4096`, `dxvk_compiler_threads == 6`, `cpu_stapm_cap_mhz == 3400`.
     - **Advanced APU (8C/16T, 32 GB RAM, Radeon 780M / 890M RDNA3):** Verifica `Tier3_AdvancedUMA`, **Do-No-Harm Guard activo** (`disable_dx12_async_compute == false`, `cpu_stapm_cap_mhz == 0`, `safe_vram_pool_mb >= 6144`, `amd_tessellation_cap == 32`).
     - **Runtime Pressure Guard:** Simular un estado `SystemMemoryTelemetry` con `90%` de uso de presupuesto DXGI en `dry_run = true` y verificar que `EvaluateUmaPressureGuard` activa `guard_triggered = true` sin bloquear el hilo.
3. **Presupuesto de Memoria del Daemon:**
   - El consumo privado de RAM (`Private Working Set`) debe mantenerse estrictamente `< 15 MB`.
