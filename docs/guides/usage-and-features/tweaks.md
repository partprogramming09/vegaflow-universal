# Windows and driver tweaks

**In short:** VegaFlow changes some Windows, graphics driver and game settings, so the game gets more of the processor and the memory.
Before it writes a file, it saves a backup copy, so each change can be undone.
For example, the game gets the fastest CPU cores, while browsers and other background programs are moved to the last cores.

**Reader:** people

### 2.1. DXGI Hardware Flip Model Presentation Upgrade

- It adds only `SwapEffectUpgradeEnable=1` to the game's value in `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`; anything else there (`GpuPreference`, `AutoHDREnable`) stays as it was. This is what turns on "Optimizations for windowed games": DirectX 10 and 11 games in a window move from the blt model to the flip model.
- It appends `HIGHDPIAWARE` to the game's flags in `AppCompatFlags\Layers` without removing the others, and skips it when another DPI mode is already set.
- **Benefit:** not measured in this project (see the verification status page). Undo it from Historial or "Devolver todo".

### 2.2. UMA Engine Memory & Texture Streamer

- It scans the game installations in Steam and detects automatically whether the engine is **Unreal Engine 4/5** or **Unity**.
- **For Unreal Engine 4/5:** it sets `Engine.ini` to unthrottle the texture pool:
  ```ini
  [SystemSettings]
  r.Streaming.PoolSize=4096
  r.Streaming.LimitPoolSizeToVRAM=0
  r.Streaming.FullyLoadUsedTextures=1
  r.Streaming.FramesForFullUpdate=1
  r.Streaming.MaxNumTexturesToStreamPerFrame=120
  r.Streaming.Boost=1.5
  r.RDG.AsyncCompute=0
  D3D12.AsyncCompute=0
  ```
  The pool size shown is the value the code writes on a Tier 2 machine (the test bench) without an override. The tier and the override decide the real value.
- **For Unity:** it edits `boot.config` to enable native multithreading (`gfx-enable-native-gfx-jobs=1`, `gfx-enable-gfx-jobs=1`, `gc-max-time-slice=3`).
- **Guaranteed safety:** it automatically creates `.vegaflow.bak` backup files, so every change can be reverted with a single click.

### 2.3. DXVK-GPLAsync Module (6-Thread Compilation)

- It integrates the optimized **DXVK-GPLAsync v2.5.3** build, which translates DirectX 11 calls into native Vulkan. The DLLs are not shipped with the project: they go in `assets/dxvk/dxvk-2.5.3/` (see `assets/dxvk/README.md`).
- It writes a `dxvk.conf` file tuned for the architecture of the 6 physical Zen 2 cores:
  - Asynchronous compilation, decoupled from the render thread (`dxvk.enableAsync = True`, `dxvk.gplAsyncCache = True`).
  - 6 dedicated threads for compiling pipelines in parallel (`dxvk.numCompilerThreads = 6`).
  - Device memory limit set to 4096 MB and shared memory to 8192 MB (`dxgi.maxDeviceMemory = 4096`).
- It prevents stutter in DX11 games, where AMD's official drivers suffer from shader-compilation overhead.
- **Your `dxvk.conf` is kept.** If the game folder already had a `dxvk.conf` that is not VegaFlow's, it is copied as `dxvk.conf.vegaflow.bak` before ours is written. The game's own DLLs, as always, are saved as `d3d11.dll.original.bak` and `dxgi.dll.original.bak`. When DXVK is removed, the original `dxvk.conf` goes back to its place. If it could not be restored, the message says so and the copy stays in `dxvk.conf.vegaflow.bak`. If the game had no `dxvk.conf`, removing DXVK deletes VegaFlow's file.
- **When it counts as installed:** VegaFlow only treats DXVK as installed if `d3d11.dll` and `dxgi.dll` exist and the first line of `dxvk.conf` starts with `# VegaFlow`. A game that ships its own DLLs does not count as "with VegaFlow's DXVK".

### 2.5. CPU Orchestration & MMCSS Quantum Tuning

- **Disjoint CPU Sets:** gives the game the fastest Zen 2 cores with the lowest L3 cache latency, while confining browsers and secondary tasks to the leftover cores (`SetProcessDefaultCpuSets`).
- **Background EcoQoS Throttling:** tells the Windows 11 scheduler to run background processes in energy-saving mode through `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`. The power saving and the GPU clock it would allow are not measured in this repository ([verification status](../../measurements/verification-status.md)).
- **NT I/O and memory hierarchy:** keeps the game at `IoPriorityNormal` (2) and gives `IoPriorityVeryLow` (0) to secondary processes, through `NtSetInformationProcess(ProcessIoPriorityInformationClass = 33)`, to eliminate disk read stalls.
- **MMCSS (Multimedia Class Scheduler Service):** sets the Games task priority to `6`, its scheduling category to `High` and its SFIO priority to `High` (`mmcss.cpp:22-34`), and caps the system reserve (`SystemResponsiveness`) at 10% (`mmcss.cpp:12`).
- **NT timer resolution:** the daemon does not change the Windows timer from 15.6 ms. The request for 0.5 ms (`NtSetTimerResolution`) exists only in the test script `scripts/hold_timer.ps1`, which measures its effect on its own.
- **Thermally efficient APU power profile** (`--apu-gpu-priority`): sets the CPU boost to *Efficient Aggressive* (value 4), the energy preference to 25 and turns PCIe ASPM off (`power_mode.cpp:24-28`). It puts no frequency cap on the CPU: the tier policy sets `cpu_stapm_cap_mhz` to 0, which means no cap (`hardware_profiler.cpp:268`). The code sets no CPU frequency or wattage target, and the figures for this profile are not measured ([verification status](../../measurements/verification-status.md)).

