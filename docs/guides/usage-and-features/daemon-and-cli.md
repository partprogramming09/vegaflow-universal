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
| `--doctor [--json]` | Read-only diagnosis you can paste in an issue: free disk space, shader caches and manufacturer tools that fight VegaFlow |
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
| `--observe` | Safe mode on demand: nothing changes by itself, and no restore point is offered. It applies to every mode, not only `--gui` |
| `--once, -1` | Runs one evaluation cycle and exits |
| `--elevate, -e` | Restarts VegaFlow with Administrator rights (a Windows UAC prompt). If it already has them, it only says so and exits, unless another option is given |
| `--shader-cache-default` | Puts the AMD driver's shader cache back to the driver default |
| `--mem-compression-on` | Turns Windows memory compression back on, but only if VegaFlow turned it off (it reverts only its own journal entry) |
| `--vram-override` | Reports the tier's safe VRAM pool to DXGI and the AMD driver (4096 MB on Tier 2; see [APU tiers](../hardware-and-tiers/tiers-and-guards.md)) |
| `--vram-restore` | Puts back the AMD VRAM reporting that VegaFlow saved. If nothing was saved, nothing changes |
| `--restore-registry` | Undoes every change still recorded in the journal, and every registry backup VegaFlow made. Exit code 0 only if all of them come back |
| `--recover` | Same as `--restore-registry`; use it after a crash |
| `--create-restore-point` | Tries to create a Windows System Restore Point now |
| `--version` | Prints the version and exits |
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
  Multi-Plane Overlay (MPO)  : DISABLED (OverlayTestMode=5, undocumented)
  AMD ShaderCache Mode       : FORCED ON (0x3200, Stutter Elimination)
  AMD DX11/UMA Driver Boost  : ACTIVE (SurfaceFormat FP16 + FlipQueue=1)
  MMCSS WDDM Gaming Profile  : OPTIMIZED (SystemResponsiveness=10%, High Priority)
  GameDVR Capture            : DEFAULT (Windows DVR Active)
  APU Laptop Power & Bus     : ACTIVE (Boost Efficient-Aggressive + PCIe ASPM Off)
  CPU Quantum Separation     : 0x2
  Windows Memory Compression : DISABLED (Zero CPU Spikes / Low Latency)
  Kernel Memory Paging Shield: ACTIVE (DisablePagingExecutive=1, RAM Lock)
  AMD Tessellation Limiter   : 16x (Over-Geometry Capped)
  Windowed Flip Model        : READY (SwapEffectUpgradeEnable=1 on Launch)
```

##### Diagnosing "it runs badly": `--doctor`
```powershell
.\build\vegaflow_daemon.exe --doctor          # text
.\build\vegaflow_daemon.exe --doctor --json   # the same for scripts
```
It only reads: it writes nothing and deletes nothing, and it works without administrator rights (it then says what it could not read). The exit code is `0` when all is well, `1` for warnings and `2` when something is critical, so a script can branch on it.

| Check | Rule (in `rust/vegaflow-core/src/policy/doctor.rs`) |
|---|---|
| Free disk space (system drive, and the drive of the open game when it differs) | `Low` under 20 GiB or under 10 % free; `Critical` under 5 GiB, because the shader cache can no longer grow. Both limits are strict: exactly 20 GiB free of 100 GiB is fine. A failed probe is reported as "could not read", never divided |
| Shader caches | Info when no cache folder exists; warning when no AMD or NVIDIA cache folder exists, or when the caches are empty on a critical disk; info over 10 GiB (deleting it is safe, the driver rebuilds it, and VegaFlow deletes nothing) |
| Manufacturer tools | Warning for each of ASUS Armoury Crate, Lenovo Vantage and Legion Zone, MSI Center and Dragon Center, HP Omen Gaming Hub, MSI Afterburner, RTSS and AMD Ryzen Master that is running, with what it also changes (power plan, fan curves, GPU limits). Matched by file name, any letter case, with or without a path; one tool with three processes is one warning |

The report also lists every shader cache folder that exists (kind, path, size and file count; `shader_caches` in the JSON), so a healthy cache is visible too, not only a problem.

The report is made to be pasted in public: the Windows user name in paths becomes `<usuario>`, and the computer and domain names become `<equipo>` and `<dominio>`. Lines are never cut. The executable names of the manufacturer tools are the ones the products ship today; vendors rename them between versions, so a missing warning is not proof that the tool is absent.

Shape of the output (taken from the unit-test inputs, not yet captured on the test laptop):
```text
VegaFlow doctor 0.1.0
Administrator: yes
Disks:
  System drive (C:\): 4.0 GiB free of 500.0 GiB
Shader caches:
  AMD: C:\Users\<usuario>\AppData\Local\AMD\DxCache: 144.0 MiB (340 files)
Findings:
  [CRITICAL] System drive (C:\) has only 4.0 GiB free: the shader cache cannot grow
  [WARN] ASUS Armoury Crate is running and also changes the power plan and fan curves: it can fight VegaFlow over them
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
