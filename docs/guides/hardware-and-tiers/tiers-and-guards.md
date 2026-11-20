# APU tiers and guards

**In short:** VegaFlow sorts each machine into one of four tiers and applies a matching set of limits. Machines with little memory or few cores get the strictest limits, so they do not run out of memory; Tier 2 is designed to stay 100% backwards compatible with the test machine. A runtime guard also watches video memory in real time and trims background work when it gets too full. For example, a machine with 8 GB of RAM falls into Tier 1 and gets a VRAM texture pool between 1536 and 2048 MB.

**Reader:** people

## 2.6. Universal 4-tier APU classification engine (4-Tier APU Engine) and Safety Guards (`ApuCapabilityGuard`)

> `ApuCapabilityGuard` is the design name for these guards. No class or function has that name in the code; the classifier is `HardwareProfiler::BuildOptimizationPolicy()`.

To go beyond support for the Ryzen 5 4600H alone without introducing regressions or degrading newer silicon, VegaFlow includes a deterministic 4-level classifier (`ApuTier`) managed by `HardwareProfiler::BuildOptimizationPolicy()`:

1. **`Tier1_EntryUMA` [Survival Guard]:**
   - **Target hardware:** APUs with RAM $\le$ 8–9 GB or $\le$ 4 physical cores (basic Vega 3/6/8, Athlon Mobile, Intel Gen 9–11 UHD).
   - **Directives:** strict VRAM texture pool ceiling between 1536 and 2048 MB (`safe_vram_pool_mb`) to avoid out-of-memory (OOM) system crashes; 3 compile threads in DXVK; AMD tessellation limit at 8x; `disable_dx12_async_compute = true`; no STAPM frequency cap (`cpu_stapm_cap_mhz = 0`).
2. **`Tier2_MidVegaUMA` [Sweet-Spot Guard]:**
   - **Target hardware:** Vega/GCN5 APUs with 12–20 GB of RAM, or Zen 2/Zen 3 processors with 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - **Directives (100% backwards compatible with the test machine):** VRAM pool at 4096 MB; 6 dedicated compile threads in DXVK-GPLAsync; tessellation limit at 16x; `disable_dx12_async_compute = true`; no STAPM frequency cap (`cpu_stapm_cap_mhz = 0`).
3. **`Tier3_AdvancedUMA` [Do-No-Harm Guard]:**
   - **Target hardware:** modern RDNA2/RDNA3/RDNA3.5 APUs (Radeon 680M, 780M, 890M, Strix Point) and Intel Xe/Arc UMA, or UMA systems with $\ge$ 22 GB of RAM.
   - **Directives ("Do-No-Harm"):** keeps modern asynchronous compute intact (`D3D12.AsyncCompute=1` and `r.RDG.AsyncCompute=1`); **does not throttle the CPU** (`cpu_stapm_cap_mhz = 0`); assigns a generous, adaptive UMA pool of 6144 to 8192 MB; tessellation allowed up to 32x; 6 to 8 threads in DXVK.
4. **`Tier4_DiscreteOrCustom` [Pass-Through Guard]:**
   - **Target hardware:** dedicated GPUs (more than 2 GB of dedicated VRAM) or discrete architectures.
   - **Directives:** transparent pass-through mode, with no forced UMA overrides and no integrated-silicon limits.

### Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):
It watches in real time the actual use of the local video memory budget (`local_usage_bytes / local_budget_bytes`) through DXGI 1.4 (`IDXGIAdapter3`). If budget use exceeds **85%**, or the free physical memory drops below the critical threshold of **1536 MB**, it activates the defensive guard in a non-invasive way: it logs the intervention and trims the working set of background processes to prevent paging stalls to disk or abrupt OOM closures.
