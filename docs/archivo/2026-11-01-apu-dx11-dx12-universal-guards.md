# Universal APU DX11/DX12 Optimization & Adaptive Safety Guards (`ApuCapabilityGuard`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded Ryzen 5 4600H / Vega 6 constants across DirectX 11, DirectX 12, UMA memory, and power profiles with a deterministic 4-Tier APU classification and adaptive safety guard system (`ApuCapabilityGuard`), while preserving 100% compatibility and performance on the primary Ryzen 5 4600H testbed (`Tier2_MidVegaUMA`).

**Architecture:** `HardwareProfiler` detects the GPU architecture generation (`GpuArchitectureGen`) and classifies the system into one of four deterministic tiers (`ApuTier`), producing a single source-of-truth contract (`ApuOptimizationPolicy`) via `HardwareProfiler::BuildOptimizationPolicy()`. `DxvkManager`, `SteamEngineOptimizer`, `GraphicsOptimizer`, `MemoryManager`, and `SystemInfo` consume this policy to apply safe DX11/DX12 settings, enforce a "Do-No-Harm" guard on advanced RDNA2/RDNA3/Xe APUs, protect entry 8GB APUs from OOM crashes, and monitor runtime DXGI UMA budget pressure (`MemoryManager::EvaluateUmaPressureGuard`).

**Tech Stack:** C++20, Win32 API, NTDLL, DXGI 1.4 (`IDXGIAdapter3`), MinGW-w64 (`g++ -std=c++20 -O2 -Wall -Wextra -static`).

**Spec:** `.claude/docs/ARQUITECTURA/spec-guards-apu-dx11-dx12.md`

## Global Constraints

- **C++20 Standard & Zero External Bloat:** Maintain `< 15 MB` private working set (`MemoryManager::GetCurrentProcessPrivateWorkingSetBytes()`).
- **100% Backward Compatibility on Ryzen 5 4600H (`Tier2_MidVegaUMA`):** All 28 existing unit tests in `tests/test_main.cpp` must continue to pass without regression (`safe_vram_pool_mb = 4096`, `dxvk_compiler_threads = 6`, `amd_tessellation_cap = 16`, `cpu_stapm_cap_mhz = 3400`, `disable_dx12_async_compute = true`).
- **Dry-Run Support & SandboxGuard Safety:** Every state-modifying method must respect `dry_run` and `SandboxGuard` anti-cheat lockdown.
- **Compiler Command (Windows MinGW):** Use `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests`.

---

### Task 1: Core `ApuTier`, `GpuArchitectureGen`, and `ApuOptimizationPolicy` in `HardwareProfiler` & `SystemInfo`

**Files:**
- Modify: `include/core/hardware_profiler.hpp`
- Modify: `src/core/hardware_profiler.cpp`
- Modify: `include/core/system_info.hpp`
- Modify: `src/core/system_info.cpp`

**Interfaces:**
- Consumes: `SystemHardwareProfile`, `GpuProfile`, `CpuTopologyProfile`
- Produces:
  - `enum class ApuTier : uint8_t { Tier1_EntryUMA = 1, Tier2_MidVegaUMA = 2, Tier3_AdvancedUMA = 3, Tier4_DiscreteOrCustom = 4 };`
  - `enum class GpuArchitectureGen : uint8_t { Unknown = 0, AMD_GCN5_Vega = 1, AMD_RDNA2 = 2, AMD_RDNA3_Plus = 3, Intel_Gen9_11_UHD = 4, Intel_Xe_Arc = 5, Discrete_GPU = 6 };`
  - `struct ApuOptimizationPolicy` in `include/core/hardware_profiler.hpp`
  - `static ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes = 0);`
  - `static const char* HardwareProfiler::ApuTierToString(ApuTier tier);`
  - `static const char* HardwareProfiler::ArchGenToString(GpuArchitectureGen gen);`
  - `ApuGuardInfo` struct in `include/core/system_info.hpp` and `"apu_guard"` JSON section in `SystemInfo::ToJson()`.

- [x] **Step 1: Update `include/core/hardware_profiler.hpp` with `ApuTier`, `GpuArchitectureGen`, and `ApuOptimizationPolicy`**

Add the enums and policy structure to `include/core/hardware_profiler.hpp`:

