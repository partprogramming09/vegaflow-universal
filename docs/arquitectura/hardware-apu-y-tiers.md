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

