# Using the daemon and the command line

**In short:** VegaFlow can run as a background service, as a console tool, or as a desktop window.
You give it a command in a PowerShell window, from the folder where the program is.
For example, `--system-info` prints the machine's hardware as JSON, which other tools can read.

**Reader:** people

### 5. Ways to Run and Use It

VegaFlow can run as a background service, as a console tool, or as a native desktop window.

**Available console commands:**

| Command | What it does |
|---|---|
| `--status, -s` | Shows a detailed report of hardware, RAM and tweaks |
| `--gui, -g` | Opens the native Win32/GDI window, with a system tray icon |
| `--system-info` | Exports hardware, UMA and OS telemetry as JSON |
| `--monitor, -m` | Real-time console monitor (RAM, Standby List, VRAM, and the active game and API) |
| `--purge-now, -p` | Forces an immediate purge of the Standby memory and a trim |
| `--mpo-disable` | Disables Multi-Plane Overlay (MPO) in DWM |
| `--mpo-restore` | Restores Multi-Plane Overlay (MPO) to its factory value |
| `--shader-cache-force` | Forces the AMD driver's on-disk shader cache |
| `--mem-compression-off` | Disables Windows memory compression |
| `--dxvk-install <path>` | Deploys DXVK-GPLAsync into the game's folder |
| `--dxvk-remove <path>` | Removes DXVK and restores the DirectX 11 backend |
| `--apu-gpu-priority` | Activates the thermal and power profile with GPU priority (no STAPM frequency cap) |
| `--apu-balanced` | Restores the standard balanced power profile |
| `--dry-run, -d` | Simulates evaluation cycles without changing the system |
| `--help, -h` | Prints the manual of options |

#### Practical examples:

##### 1. Checking the state of the system:
```powershell
.\build\vegaflow_daemon.exe --status
```
*Representative output with the metrics of the modern 3D suite:*
```text
[CPU TOPOLOGY & SCHEDULING]
  Processor Brand            : AMD Ryzen 5 4600H with Radeon Graphics
  Physical Cores             : 6
  Logical Processors         : 12
  Architecture Type          : AMD Multi-CCX/CCD
  Game Optimal CPU Set IDs   : [258, 259, 260, 261, 262, 263, 264, 265]
  Background CPU Set IDs     : [266, 267]
  EcoQoS Throttled Processes : 0 processes in Efficiency Mode (Thermal Headroom for APU)

[SYSTEM GRAPHICS TWEAKS]
  Multi-Plane Overlay (MPO)  : DISABLED (OverlayTestMode=5, Stutter-Free)
  AMD ShaderCache Mode       : FORCED ON (0x3200, Stutter Elimination)
  AMD DX11/UMA Driver Boost  : ACTIVE (SurfaceFormat FP16 + FlipQueue=1)
  MMCSS WDDM Gaming Profile  : OPTIMIZED (GPU Priority=8, SystemResponsiveness=10%)
  GameDVR & FSE Bypass       : DEFAULT (Windows DVR Active)
  APU Laptop Power & Bus     : ACTIVE (Boost Efficient-Aggressive + PCIe ASPM Off)
  CPU Quantum Separation     : 0x2
  Windows Memory Compression : DISABLED (Zero CPU Spikes / Low Latency)
  Kernel Memory Paging Shield: ACTIVE (DisablePagingExecutive=1, RAM Lock)
  AMD Tessellation Limiter   : 16x (Over-Geometry Capped)
  DirectFlip / AutoFlip Model: READY (AutoFlipModel=1 & DirectFlip=1 Injection on Launch)
```

##### 2. Exporting telemetry and specifications as JSON:
```powershell
.\build\vegaflow_daemon.exe --system-info
```
*Standard structured output:*
```json
{
  "cpu": {
    "brand_string": "AMD Ryzen 5 4600H with Radeon Graphics",
    "physical_cores": 6,
    "logical_processors": 12,
    "microarchitecture": "Zen 2 Renoir 7nm",
    "l3_cache_mb": 8.00,
    "boost_target_ghz": 4.00,
    "stapm_target": "4.0 GHz Smart Uncapped (100% CPU + 100% GPU Efficient-Aggressive + EPP 25)"
  },
  "gpu": {
    "adapter_name": "AMD Radeon(TM) Graphics (Vega 6 / GCN 5.0)",
    "architecture": "UMA (Unified Memory Architecture)",
    "bios_dedicated_vram_mb": 495.56,
    "dynamic_shared_vram_gb": 7.71
  },
  "ram": {
    "total_physical_gb": 15.42,
    "channel_topology": "Dual-Channel (128-bit bus bandwidth)",
    "memory_bus_width_bits": 128
  },
  "os_kernel": {
    "os_name": "Windows 11",
    "build_number": 26300
  }
}
```

##### 3. Opening the graphical interface with the system tray:
```powershell
.\build\vegaflow_daemon.exe --gui
```
*Lets you turn optimizations on or off for each game in your Steam library, visually, and check RAM graphs.*

##### 4. Real-time console monitor:
```powershell
.\build\vegaflow_daemon.exe --monitor
```

##### 5. Installing DXVK by hand in a game without anti-cheat:
```powershell
.\build\vegaflow_daemon.exe --dxvk-install "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```

##### 6. Reverting and removing DXVK:
```powershell
.\build\vegaflow_daemon.exe --dxvk-remove "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```
