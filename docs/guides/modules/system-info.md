# Modules: SystemInfo and hardware telemetry

**In short:** `SystemInfo` is the part of VegaFlow that reads what the computer has: the processor, the graphics chip, the memory, the Windows version and so on. It produces one report, which can also be exported as JSON for scripts and other tools. For example, it gets the processor's name by asking the chip itself with the `CPUID` instruction. The readings that only Windows can give are in `windows/system_counters.cpp`.

**Reader:** people

## 3.2. SystemInfo subsystem: decoupled telemetry and hardware detection

Implemented in [`src/session/system_info.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/system_info.hpp) and [`src/session/system_info.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/system_info.cpp). This module isolates all the hardware and operating system data collection in one reusable library. The readings that need Windows (L3, DIMM slots, counts) are in [`src/windows/system_counters.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/windows/system_counters.cpp):

- **CPU detection**:
  - Brand reading with the x86 `CPUID` instruction (registers EAX, EBX, ECX, EDX), reporting the full processor name (e.g. *AMD Ryzen 5 4600H with Radeon Graphics*).
  - Exact count of physical cores (6) and logical processors (12) through `GetLogicalProcessorInformationEx`.
  - Microarchitecture identified: *Zen 2 Renoir 7nm*.
  - L3 cache size (8 MB / 8,388,608 bytes).
  - Target boost frequency of **4.0 GHz**, with no STAPM cap (`cpu_stapm_cap_mhz = 0`).
  - Masks and active CPU Sets for games and background processes.
- **Graphics & UMA memory (Vega 6)**:
  - DirectX adapter enumeration through `IDXGIFactory4` / `IDXGIAdapter3`.
  - Explicit difference between the false "Dedicated VRAM" reported by the BIOS (512 MB) and the real shared UMA system memory available to the iGPU (~7.7 GB).
  - Real-time query of local and non-local video memory budgets with `QueryVideoMemoryInfo`.
- **RAM channel topology (SMBIOS Type 17)**:
  - Direct reading of the system firmware tables through `GetSystemFirmwareTable('RSMB', 0, ...)` to inspect the SMBIOS Type 17 (Memory Device) records.
  - Detects the presence of 2 populated DIMM slots in **Dual-Channel with a 128-bit bus** configuration (~42 GB/s of bandwidth).
  - Instant breakdown of physical pages: memory in use, real free memory (Free + Zeroed), cached Standby List and modified pages.
- **NT kernel & operating system**:
  - Windows version reading (e.g. *Windows 11 Build 26300* with the Update Build Revision UBR).
  - Status of the MMCSS profile for games (`SystemResponsiveness`, `Priority`, `Scheduling Category`, `SFIO Priority`; without `GPU Priority`, retired in phase 8) and memory compression (MMAgent).
- **Standard JSON serialization**:
  - The `SystemInfo::ToJson()` function exports all the telemetry in a structured schema, for interoperability with scripts, web frontends or external CLI tools (`--system-info`).
