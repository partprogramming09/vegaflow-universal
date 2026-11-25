# Design record: integration and tests

**In short:** This second part of the design record, dated 2026-11-01, lists the changes made to each existing module (the hardware profiler, the DXVK manager, the engine presets, the graphics settings and the memory guard). It also lists the checks that must pass before the change is accepted. For example, on the test machine (Ryzen 5 4600H, 16 GB RAM, Vega 6) the classifier must still choose Tier 2 and keep the same values as before.

**Reader:** people

Some names in this page were updated to match the code. Each correction is marked with a note.

## 4. Integration into the existing modules

### 4.1. `HardwareProfiler` and `SystemInfo`
- In [`src/model/hardware.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/model/hardware.hpp) and [`src/detect/hardware_profiler.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/detect/hardware_profiler.cpp):
  - Add `ApuTier`, `GpuArchitectureGen` and `ApuOptimizationPolicy`.
  - Add the `arch_gen` and `apu_tier` fields to `SystemHardwareProfile`.
  - Implement `HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0)` and the helper functions `ApuTierToString` and `ArchGenToString`.
- In [`src/session/system_info.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/system_info.cpp) and [`src/session/system_info.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/system_info.hpp):
  - In `GatherGpu()`: replace the fixed suffix `" (Vega 6 / GCN 5.0)"` with the real suffix according to `arch_gen`. The design kept `" (Vega 6 / GCN 5.0)"` for a Renoir 4600H / GCN5, to keep full compatibility with existing reports, and showed `" (RDNA 2 UMA)"`, `" (RDNA 3/3.5 UMA)"` or `" (Intel Xe/Arc UMA)"` on other machines.
    > Note: the current code writes `" (Vega / GCN 5.0)"` for every GCN 5 machine (`SystemInfo::GatherGpu` in `src/session/system_info.cpp`). The `(Vega 6 / GCN 5.0)` string is no longer produced.
  - In `GatherCpu()`: adjust `boost_target_ghz` and `stapm_target` according to the policy (`boost_target_ghz` and the `stapm_target` string in `src/session/system_info.cpp`: Tier 1 `3.5 GHz` with `"Smart Uncapped Boost"`; Tier 2 `4.0 GHz` with `"4.0 GHz Smart Uncapped"`; Tiers 3 and 4 `4.4 GHz` with `"Dynamic Boost (Efficient Aggressive / Uncapped Guard)"`).
  - Export in `SystemInfo::ToJson()` an additional `"apu_guard"` block with `tier`, `guard_mode`, `safe_vram_pool_mb`, `dx12_async_compute_allowed`, `dxvk_compiler_threads` and `tessellation_cap`.