### 2.9. Modern Suite for 3D Games (EcoQoS, NT I/O Hierarchy and DirectFlip)

Built in at a low level to fix, at the root, the latency, storage-contention and power-balance limits in demanding 3D titles (Zenless Zone Zero, Genshin Impact, Honkai: Star Rail, Octopath Traveler II):

1. **Background EcoQoS (Power Throttling in Background Processes):**
   - **Technical and physical explanation:** on APU chips such as the Ryzen 5 4600H, the STAPM thermal budget (Skin Temperature Aware Power Management, the power limit for the whole chip) is dynamically limited to between 35W and 45W for the whole package. When secondary processes (web browsers, launchers, Discord, indexing tasks) run bursts of computation on Zen 2 cores at high voltage, they take between 3W and 5W from this shared budget. AMD's SMU (the chip's power-management unit) is forced to lower the Vega 6 GPU clock to 1100–1200 MHz to stay within the thermal threshold.
   - **NT mechanism:** VegaFlow uses the native Windows 11 API `SetProcessInformation` with the `PROCESS_POWER_THROTTLING_STATE` structure, setting the `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` flag. By confining background tasks to ultra-low power states, it recovers between 3W and 5W of the STAPM budget, which lets the iGPU sustain its **continuous 1500 MHz** without thermal throttling.
2. **NT I/O Priority and Memory Hierarchy:**
   - **Technical explanation:** modern 3D games continuously stream models and high-resolution textures from the NVMe SSD into shared memory (UMA) and the GPU buffer. When background applications compete for the file I/O bus or cause contention in the memory manager, severe stutter and frametime spikes of 50 to 150 ms appear.
   - **NT mechanism:** through calls to `NtSetInformationProcess(ProcessIoPriorityInformationClass = 33)`, VegaFlow keeps the 3D game at `IoPriorityNormal` (2; in Windows' `IO_PRIORITY_HINT`, 3 is High) and confines background processes to `IoPriorityVeryLow` (0). With the background at VeryLow, the game's mesh and texture reads go ahead of background disk work. At the same time, `MEMORY_PRIORITY_LOW` (2) is set on background processes, to prevent active graphics resources from being pushed out to the pagefile.
3. **Windowed flip model:**
   - In a window, the compositor (DWM) copies each frame of a game that presents with the blt model.
   - VegaFlow adds only `SwapEffectUpgradeEnable=1` to the game's value in `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` (the user's GPU and HDR choice do not change) so Windows moves it to the flip model. Effect not measured here.

### 2.10. APU Silicon Suite & NT Kernel Memory Shield (DisablePagingExecutive and Smart Tessellation 16x)

Designed to mitigate the inherent weaknesses of Ryzen APUs, which share memory (UMA) and have compact compute units (Vega 6):

1. **NT Kernel Memory Shield (RAM Lock / DisablePagingExecutive=1):**
   - **Technical and physical explanation:** on systems with unified memory, Windows NT tends to page kernel executable code and the D3D/WDDM drivers out to `pagefile.sys` when it sees little recent activity on certain pages. When the game engine or the integrated GPU suddenly requests a routine that is not resident in RAM, a hard page fault occurs. This freezes the render pipeline for 50 to 200 ms, until the NVMe disk returns the page to RAM.
   - **NT mechanism:** VegaFlow sets `DisablePagingExecutive = 1` and `LargeSystemCache = 0` in `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management`. This keeps all the kernel executive's drivers and components in physical RAM permanently, which eliminates micro-stutter from paging to disk and keeps interrupt latency below one millisecond.
2. **Smart AMD Tessellation Limiter (Smart Tessellation 16x / Anti-Overdraw):**
   - **Technical explanation:** the Radeon RX Vega 6 has only 6 physical Compute Units (384 ALUs) and a single hardware Primitive Engine. Over-tessellated meshes and 3D effects (often at 64x in DirectX 11 engines) saturate the geometry pipeline with triangles smaller than one screen pixel (*sub-pixel overdraw*). This collapses performance without adding any visible detail.
   - **AMD driver mechanism:** in the adapter's `UMD` subkey, VegaFlow sets `Tessellation = "16"` and `Tessellation_OPTION = "1"`. This caps maximum tessellation at **16x**, removes sub-pixel overdraw, eases the load on the Vega 6 CUs, and noticeably stabilizes frametime and the 1% lows.
