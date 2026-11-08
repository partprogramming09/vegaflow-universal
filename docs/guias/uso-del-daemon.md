# Uso del daemon

## 5. Modos de Uso y Ejecución

VegaFlow puede operar como servicio en segundo plano, como herramienta de consola o como interfaz gráfica de escritorio.

```
+----------------------------------------------------------------------------------+
|                            COMANDOS CLI DISPONIBLES                              |
+----------------------------------------------------------------------------------+
|  --status, -s             Muestra reporte detallado de hardware, RAM y tweaks   |
|  --gui, -g                Lanza la interfaz gráfica nativa Win32/GDI en bandeja  |
|  --system-info            Exporta telemetría de hardware, UMA y SO en JSON       |
|  --monitor, -m            Monitor en tiempo real en consola (RAM, VRAM, FPS)     |
|  --purge-now, -p          Fuerza purga inmediata de memoria Standby y trim       |
|  --mpo-disable            Desactiva Multi-Plane Overlay (MPO) en DWM             |
|  --mpo-restore            Restaura Multi-Plane Overlay (MPO) al valor de fábrica |
|  --shader-cache-force     Fuerza la caché de shaders en disco del driver AMD     |
|  --mem-compression-off    Desactiva la compresión de memoria de Windows          |
|  --dxvk-install <ruta>    Despliega DXVK-GPLAsync en la carpeta del juego        |
|  --dxvk-remove <ruta>     Elimina DXVK y restaura el backend DirectX 11          |
|  --apu-gpu-priority       Activa perfil térmico/energía con prioridad GPU (cap 3.4 GHz) |
|  --apu-balanced           Restaura el perfil de energía balanceado estándar      |
|  --fsr-validate           Compila y valida los shaders HLSL de FSR 1.0           |
|  --dry-run, -d            Simula ciclos de evaluación sin modificar el sistema   |
|  --help, -h               Imprime el manual de opciones                          |
+----------------------------------------------------------------------------------+
```

### Ejemplos Prácticos:

#### 1. Verificación del Estado del Sistema:
```powershell
.\build\vegaflow_daemon.exe --status
```
*Salida representativa con métricas de la suite moderna 3D:*
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
  External FSR 1.0 Scaler    : READY (EASU + RCAS 2-Pass D3D11 | Hotkey: Alt+F11)
```

#### 2. Exportación de Telemetría y Especificaciones en Formato JSON:
```powershell
.\build\vegaflow_daemon.exe --system-info
```
*Salida estructurada estándar:*
```json
{
  "cpu": {
    "brand_string": "AMD Ryzen 5 4600H with Radeon Graphics",
    "physical_cores": 6,
    "logical_processors": 12,
    "microarchitecture": "Zen 2 Renoir 7nm",
    "l3_cache_mb": 8.00,
    "boost_target_ghz": 3.40,
    "stapm_target": "3.4 GHz (APU Sustained / STAPM 35W-45W)"
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
    "build_number": 26300,
    "timer_resolution": { "current_ms": 0.50 }
  }
}
```

#### 3. Ejecutar la Interfaz Gráfica con Bandeja de Sistema:
```powershell
.\build\vegaflow_daemon.exe --gui
```
*Permite activar o desactivar optimizaciones para cada juego de la biblioteca de Steam de forma visual, consultar gráficos de RAM y alternar el modo de reescalado FSR/NIS.*

#### 4. Monitor de Consola en Tiempo Real:
```powershell
.\build\vegaflow_daemon.exe --monitor
```

#### 5. Desplegar DXVK Manualmente en un Juego sin Anti-Cheat:
```powershell
.\build\vegaflow_daemon.exe --dxvk-install "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```

#### 6. Revertir y Eliminar DXVK:
```powershell
.\build\vegaflow_daemon.exe --dxvk-remove "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```

---