```cpp
enum class ApuTier : uint8_t {
    Tier1_EntryUMA = 1,        // <= 8-9GB RAM or <= 4 physical cores (Vega 3/6/8 entry, Athlon, Intel UHD)
    Tier2_MidVegaUMA = 2,      // 12-20GB RAM GCN5/Vega or Zen2/Zen3 6C-8C (Ryzen 5 4600H, 5500U, 5600H)
    Tier3_AdvancedUMA = 3,     // RDNA2/RDNA3/RDNA3.5 (680M, 780M, 890M), Intel Xe/Arc, or UMA >= 22GB RAM
    Tier4_DiscreteOrCustom = 4 // Dedicated GPU (> 2GB VRAM) or unknown non-UMA architecture
};

enum class GpuArchitectureGen : uint8_t {
    Unknown = 0,
    AMD_GCN5_Vega = 1,
    AMD_RDNA2 = 2,
    AMD_RDNA3_Plus = 3,
    Intel_Gen9_11_UHD = 4,
    Intel_Xe_Arc = 5,
    Discrete_GPU = 6
};

struct ApuOptimizationPolicy {
    ApuTier tier = ApuTier::Tier2_MidVegaUMA;
    GpuArchitectureGen arch_gen = GpuArchitectureGen::AMD_GCN5_Vega;
    std::string tier_label;
    std::string guard_mode_name;

    // UMA Memory Guard (DX11 & DX12)
    uint32_t os_reserve_floor_mb = 4096;
    uint32_t safe_vram_pool_mb = 4096;
    uint32_t safe_shared_pool_mb = 8192;
    double runtime_pressure_threshold = 0.85;

    // DirectX 11 Specialization
    uint32_t dxvk_compiler_threads = 6;
    uint32_t amd_tessellation_cap = 16;
    uint32_t max_anisotropy = 4;
    bool recommend_dxvk_gplasync = true;

    // DirectX 12 Specialization
    bool disable_dx12_async_compute = true;
    uint32_t ue_max_csm_resolution = 1024;
    uint32_t ue_max_shadow_cascades = 2;
    bool disable_volumetric_fog = true;

    // CPU STAPM & Power Guard
    uint32_t cpu_stapm_cap_mhz = 3400; // 0 = Uncapped (Do-No-Harm Guard for Tier 3/4)
    bool apply_uma_vram_reg_override = true;
};
```

Also add `GpuArchitectureGen arch_gen = GpuArchitectureGen::Unknown;` to `GpuProfile`, `ApuTier apu_tier = ApuTier::Tier2_MidVegaUMA;` to `SystemHardwareProfile`, and declare `BuildOptimizationPolicy`, `ApuTierToString`, and `ArchGenToString` on `HardwareProfiler`.

- [x] **Step 2: Implement deterministic classification and `BuildOptimizationPolicy` in `src/core/hardware_profiler.cpp`**