### 4.2. `DxvkManager` (DirectX 11 -> Vulkan specialization)
- In [`src/optimize/dxvk_manager.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/optimize/dxvk_manager.cpp):
  - `ConfigureApuProfile()` receives the policy already calculated (`const ApuOptimizationPolicy&`; since phase R2 they do not detect the hardware on each call: the daemon builds it once at startup).
  - Write `dxvk.numCompilerThreads`, `dxgi.maxDeviceMemory` and `dxgi.maxSharedMemory` using the policy values (`policy.dxvk_compiler_threads`, `policy.safe_vram_pool_mb`, `policy.safe_shared_pool_mb`).
  - In `ValidateDxvkApuProfile()`: accept as valid any `compilerThreads` between `1` and `16` (on the Ryzen 5 4600H it stays `6`, passing the DXVK profile check in `tests/integration/dxvk_tests.cpp` unchanged).
    > Note: the design said the value must match the system policy. The current check only tests the range `1` to `16` (`DxvkManager::ValidateDxvkApuProfile` in `src/optimize/dxvk_manager.cpp`); it does not compare the value with the policy.

### 4.3. `SteamEngineOptimizer` (DirectX 11 and DirectX 12 specialization in engines)
- In [`src/optimize/steam/`](https://github.com/partprogramming09/vegaflow-universal/tree/main/src/optimize/steam):
  - In `ApplyEngineUmaPreset()`:
    - Read the `ApuOptimizationPolicy`.
    - If `vram_pool_mb == 0`, use `policy.safe_vram_pool_mb`. If the user or caller passes an explicit `vram_pool_mb`, cap it with the guard when we are in `Tier1_EntryUMA`, so that it never exceeds the OS reserve.
    - Adjust `r.Shadow.MaxResolution`, `r.Shadow.MaxCSMResolution`, `r.Shadow.CSM.MaxCascades`, `r.VolumetricFog` and `r.MaxAnisotropy` according to the tier.
    - **DirectX 12 guard:** if `policy.disable_dx12_async_compute == true` (Tier 1 and Tier 2), write `r.RDG.AsyncCompute=0` and `D3D12.AsyncCompute=0`. If it is `false` (**Tier 3 Advanced APU Guard**), write `r.RDG.AsyncCompute=1` and `D3D12.AsyncCompute=1`, so that RDNA2/RDNA3 engines can use their asynchronous compute queues in DX12.

### 4.4. `GraphicsOptimizer` (driver limits, tessellation and STAPM)
- In [`src/optimize/graphics/`](https://github.com/partprogramming09/vegaflow-universal/tree/main/src/optimize/graphics):
  - In `SetApuPowerMode(ApuPowerMode::GpuPrioritySustained)`:
    - Read `policy.cpu_stapm_cap_mhz`.
    - Copy `cpu_max_freq = policy.cpu_stapm_cap_mhz`. Today it is `0` in every tier, that is, no ceiling (as `hardware.hpp` comments: *Uncapped*). The code does not distinguish tiers at this point.
    - Set `boost_val = 4` (*Efficient Aggressive*), `aspm_val = 0` and `epp_val = 25` in this mode.
  - In `SetDx11UmaDriverBoost()`:
    > Note: the design named this `SetAmdDx11UmaDriverBoost()`. The code method is `AmdDriverTweaks::SetDx11UmaDriverBoost()` in `src/optimize/graphics/amd_driver_tweaks.cpp`.
    - Use `policy.amd_tessellation_cap` (`8`, `16` or `32`) instead of forcing `16` for every generation.

### 4.5. Real-time guard (`MemoryManager::EvaluateUmaPressureGuard`)
- In [`src/optimize/memory_manager.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/optimize/memory_manager.hpp) and [`src/optimize/memory_manager.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/optimize/memory_manager.cpp):
  - Add the `UmaGuardStatus` struct:
    ```cpp
    struct UmaGuardStatus {
        bool guard_triggered = false;
        double dxgi_budget_utilization = 0.0; // 0.0 a 1.0+
        uint64_t available_os_headroom_mb = 0;
        uint32_t total_interventions = 0;
        std::string active_reason;
    };
    ```
  - Add the method `EvaluateUmaPressureGuard(const SystemMemoryTelemetry& telem, const ApuOptimizationPolicy& policy, const std::vector<uint32_t>& background_pids = {}, bool dry_run = false)`.
    > Note: the design sketch had no `background_pids` parameter. The current signature has it (`MemoryManager::EvaluateUmaPressureGuard` in `src/optimize/memory_manager.hpp`).
    - Compute the ratio `local_usage_bytes / local_budget_bytes` and the available RAM cushion (`free_and_zero_bytes + standby_bytes`).
    - If DXGI budget use exceeds `policy.runtime_pressure_threshold` (`85%`), or the real free RAM falls below the critical threshold (`1536 MB`) while a game is running:
      - Set `guard_triggered = true`, increment `total_interventions` and run a non-invasive mitigation (trimming the working set of background processes, or a controlled purge if the disk has low I/O).

---

## 5. Acceptance criteria and test plan

1. **Full backward compatibility on the Ryzen 5 4600H (Tier 2):**
   - The 28 existing tests in `build/vegaflow_tests.exe`, plus the new Test 29, must pass (`29/29 PASS`).
   - On the current test machine (Ryzen 5 4600H, 16 GB RAM, Vega 6), the classifier must select `Tier2_MidVegaUMA`, keeping `safe_vram_pool_mb = 4096`, `dxvk_compiler_threads = 6`, `amd_tessellation_cap = 16`, `cpu_stapm_cap_mhz = 0` and `disable_dx12_async_compute = true`.
2. **New Test 29 (planned name `Test 29: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards`):**
   > Note: in the code this test is numbered `Test 25` (`Test25` in `tests/integration/hardware_tests.cpp`), with the same name.
   - Check synthetic hardware profiles for:
     - **Entry APU (4C/8T, 8 GB RAM, Vega 8):** checks `Tier1_EntryUMA`, `safe_vram_pool_mb <= 2048`, `dxvk_compiler_threads <= 3`, `disable_dx12_async_compute == true`, `amd_tessellation_cap == 8`.
     - **Mid APU (6C/12T, 16 GB RAM, Vega 6 Renoir):** checks `Tier2_MidVegaUMA`, `safe_vram_pool_mb == 4096`, `dxvk_compiler_threads == 6`, `cpu_stapm_cap_mhz == 0`.
     - **Advanced APU (8C/16T, 32 GB RAM, Radeon 780M / 890M RDNA3):** checks `Tier3_AdvancedUMA`, **Do-No-Harm Guard active** (`disable_dx12_async_compute == false`, `cpu_stapm_cap_mhz == 0`, `safe_vram_pool_mb >= 6144`, `amd_tessellation_cap == 32`).
     - **Runtime Pressure Guard:** simulate a `SystemMemoryTelemetry` state with `90%` DXGI budget use in `dry_run = true`, and check that `EvaluateUmaPressureGuard` sets `guard_triggered = true` without blocking the thread.
3. **Daemon memory budget:**
   - Private RAM use (`Private Working Set`) must stay strictly `< 15 MB`.
