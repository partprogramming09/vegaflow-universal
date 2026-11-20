# Design record: APU classification and policies

**In short:** This design record, dated 2026-11-01, explains why VegaFlow stopped using one set of fixed numbers for every machine. Each machine is now classified into a tier, and each tier gets its own limits for memory, DirectX and CPU power. The section also gives the rules that choose the tier and a table with the values per tier. For example, the test machine is classified as Tier 2, and its settings stay the same as before.

**Reader:** people

## Design specification: universal DX11/DX12 optimization for APUs and adaptive guard system (`ApuCapabilityGuard`)

**Date:** 2026-11-01  
**Project:** VegaFlow Universal  
**Status:** Implemented and verified (29/29 tests passing)

---

### 1. Problem and motivation

Although **VegaFlow Universal** started as an optimization framework for unified memory architectures (UMA), several key subsystems contain fixed (*hardcoded*) constants calibrated only for the original test machine (**AMD Ryzen 5 4600H + Radeon RX Vega 6 with 16 GB DDR4 dual-channel**):

1. **`DxvkManager` (DirectX 11 -> Vulkan):** fixes `dxvk.numCompilerThreads = 6`, `dxgi.maxDeviceMemory = 4096` and `dxgi.maxSharedMemory = 8192`.
   - On entry-level APUs (`4 cores / 8 GB RAM`), assigning 6 compile threads and 4 GB of VRAM saturates the CPU and causes unexpected crashes from running out of memory (*Out of Memory*).
   - On advanced APUs (`RDNA 2 / RDNA 3 / RDNA 3.5 / Intel Arc` with `8–12 cores` and `24–32 GB RAM`), limiting memory to 4 GB wastes the hardware.
2. **`SteamEngineOptimizer` (DirectX 11 and DirectX 12 in 3D engines):** forces `D3D12.AsyncCompute=0`, `r.RDG.AsyncCompute=0`, `r.Streaming.PoolSize=4096`, `r.Shadow.MaxCSMResolution=1024` and `r.VolumetricFog=0` without distinguishing the APU's capability.
   - While on **GCN 5 / Vega (6–8 CUs)** turning off `AsyncCompute` in DX12 is essential to avoid blocked queues and *context-switching stutter*, on **advanced APUs (Radeon 680M/780M/890M with 12–16 WGP/CUs)** turning off `AsyncCompute` or cutting shadows aggressively **lowers performance and visual quality** for no reason.
3. **`GraphicsOptimizer` and `SystemInfo`:**
   - `SetApuPowerMode` takes the CPU frequency ceiling from `policy.cpu_stapm_cap_mhz` (currently `0`, no ceiling in any tier; see 3.1) and the tessellation at `16x`. On a Zen 4 / Zen 5 APU (whose thermal efficiency point is around 4.2–4.5 GHz, or which has dual RDNA3 geometry engines), a fixed 3.4 GHz ceiling would throttle FPS in CPU-bound games (not measured).
   - `SystemInfo::GatherGpu()` appends a suffix chosen by GPU architecture to any AMD UMA integrated GPU: `(Vega / GCN 5.0)` for GCN 5, `(RDNA 2 UMA)` for RDNA 2, `(RDNA 3/3.5 UMA)` for RDNA 3 and later, and `(Integrada)` for other AMD integrated GPUs.
     > Note: the design text described a fixed `(Vega 6 / GCN 5.0)` suffix. The current code writes `(Vega / GCN 5.0)` for every GCN 5 machine (`src/session/system_info.cpp:169-176`).

**Goal:** turn DirectX 11 and DirectX 12 handling into a **universal system classified by APU tiers with Guards (`ApuCapabilityGuard`)**, keeping the optimal behavior on the Ryzen 5 4600H (`Tier2_MidVegaUMA`) 100% intact and applying a **"Do-No-Harm" guard + dynamic UMA budget ceiling** for entry-level and advanced APUs.

---

### 2. Architecture of the `ApuCapabilityGuard` subsystem

A deterministic classification and policy structure is introduced in [`src/model/hardware.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/model/hardware.hpp) (types; detection in `src/detect/hardware_profiler.cpp`) and [`src/optimize/graphics/graphics_optimizer.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/optimize/graphics/graphics_optimizer.hpp), which acts as the **single source of truth** for the DX11, DX12, UMA memory and power limits.

#### 2.1. `ApuTier` enumeration and `GpuArchitectureGen` generation

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

> Note: the enumerator names above exist in `src/model/hardware.hpp`. The comments inside this block are the original Spanish text, kept as written.

#### 2.2. Deterministic classification rules