Implement `HardwareProfiler::ApuTierToString`, `HardwareProfiler::ArchGenToString`, architecture detection in `DetectHardware()`, and `HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile& profile, uint64_t live_dxgi_local_budget_bytes)`:
- For `Tier1_EntryUMA`: `guard_mode_name = "Survival Guard"`, `os_reserve_floor_mb = 3072`, `safe_vram_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 22) / 100), 1536u, 2048u)`, `safe_shared_pool_mb = 4096`, `dxvk_compiler_threads = std::clamp(cores > 1 ? cores - 1 : 1u, 1u, 3u)`, `amd_tessellation_cap = 8`, `max_anisotropy = 2`, `disable_dx12_async_compute = true`, `ue_max_csm_resolution = 512`, `ue_max_shadow_cascades = 1`, `disable_volumetric_fog = true`, `cpu_stapm_cap_mhz = 3000`, `apply_uma_vram_reg_override = true`.
- For `Tier2_MidVegaUMA`: `guard_mode_name = "Sweet-Spot Guard"`, `os_reserve_floor_mb = 4096`, `safe_vram_pool_mb = 4096`, `safe_shared_pool_mb = 8192`, `dxvk_compiler_threads = std::clamp(cores, 4u, 6u)`, `amd_tessellation_cap = 16`, `max_anisotropy = 4`, `disable_dx12_async_compute = true`, `ue_max_csm_resolution = 1024`, `ue_max_shadow_cascades = 2`, `disable_volumetric_fog = true`, `cpu_stapm_cap_mhz = 3400`, `apply_uma_vram_reg_override = true`.
- For `Tier3_AdvancedUMA`: `guard_mode_name = "Do-No-Harm Guard"`, `os_reserve_floor_mb = 4096`, `safe_vram_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 30) / 100), 4096u, 8192u)`, `safe_shared_pool_mb = std::clamp(static_cast<uint32_t>((total_ram_mb * 50) / 100), 8192u, 16384u)`, `dxvk_compiler_threads = std::clamp(cores > 2 ? cores - 2 : 4u, 4u, 8u)`, `amd_tessellation_cap = 32`, `max_anisotropy = 8`, `recommend_dxvk_gplasync = (arch_gen == GpuArchitectureGen::AMD_GCN5_Vega)`, `disable_dx12_async_compute = false`, `ue_max_csm_resolution = 2048`, `ue_max_shadow_cascades = 3`, `disable_volumetric_fog = false`, `cpu_stapm_cap_mhz = 0`, `apply_uma_vram_reg_override = true`.
- For `Tier4_DiscreteOrCustom`: `guard_mode_name = "Pass-Through Guard"`, `os_reserve_floor_mb = 4096`, `safe_vram_pool_mb = 4096`, `safe_shared_pool_mb = 8192`, `dxvk_compiler_threads = std::clamp(cores > 2 ? cores - 2 : 4u, 4u, 8u)`, `amd_tessellation_cap = 64`, `max_anisotropy = 16`, `recommend_dxvk_gplasync = false`, `disable_dx12_async_compute = false`, `ue_max_csm_resolution = 2048`, `ue_max_shadow_cascades = 4`, `disable_volumetric_fog = false`, `cpu_stapm_cap_mhz = 0`, `apply_uma_vram_reg_override = false`.
- If `live_dxgi_local_budget_bytes > 0` and `tier != ApuTier::Tier4_DiscreteOrCustom`: compute `uint32_t budget_80_pct_mb = static_cast<uint32_t>((live_dxgi_local_budget_bytes * 80ULL / 100ULL) / (1024ULL * 1024ULL))`. In `Tier1_EntryUMA` and `Tier3_AdvancedUMA`, clamp `safe_vram_pool_mb` so it does not exceed `budget_80_pct_mb` (with a floor of `1536` MB for Tier 1 and `4096` MB for Tier 3 if `total_ram_mb >= 15000`).

- [x] **Step 3: Integrate `ApuGuardInfo` into `include/core/system_info.hpp` and `src/core/system_info.cpp`**

Add `ApuGuardInfo` to `SystemInfoSnapshot` in `include/core/system_info.hpp` and populate it in `SystemInfo::Gather()` in `src/core/system_info.cpp`, serializing `"apu_guard"` in `SystemInfo::ToJson()`. Ensure `GatherGpu()` preserves `" (Vega 6 / GCN 5.0)"` when `arch_gen == GpuArchitectureGen::AMD_GCN5_Vega`, and uses `" (RDNA 2 UMA)"`, `" (RDNA 3/3.5 UMA)"`, or `" (Intel Xe/Arc UMA)"` for newer architectures.

---

### Task 2: Adaptive DX11/DX12 Specialization & Runtime UMA Pressure Guard (`DxvkManager`, `SteamEngineOptimizer`, `GraphicsOptimizer`, `MemoryManager`)

**Files:**
- Modify: `src/optimizers/dxvk_manager.cpp`
- Modify: `src/optimizers/steam_engine_optimizer.cpp`
- Modify: `src/optimizers/graphics_optimizer.cpp`
- Modify: `include/optimizers/memory_manager.hpp`
- Modify: `src/optimizers/memory_manager.cpp`

**Interfaces:**
- Consumes: `HardwareProfiler::DetectHardware()`, `HardwareProfiler::BuildOptimizationPolicy()`
- Produces:
  - `struct UmaGuardStatus` in `include/optimizers/memory_manager.hpp`
  - `UmaGuardStatus MemoryManager::EvaluateUmaPressureGuard(const SystemMemoryTelemetry& telem, const ApuOptimizationPolicy& policy, const std::vector<uint32_t>& background_pids = {}, bool dry_run = false);`
  - Tier-aware DXVK configuration in `DxvkManager::DeployDxvk` and `DxvkManager::ConfigureApuProfile`
  - Tier-aware DX12 `AsyncCompute` and UMA Pool guard in `SteamEngineOptimizer::ApplyEngineUmaPreset`
  - Tier-aware STAPM frequency ceiling (`cpu_stapm_cap_mhz`) and tessellation cap in `GraphicsOptimizer::SetApuPowerMode` and `GraphicsOptimizer::SetAmdDx11UmaDriverBoost`.

