# Anti-cheat, file sandbox and hardware tiers

**In short:** VegaFlow checks whether a game has anti-cheat that runs at kernel level (the deepest layer of Windows).
If it does, the program changes nothing inside that game's folder and applies only system-wide tweaks.
It also blocks any write, copy or delete outside the paths it is allowed to touch.
For example, when BattlEye (`beservice.exe`) is detected, VegaFlow does not inject DLLs or edit the game's files.

**Reader:** people

### 2.4. Anti-Cheat Sentinel (Proactive Kernel Protection)

- It identifies anti-cheat technologies that run at kernel level by the signatures of their binaries and services:
  - **HoYoverse Protect** (`HoYoKProtect.sys`, `mhypbase.dll`, `mhyprot.sys`)
  - **Riot Vanguard** (`vgk.sys`)
  - **EasyAntiCheat** (`EasyAntiCheat_EOS.dll`)
  - **BattlEye** (`beservice.exe`)
  - **AntiCheatExpert** (`randgrid.sys`)
- If a protected game is detected, **VegaFlow activates the security lock:**
  - No DLL injection.
  - No changes to configuration files inside the game folder.
  - No calls to invasive process APIs (`PROCESS_VM_WRITE`).
  - It applies only safe operating-system-level optimizations (the Flip Model in the registry, cooperative CPU Sets and a high-precision timer).

### 2.6. SandboxGuard Subsystem (File Isolation & Anti-Cheat Quarantine)

The [`SandboxGuard`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/sandbox_guard.hpp) subsystem implements a strict file-isolation design with a zero-trust model (**Zero-Trust File Access**):

- **Anti-fraud path normalization:** neutralizes *path traversal* attacks and overflows (`..`, `.`, double backslashes and symlinks). It uses the Win32 API `GetFullPathNameW` to make the path absolute and removes the `.` and `..` parts. It then replaces the deepest part of the path that already exists with its real location, which resolves symbolic links and junctions before any access check.
- **Strict whitelist vs. inviolable blacklist:**
  - *Blacklist (highest priority):* an absolute ban on writes to drive roots (`C:\`, `D:\`) and their direct child files (`C:\bootmgr`), to paging files (`pagefile.sys`, `hiberfil.sys`, `swapfile.sys`), to operating-system core folders (`C:\Windows`, `System32`, `SysWOW64`, `WinSxS`), to `Program Files\Common Files`, and to `System Volume Information`.
  - *Strict whitelist:* only paths inside the VegaFlow root, recognized Steam libraries (`steamapps/common/...`), `%LOCALAPPDATA%` profiles, the system's safe temporary folder, and saved games in Documents are allowed.
- **Quarantine and unconditional anti-cheat lockdown:**
  - When kernel-level anti-cheat technologies are present (**Zenless Zone Zero**, **Genshin Impact**, **Honkai: Star Rail**, **EasyAntiCheat**, **BattlEye**, **Riot Vanguard**, **AntiCheatExpert**), the system activates the **Anti-Cheat Lockdown**. Any write, copy or delete in the game's folder or its subfolders is strictly forbidden.
- **Safe I/O wrappers:** every change to disk goes through safe wrappers (`SafeWriteFile`, `SafeCopyFile`, `SafeRemoveFile`, `SafeReadFile`), which block any risky I/O before native calls are made.
- **Real-time forensic audit:** every denied access is kept in memory with a millisecond timestamp (`timestamp_ms`), a violation code (`SandboxDenialReason`) and a descriptive message for diagnosis.

### 2.11. ApuCapabilityGuard: Universal 4-Level Classification and Safety Guards

To keep compatibility across the AMD and Intel processor range without applying harmful generic recipes, VegaFlow introduces the `ApuCapabilityGuard` subsystem (`HardwareProfiler::BuildOptimizationPolicy`):

1. **Tier 1: Entry UMA [Survival Guard]:**
   - **Target:** entry-level APUs with ≤ 9 GB of RAM or ≤ 4 physical cores (basic Vega 3/6/8, Athlon Mobile, Intel Gen 9–11 UHD).
   - **Defense:** strictly limits the Unreal Engine texture pool to between 1536 and 2048 MB, to avoid catastrophic out-of-memory (OOM) crashes; caps DXVK at 3 shader-compilation threads; sets tessellation to 8x; disables `AsyncCompute` in DX12; no STAPM frequency cap (`cpu_stapm_cap_mhz = 0`).
2. **Tier 2: Mid Vega UMA [Sweet-Spot Guard]:**
   - **Target:** GCN 5 / Vega APUs with 12–20 GB of RAM, or Zen 2/Zen 3 CPUs with 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - **Policies (100% backward compatible with the test bench):** VRAM pool at 4096 MB; 6 dedicated threads for DXVK; tessellation 16x; `disable_dx12_async_compute = true`; no STAPM frequency cap (`cpu_stapm_cap_mhz = 0`).
3. **Tier 3: Advanced UMA [Do-No-Harm Guard]:**
   - **Target:** modern RDNA 2, RDNA 3 and RDNA 3.5 APUs (Radeon 680M, 780M, 890M, Strix Point), Intel Xe/Arc UMA, or setups with ≥ 22 GB of RAM.
   - **Do-No-Harm Guard:** **keeps modern asynchronous compute intact** (`D3D12.AsyncCompute=1` and `r.RDG.AsyncCompute=1`); **does not throttle the CPU clock** (`cpu_stapm_cap_mhz = 0`); assigns a generous, adaptive UMA pool of between 4096 and 8192 MB, set to 30% of RAM; tessellation up to 32x.
4. **Tier 4: Discrete / Custom [Pass-Through Guard]:**
   - **Target:** dedicated GPUs (any GPU that is not integrated) or discrete architectures. It runs in transparent mode, without interfering with the dedicated driver stack.
5. **Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):**
   - It samples the real video-memory budget from DXGI 1.4 (`IDXGIAdapter3`). If local use reaches **85%**, or free physical memory drops below **1536 MB**, it immediately triggers a non-invasive intervention. This frees pages from secondary processes to avoid freezes or swapping to disk.