1. **Detection of `GpuArchitectureGen` (`HardwareProfiler::DetectHardware`)**:
   - If `mem_arch == MemoryArchitecture::Dedicated_VRAM` (`dedicated_vram_bytes > 2 GB`): `Discrete_GPU`.
   - If `vendor == GpuVendor::AMD` and `UMA_Integrated`:
     - Inspects the CPU family/model (`CPUID` leaf `1`) and the DXGI adapter description:
       - Family `0x17` (Zen/Zen+/Zen 2: Raven Ridge, Picasso, Renoir, Lucienne) or family `0x19` with models `0x50..0x5F` (Cezanne / Barcelo): **`AMD_GCN5_Vega`**.
       - Family `0x19` with models `0x40..0x4F` (Rembrandt) or a description containing `"680M"`, `"660M"`, `"610M"`: **`AMD_RDNA2`**.
       - Family `0x19` with models `>= 0x70` (Phoenix, Hawk Point), or family `>= 0x1A` (Strix Point / Zen 5), or a description with `"780M"`, `"760M"`, `"740M"`, `"880M"`, `"890M"`: **`AMD_RDNA3_Plus`**.
       - Fallback for AMD UMA if it contains `"Vega"` or `"Radeon(TM) Graphics"` in family `0x17`: **`AMD_GCN5_Vega`**.
   - If `vendor == GpuVendor::Intel` and `UMA_Integrated`:
     - If the description contains `"Arc"` or `"Iris(R) Xe"`: **`Intel_Xe_Arc`**.
     - Otherwise: **`Intel_Gen9_11_UHD`**.

2. **Assignment of `ApuTier`**:
   - If `mem_arch == Dedicated_VRAM` or `vendor == GpuVendor::NVIDIA`: **`Tier4_DiscreteOrCustom`**.
   - If `total_physical_ram_bytes <= 9ULL * 1024 * 1024 * 1024` (`<= 9 GB`) **or** `physical_cores <= 4` **or** `arch_gen == Intel_Gen9_11_UHD`: **`Tier1_EntryUMA`**.
   - If `arch_gen == AMD_RDNA2` **or** `arch_gen == AMD_RDNA3_Plus` **or** `arch_gen == Intel_Xe_Arc` **or** `total_physical_ram_bytes >= 22ULL * 1024 * 1024 * 1024` (`>= 22 GB`): **`Tier3_AdvancedUMA`**.
   - In any other UMA case (including the Ryzen 5 4600H 6C/12T with 16 GB RAM and Vega 6): **`Tier2_MidVegaUMA`**.

---

### 3. Policy and guard contract (`ApuOptimizationPolicy`)

`HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& hw, uint64_t live_dxgi_local_budget_bytes = 0)` returns an `ApuOptimizationPolicy` structure with all parameters bounded by **Hard Guards**:

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
    uint32_t cpu_stapm_cap_mhz = 3400;   // Valor por defecto del struct; BuildOptimizationPolicy lo fija a 0 (sin techo) en todos los tiers
    bool apply_uma_vram_reg_override = true; // False en Tier4 (GPU dedicada)
};
```

#### 3.1. Parameter table by tier

| Parameter / guard | `Tier1_EntryUMA` (8GB / <=4C) | `Tier2_MidVegaUMA` (16GB Zen2/3 Vega) | `Tier3_AdvancedUMA` (RDNA2/3/Xe / >=24GB) | `Tier4_DiscreteOrCustom` (dGPU) |
| :--- | :--- | :--- | :--- | :--- |
| **`guard_mode_name`** | `"Survival Guard"` | `"Sweet-Spot Guard"` | `"Do-No-Harm Guard"` | `"Pass-Through Guard"` |
| **`os_reserve_floor_mb`** | `3072 MB` | `4096 MB` | `4096 MB` | `4096 MB` |
| **`safe_vram_pool_mb`** | `clamp(RAM*0.22, 1536, 2048)` | `4096 MB` | `clamp(RAM*0.30, 4096, 8192)` | `4096 MB` (no registry override) |
| **`safe_shared_pool_mb`** | `4096 MB` | `8192 MB` | `clamp(RAM*0.50, 8192, 16384)` | `8192 MB` |
| **Dynamic DXGI guard** | If `live_dxgi_budget > 0`, `min(safe_vram_pool_mb, 80% of live_dxgi_budget)` with a floor of `1536 MB` | Keeps `4096 MB` (or `80%` if the system has less than 12 GB free) | If `live_dxgi_budget > 0`, caps it so the OS always keeps `>= 4096 MB` | N/A |
| **DX11 `dxvk_compiler_threads`** | `clamp(Cores - 1, 1, 3)` | `clamp(Cores, 4, 6)` (`6` on 6C) | `clamp(Cores - 2, 4, 8)` | `clamp(Cores - 2, 4, 8)` |
| **DX11 `amd_tessellation_cap`** | `8` (8x) | `16` (16x) | `32` (32x — avoids 64x over-tessellation without degrading meshes) | `64` (Native) |
| **DX12 `disable_dx12_async_compute`** | `true` (`AsyncCompute=0`) | `true` (`AsyncCompute=0`) | **`false` (the guard protects `AsyncCompute=1`)** | **`false`** |
| **DX12 `ue_max_csm_resolution`** | `512` | `1024` | `2048` | `2048` |
| **CPU `cpu_stapm_cap_mhz`** | `0 MHz` | `0 MHz` | **`0 MHz` (no fixed clock cap; only Boost Mode 4 Efficient Aggressive)** | `0 MHz` |