- [x] **Step 1: Update `DxvkManager` (`src/optimizers/dxvk_manager.cpp`) to use `ApuOptimizationPolicy`**

In `DxvkManager::DeployDxvk()` and `DxvkManager::ConfigureApuProfile()`, query `const auto policy = HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware());`:
- Use `std::to_string(policy.dxvk_compiler_threads)` for `dxvk.numCompilerThreads` (which evaluates to `"6"` on the 6-core Ryzen 5 4600H).
- Use `std::to_string(policy.safe_vram_pool_mb)` and `std::to_string(policy.safe_shared_pool_mb)` in `DeployDxvk()`.
- In `DxvkManager::ValidateDxvkApuProfile()`, validate `compilerThreads >= 1 && compilerThreads <= 16` (preserving `compilerThreads == 6` on the 6-core testbed).

- [x] **Step 2: Update `SteamEngineOptimizer::ApplyEngineUmaPreset` (`src/optimizers/steam_engine_optimizer.cpp`) with DX12 & UMA Guards**

In `SteamEngineOptimizer::ApplyEngineUmaPreset()`:
- Query `const auto hw = HardwareProfiler::DetectHardware();` and `const auto policy = HardwareProfiler::BuildOptimizationPolicy(hw);`.
- Determine `effective_pool_mb`:
  - If `policy.tier == ApuTier::Tier1_EntryUMA`, enforce the survival ceiling: `effective_pool_mb = (vram_pool_mb == 0) ? policy.safe_vram_pool_mb : std::min(vram_pool_mb, policy.safe_vram_pool_mb);`.
  - If `policy.tier == ApuTier::Tier2_MidVegaUMA`, keep `(vram_pool_mb == 0 || vram_pool_mb >= 3000) ? 4096 : vram_pool_mb`.
  - If `policy.tier == ApuTier::Tier3_AdvancedUMA`, use `(vram_pool_mb == 0 || vram_pool_mb == 4096) ? policy.safe_vram_pool_mb : vram_pool_mb`.
- Use `policy.ue_max_csm_resolution`, `policy.ue_max_shadow_cascades`, `policy.disable_volumetric_fog ? 0 : 1`, `policy.max_anisotropy`, and `policy.disable_dx12_async_compute ? 0 : 1` for `r.RDG.AsyncCompute` and `D3D12.AsyncCompute`.
- Add a helper overload or static method `SteamEngineOptimizer::GenerateUnrealUmaBlock(const ApuOptimizationPolicy& policy, uint32_t vram_pool_mb_override = 0)` so unit tests can directly inspect the generated DX11/DX12 Unreal Engine block for any synthetic `ApuOptimizationPolicy` tier!

- [x] **Step 3: Update `GraphicsOptimizer` (`src/optimizers/graphics_optimizer.cpp`) for Tier-aware STAPM and Tessellation Guards**

- In `GraphicsOptimizer::SetApuPowerMode()`: when `mode == ApuPowerMode::GpuPrioritySustained`, query `const auto policy = HardwareProfiler::BuildOptimizationPolicy(HardwareProfiler::DetectHardware());` and set `cpu_max_freq = policy.cpu_stapm_cap_mhz;` (`3400` on Tier 2, `3000` on Tier 1, `0` uncapped on Tier 3/4).
- In `GraphicsOptimizer::SetAmdDx11UmaDriverBoost()`: query `policy.amd_tessellation_cap` (clamped to `16` on Tier 2, `8` on Tier 1, `32` on Tier 3) when `enable == true`.

- [x] **Step 4: Add `EvaluateUmaPressureGuard` to `include/optimizers/memory_manager.hpp` and `src/optimizers/memory_manager.cpp`**

In `include/optimizers/memory_manager.hpp`, include `"hardware_profiler.hpp"` and add:

```cpp
struct UmaGuardStatus {
    bool guard_triggered = false;
    double dxgi_budget_utilization = 0.0;
    uint64_t available_os_headroom_mb = 0;
    uint32_t total_interventions = 0;
    std::string active_reason;
};
```

