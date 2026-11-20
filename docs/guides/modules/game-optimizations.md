# Modules: game optimizations

**In short:** these are the changes VegaFlow makes so a game runs better on an AMD APU: which background programs get less power, which get less disk priority, and which Windows settings change while a game is open. Each change is explained with its reason and the Windows mechanism behind it. For example, background programs are moved to a low-power mode (EcoQoS), which the page describes as freeing 3 W to 5 W for the integrated GPU. The figures here explain the mechanism; what was measured is on the [verification status](../../measurements/verification-status.md) page.

**Reader:** people

## 3.4. Modern suite for 3D games (EcoQoS, NT I/O hierarchy and DirectFlip)

Implemented in a coordinated way in [`src/optimize/cpu_orchestrator.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/optimize/cpu_orchestrator.cpp), [`src/optimize/graphics/`](https://github.com/partprogramming09/vegaflow-universal/tree/main/src/optimize/graphics), [`src/app/main.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp) and [`src/ui/gui_window.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp):

1. **Background EcoQoS (Power Throttling on background processes)**:
   - **Technical and physical explanation:** on APU processors such as the Ryzen 5 4600H, the STAPM (Skin Temperature Aware Power Management) thermal budget is dynamically limited between 35W and 45W for the whole package. When secondary processes (web browsers, launchers, Discord, indexing tasks) run compute bursts on Zen 2 cores at high voltages, they take between 3W and 5W from this shared budget. AMD's SMU is forced to reduce the Vega 6 GPU frequency to 1100–1200 MHz to stay within the thermal threshold.
   - **NT mechanism:** VegaFlow uses the `SetProcessInformation` API with the `PROCESS_POWER_THROTTLING_STATE` structure, setting the `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` flag:
     ```cpp
     PROCESS_POWER_THROTTLING_STATE powerThrottling{};
     powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
     powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
     powerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
     SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));
     ```
   - By confining background tasks to ultra-low power dissipation states, between 3W and 5W are recovered from the STAPM budget, letting the iGPU sustain its **continuous 1500 MHz** without thermal throttling or sudden framerate drops. When the game closes, `CpuOrchestrator::RestoreAll()` reverts the directives cleanly.

2. **I/O priority and NT memory hierarchy**:
   - **Technical explanation:** modern open-world games and 3D engines (such as Zenless Zone Zero, Genshin Impact and Octopath Traveler II) continuously stream 3D models and high-resolution textures from the NVMe SSD into RAM (UMA) and the GPU buffer. When background applications compete for the file system's bandwidth or cause memory trimming, severe stuttering and frametime spikes of 50 to 150 ms are produced.
   - **NT mechanism:** VegaFlow applies a strict hierarchy by invoking the native kernel call:
     ```cpp
     PFN_NtSetInformationProcess pfnNtSetInformationProcess = 
         (PFN_NtSetInformationProcess)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess");
     
     // 1. 3D game process: IoPriorityHigh (2) and MemoryPriorityNormal (5)
     ULONG io_high = 2; // IoPriorityHigh
     pfnNtSetInformationProcess(hGame, ProcessIoPriorityInformationClass /* 33 */, &io_high, sizeof(io_high));
     
     // 2. Background processes: IoPriorityVeryLow (0) and MemoryPriorityLow (2)
     ULONG io_very_low = 0; // IoPriorityVeryLow
     pfnNtSetInformationProcess(hBg, ProcessIoPriorityInformationClass /* 33 */, &io_very_low, sizeof(io_very_low));
     ```
   - This hierarchy ensures that the game's read requests have absolute priority in the Windows NT I/O queue, while background processes only access storage during idle cycles (`IoPriorityVeryLow = 0`). In addition, `MEMORY_PRIORITY_LOW` (2) in the background keeps the game's working memory from being paged out to the paging file, which removes micro-stutters in 3D texture streaming.

3. **DirectFlip & AutoFlip Model upgrade**:
   - **Technical explanation:** in borderless windowed modes, the Desktop Window Manager (DWM) intercepts the swap chain and makes an intermediate copy of each frame on a DWM composition surface, adding between 15 and 30 ms of input lag and stutter from vertical-sync contention.
   - **DXGI injection:** VegaFlow writes directly into `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` the value below. The code (`src/optimize/graphics/dxgi_flip.cpp`) merges these parameters into whatever value is already there:
     ```ini
     SwapEffectUpgradeEnable=1;GpuPreference=2;AutoHDREnable=0;AutoFlipModel=1;DirectFlip=1;
     ```
   - `TurboSync` is not part of this value. The AMD driver tweak sets it in the driver's `UMD` subkey (`src/optimize/graphics/amd_driver_tweaks.cpp`; see section 3.5 below).
   - This enables direct promotion to **DirectFlip** on Windows 11, decoupling the swap chain from DWM's copy queue. The game engine projects its frames straight to the video controller, reaching **0 ms of extra input latency** (equivalent to exclusive full screen) while keeping the convenience of borderless multitasking and compatibility with system overlays.

## 3.5. APU silicon suite & NT kernel memory shielding (DisablePagingExecutive & Smart Tessellation 16x)

Implemented in [`src/optimize/graphics/`](https://github.com/partprogramming09/vegaflow-universal/tree/main/src/optimize/graphics), [`src/app/main.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp) and [`src/ui/gui_window.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp):

1. **NT kernel memory shield (`DisablePagingExecutive=1` & `LargeSystemCache=0`)**:
   - **Technical and physical explanation:** on APU systems with a unified memory architecture (UMA), Windows NT by default moves the executable code of drivers and kernel subsystems that are temporarily idle to the paging file (`pagefile.sys`). When the integrated Vega 6 GPU or the D3D pipeline needs those resources immediately, a hard page fault to the NVMe disk causes a noticeable freeze (micro-stutter) of 50 to 200 ms.
   - **NT mechanism:** VegaFlow sets, under `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management`, the value `DisablePagingExecutive = 1` and `LargeSystemCache = 0`. This locks all drivers and kernel executive components in physical RAM, preventing any paging to disk and ensuring sub-millisecond latency in the GPU's DPC interrupt routines.

2. **AMD smart tessellation limiter (`Tessellation="16"` & `Tessellation_OPTION="1"`)**:
   - **Technical explanation:** the Radeon RX Vega 6 iGPU has only 6 physical Compute Units (384 ALUs) and a single hardware *Primitive Engine / Tessellator*. Modern titles or effects with excessive tessellation (e.g. meshes over-tessellated to 64x) saturate the Primitive Engine with micro-triangles smaller than one pixel (*sub-pixel overdraw*), creating a severe geometry bottleneck with no visible gain.
   - **AMD driver mechanism:** in the `UMD` subkey of the video driver, VegaFlow sets `Tessellation = "16"` and `Tessellation_OPTION = "1"`. This caps maximum tessellation at **16x**, removes sub-pixel overdraw, relieves the Vega 6 CUs and noticeably stabilizes the frametime and the 1% lows.