And in `MemoryManager`:
```cpp
UmaGuardStatus EvaluateUmaPressureGuard(
    const SystemMemoryTelemetry& telem,
    const ApuOptimizationPolicy& policy,
    const std::vector<uint32_t>& background_pids = {},
    bool dry_run = false);
```

Implement `EvaluateUmaPressureGuard` in `src/optimizers/memory_manager.cpp`:
- Compute `available_os_headroom_mb = (telem.free_and_zero_bytes + telem.standby_bytes) / (1024ULL * 1024ULL)`.
- Compute `dxgi_budget_utilization = (telem.vram.valid && telem.vram.local_budget_bytes > 0) ? (static_cast<double>(telem.vram.local_usage_bytes) / static_cast<double>(telem.vram.local_budget_bytes)) : 0.0`.
- Trigger guard (`guard_triggered = true`) if `policy.tier != ApuTier::Tier4_DiscreteOrCustom` and (`dxgi_budget_utilization >= policy.runtime_pressure_threshold` or `(telem.free_and_zero_bytes / (1024ULL * 1024ULL)) < 1536ULL`).
- When triggered, increment `guard_interventions_count_`, set descriptive `active_reason`, and if `!dry_run`, trim working sets of `background_pids`.

---

### Task 3: Unit Test Suite (`Test 29: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards`) & Full Verification

**Files:**
- Modify: `tests/test_main.cpp`
- Modify: `CLAUDE.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: `HardwareProfiler::BuildOptimizationPolicy`, `SteamEngineOptimizer::GenerateUnrealUmaBlock`, `MemoryManager::EvaluateUmaPressureGuard`, `SystemInfo::ToJson`
- Produces: 29 passing tests in `build/vegaflow_tests.exe` and updated documentation.

- [x] **Step 1: Add Test 29 in `tests/test_main.cpp`**

Add `Test 29: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards` to `tests/test_main.cpp` verifying:
1. **Entry APU Profile (`Tier1_EntryUMA`):** Synthetic 4C/8T, 8 GB RAM, `AMD_GCN5_Vega` -> verifies `tier == ApuTier::Tier1_EntryUMA`, `safe_vram_pool_mb <= 2048`, `dxvk_compiler_threads <= 3`, `amd_tessellation_cap == 8`, `disable_dx12_async_compute == true`, `cpu_stapm_cap_mhz == 3000`.
2. **Mid Vega APU Profile (`Tier2_MidVegaUMA` - Ryzen 5 4600H):** Synthetic 6C/12T, 16 GB RAM, `AMD_GCN5_Vega` -> verifies `tier == ApuTier::Tier2_MidVegaUMA`, `safe_vram_pool_mb == 4096`, `dxvk_compiler_threads == 6`, `amd_tessellation_cap == 16`, `disable_dx12_async_compute == true`, `cpu_stapm_cap_mhz == 3400`.
3. **Advanced RDNA3 APU Profile (`Tier3_AdvancedUMA` - Radeon 780M/890M 32GB):** Synthetic 8C/16T, 32 GB RAM, `AMD_RDNA3_Plus` -> verifies `tier == ApuTier::Tier3_AdvancedUMA`, **Do-No-Harm Guard activo** (`disable_dx12_async_compute == false`, `cpu_stapm_cap_mhz == 0`, `safe_vram_pool_mb >= 6144`, `amd_tessellation_cap == 32`), and verifies `SteamEngineOptimizer::GenerateUnrealUmaBlock(adv_policy)` emits `D3D12.AsyncCompute=1` and `r.RDG.AsyncCompute=1`.
4. **Runtime DXGI Pressure Guard (`EvaluateUmaPressureGuard`):** Synthetic `SystemMemoryTelemetry` with 90% DXGI local budget utilization (`3686 MB / 4096 MB`) in `dry_run = true` -> verifies `guard_triggered == true` and `dxgi_budget_utilization >= 0.85`, while 50% utilization keeps `guard_triggered == false`.
5. **JSON Telemetry Integration:** Verifies `SystemInfo().ToJson()` includes `"apu_guard"` and `"guard_mode"`.

- [x] **Step 2: Compile `build/vegaflow_tests.exe` and `build/vegaflow_daemon.exe` and verify 29/29 PASS**

Run with PowerShell build script:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```
Verifies `TEST RESULTS: 29 PASSED, 0 FAILED`.
And test `.\build\vegaflow_daemon.exe --system-info` and `.\build\vegaflow_daemon.exe --status`.
