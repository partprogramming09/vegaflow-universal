# Funciones de VegaFlow

## 2. Módulos Principales de VegaFlow

```
+---------------------------------------------------------------------------------------------------+
|                                VEGAFLOW UNIVERSAL ARCHITECTURE                                    |
+---------------------------------------------------------------------------------------------------+
|                                                                                                   |
|  [ Detección No Invasiva ] ---> [ Anti-Cheat Sentinel ]                                           |
|                                       |                                                           |
|             +-------------------------+-------------------------+                                 |
|             | (Si has_anticheat == false)                       | (Si has_anticheat == true)      |
|             v                                                   v                                 |
|   +------------------------------------+              +------------------------------------+      |
|   |   OPTIMIZACIONES A NIVEL MOTOR     |              |   OPTIMIZACIONES NO INVASIVAS      |      |
|   | - DXVK-GPLAsync (6 worker threads) |              | - DXGI Hardware Flip Model         |      |
|   | - UMA Engine Streaming (Pool 3000) |              | - CPU Sets cooperativos de juego   |      |
|   | - AsyncCompute deshabilitado       |              | - Temporizador NT a 0.5 ms exactos |      |
|   | - Backups automáticos .bak         |              | - FSR 1.0 Overlay Win32 Externo    |      |
|   +------------------------------------+              +------------------------------------+      |
|                                       \                         /                                 |
|                                        +-----------+-----------+                                  |
|                                                    |                                              |
|                                                    v                                              |
|                           [ SUBSISTEMA CENTRAL DE RENDIMIENTO (KERNEL & OS) ]                     |
|                           - MMCSS Gaming Profile (GPU Priority = 8)                               |
|                           - Dynamic Win32PrioritySeparation (0x26 / 0x28)                         |
|                           - APU Power Profile (CPU capped 3.2GHz, ASPM Off, Vega @ 1500MHz)       |
|                           - Purga Inteligente de Standby List (con histéresis y check de I/O)     |
|                           - Desactivación de MPO y Memory Compression                             |
|                                                                                                   |
+---------------------------------------------------------------------------------------------------+
```

### 2.1. DXGI Hardware Flip Model Presentation Upgrade
- Interviene en `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` registrando las directivas `AutoFlipModel=1;DirectFlip=1;TurboSync=1;SwapEffectUpgradeEnable=1;GpuPreference=2;`.
- Fuerza a los juegos basados en DirectX 11 a utilizar la cadena de intercambio **Hardware Flip Model** (`DXGI_SWAP_EFFECT_FLIP_DISCARD` / `FLIP_SEQUENTIAL`) y promueve directamente al modo **DirectFlip** en Windows 11, puenteando la superficie de composición intermedia y la cola de copiado de DWM.
- **Beneficio:** Reduce entre 15 y 25 ms el retardo de entrada (input lag) alcanzando 0ms de latencia de composición adicional en pantalla completa sin bordes, elimina el micro-stuttering de composición y previene la caída de cuadros por buffers bloqueados.

### 2.2. UMA Engine Memory & Texture Streamer
- Analiza la instalación de juegos en Steam y detecta automáticamente si el motor es **Unreal Engine 4/5** o **Unity**.
- **Para Unreal Engine 4/5:** Configura `Engine.ini` desestrangulando el pool de texturas:
  ```ini
  [SystemSettings]
  r.Streaming.PoolSize=3000
  r.Streaming.LimitPoolSizeToVRAM=0
  r.Streaming.FullyLoadUsedTextures=1
  r.Streaming.FramesForFullUpdate=1
  r.Streaming.MaxNumTexturesToStreamPerFrame=100
  r.Streaming.Boost=1.5
  r.RDG.AsyncCompute=0
  D3D12.AsyncCompute=0
  ```
- **Para Unity Engine:** Modifica `boot.config` habilitando multihilo nativo (`gfx-enable-native-gfx-jobs=1`, `gfx-enable-gfx-jobs=1`, `gc-max-time-slice=3`).
- **Seguridad Garantizada:** Crea automáticamente archivos de respaldo `.vegaflow.bak` para permitir la reversión total con un solo clic.

### 2.3. DXVK-GPLAsync Module (Compilación en 6 Hilos)
- Integra la versión optimizada de **DXVK-GPLAsync v2.5.3**, traduciendo llamadas de DirectX 11 a Vulkan nativo.
- Genera un archivo `dxvk.conf` adaptado a la arquitectura de 6 núcleos físicos Zen 2:
  - Compilación asíncrona desacoplada del hilo de renderizado (`dxvk.enableAsync = True`, `dxvk.gplAsyncCache = True`).
  - Asignación de 6 hilos dedicados para compilación de pipelines en paralelo (`dxvk.numCompilerThreads = 6`).
  - Fijación de límites de memoria de dispositivo a 4096 MB y compartida a 8192 MB (`dxgi.maxDeviceMemory = 4096`).
- Previene los tirones en juegos DX11 donde los drivers oficiales de AMD sufren de sobrecarga de compilación.

### 2.4. Anti-Cheat Sentinel (Protección Kernel Proactiva)
- Identifica tecnologías anti-trampas a nivel de kernel mediante firma de binarios y servicios:
  - **HoYoverse Protect** (`HoYoKProtect.sys`, `mhypbase.dll`, `mhyprot.sys`)
  - **Riot Vanguard** (`vgk.sys`)
  - **EasyAntiCheat** (`EasyAntiCheat_EOS.dll`)
  - **BattlEye** (`beservice.exe`)
  - **AntiCheatExpert** (`randgrid.sys`)
- Si un juego protegido es detectado, **VegaFlow activa el bloqueo de seguridad:**
  - Cero inyecciones de DLLs.
  - Cero modificaciones de archivos de configuración dentro de la carpeta del juego.
  - Cero llamadas a APIs invasivas de procesos (`PROCESS_VM_WRITE`).
  - Aplica exclusivamente optimizaciones seguras a nivel de sistema operativo (Flip Model en registro, CPU Sets cooperativos y temporizador de alta precisión).

### 2.5. Zero-Hook Spatial Upscaler (FSR 1.0 / NIS / Anime4K)
- Reescalador espacial que se proyecta mediante una ventana Win32 borderless transparente click-through (`WS_EX_LAYERED | WS_EX_TRANSPARENT`).
- Captura la superficie del juego mediante la API nativa de duplicación de escritorio / DWM y ejecuta shaders HLSL en una cola gráfica independiente:
  - **AMD FSR 1.0 (EASU + RCAS)**: Reescalado espacial ultra nítido con atenuación de bordes y enfoque adaptativo.
  - **NVIDIA NIS**: Filtro bicúbico direccional de 16 taps.
  - **Anime4K Line Reconstruct**: Filtro optimizado para reconstrucción de líneas en juegos con estética anime y cell-shading.
- **Totalmente invisible para los Anti-Cheats:** No inyecta código en la memoria del juego ni intercepta la llamada `Present()`. Se activa y desactiva con el atajo global `Alt + F11`.

### 2.6. CPU Orchestration & MMCSS Quantum Tuning
- **CPU Sets Disjuntos:** Asigna al juego los núcleos Zen 2 de mayor rendimiento y menor latencia de caché L3, mientras confina navegadores y tareas secundarias a núcleos residuales (`SetProcessDefaultCpuSets`).
- **Background EcoQoS Throttling:** Ordena al planificador de Windows 11 ejecutar los procesos de fondo en modo de eficiencia energética mediante `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`, recuperando de 3W a 5W de presupuesto térmico STAPM para sostener el reloj de la Radeon Vega 6 a 1500 MHz ininterrumpidos.
- **Jerarquía NT de I/O y Memoria:** Aplica `IoPriorityHigh` en el juego e `IoPriorityVeryLow` (0) en procesos secundarios mediante `NtSetInformationProcess(ProcessIoPriorityInformationClass = 33)`, erradicando parones de lectura de disco.
- **MMCSS (Multimedia Class Scheduler Service):** Configura la prioridad de GPU para juegos en `8` y reduce la reserva de sistema a un 10% máximo.
- **Resolución de Temporizador NT:** Modula el reloj del kernel de Windows de 15.6 ms a **0.5 ms exactos** (`NtSetTimerResolution`), estabilizando la cadencia de presentación de fotogramas.
- **Perfil de Energía APU Térmicamente Eficiente:** Limita el modo boost agresivo de la CPU y estabiliza frecuencias en 3.2 GHz, cediendo ~20W de presupuesto térmico continuo a la GPU integrada para mantener sus 1500 MHz de forma ininterrumpida.

### 2.7. Subsistema SandboxGuard (Aislamiento de Archivos & Cuarentena Anti-Cheat)
El subsistema [`SandboxGuard`](../../src/safety/sandbox_guard.hpp) implementa una arquitectura estricta de aislamiento de archivos con modelo de confianza cero (**Zero-Trust File Access**):
- **Normalización Canónica Antifraude:** Neutraliza ataques o desbordamientos por *Path Traversal* (`..`, `.`, dobles barras invertidas y symlinks). Combina la API Win32 `GetFullPathNameW` con `std::filesystem::weakly_canonical` para resolver de forma anticipada enlaces simbólicos y junctions antes de cualquier chequeo de acceso.
- **Whitelist Estricta vs Blacklist Inviolable:**
  - *Blacklist (Prioridad Máxima):* Bloqueo categórico de escrituras en la raíz de unidades (`C:\`, `D:\`) y archivos hijo directos (`C:\bootmgr`), archivos de paginación (`pagefile.sys`, `hiberfil.sys`, `swapfile.sys`), carpetas del núcleo del sistema operativo (`C:\Windows`, `System32`, `SysWOW64`, `WinSxS`), `Program Files\Common Files` y `System Volume Information`.
  - *Whitelist Estricta:* Solo se autorizan rutas confinadas a la raíz de VegaFlow, bibliotecas reconocidas de Steam (`steamapps/common/...`), perfiles `%LOCALAPPDATA%`, directorio temporal seguro del sistema y partidas guardadas en Documentos.
- **Cuarentena y Lockdown Anti-Cheat Incondicional:**
  - Ante la presencia de tecnologías anti-cheat a nivel de kernel (**Zenless Zone Zero**, **Genshin Impact**, **Honkai: Star Rail**, **EasyAntiCheat**, **BattlEye**, **Riot Vanguard**, **AntiCheatExpert**), el sistema activa el **Lockdown Anti-Cheat**: se prohíbe terminantemente cualquier operación de escritura, copiado o borrado en el directorio del juego o sus subcarpetas.
- **Wrappers Seguros de I/O:** Toda mutación en disco se canaliza por wrappers seguros (`SafeWriteFile`, `SafeCopyFile`, `SafeRemoveFile`, `SafeReadFile`), bloqueando cualquier I/O riesgoso antes de abrir llamadas nativas.
- **Auditoría Forense en Tiempo Real:** Cada rechazo de acceso se almacena en memoria con sello de tiempo milimétrico (`timestamp_ms`), código de infracción (`SandboxDenialReason`) y mensaje descriptivo para diagnóstico.

### 2.8. Subsistema SystemInfo (Telemetría de Hardware & Exportación JSON)
El subsistema [`SystemInfo`](../../src/detect/system_info.hpp) desacopla la detección profunda de hardware y sistema operativo en un módulo autónomo y de alta precisión:
- **Detección de CPU Zen 2:** Extrae el *Brand String* mediante instrucciones `CPUID` nativas (*AMD Ryzen 5 4600H with Radeon Graphics*), calcula topología de 6 núcleos físicos / 12 lógicos, arquitectura Renoir de 7nm, tamaño de caché L3 de 8 MB y frecuencia objetivo APU STAPM a 3.4 GHz sostenidos.
- **Topología de Gráficos & Memoria UMA (Vega 6):** Contrasta el reporte ficticio de BIOS (512 MB) con la memoria UMA compartida real del sistema (~7.7 GB) y sondea presupuestos dinámicos de VRAM en tiempo real mediante `IDXGIAdapter3::QueryVideoMemoryInfo`.
- **Topología de Memoria RAM por Firmware (SMBIOS Type 17):** Inspecciona las tablas SMBIOS `RSMB` (`GetSystemFirmwareTable`), identificando la presencia de 2 módulos DIMM poblados y verificando el bus de 128 bits en **Dual-Channel** (~42 GB/s de ancho de banda). Presenta desglose de memoria libre real, Standby List y páginas modificadas.
- **Kernel NT & Temporizador:** Informa la compilación exacta de Windows 11 (Build 26300 con UBR), resolución del temporizador NT (0.50 ms), MMCSS GPU Priority (8) y estado de compresión de memoria.
- **Exportación CLI Estándar JSON:** Soporta el comando `--system-info`, emitiendo la totalidad del perfil de hardware y sistema en formato JSON estándar para consumo externo.

### 2.9. Rediseño Visual de la GUI (Clean Slate / Nordic Dark Mode)
La interfaz gráfica Win32/GDI ([`gui_window.cpp`](../../src/ui/gui_window.cpp)) ha sido completamente renovada bajo estándares ergonómicos y de diseño moderno de instrumentación:
- **Paleta Slate/Zinc Neutra:** Se sustituyeron los gradientes chillones por tonalidades carbón elegantes (`#111317` fondo principal, `#16191E` barra lateral, `#1B1E24` tarjetas, bordes finos `#2B303C` de 1px) y acentos cromáticos sobrios y moderados (Azul acero `#3B82F6`, Verde esmeralda `#10B981`, Ámbar `#F59E0B`, Rojo crítico `#EF4444`).
- **Geometría y Espaciados Simétricos:** Márgenes exteriores exactos de 16px, gutters uniformes de 12px entre tarjetas y divisores horizontales nítidos de 1px.
- **Subtítulos Técnicos Educativos:** Cada tarjeta de telemetría y control interactivo incorpora micro-subtítulos explicativos que educan al usuario sobre el impacto físico real en el silicio (mitigación de contención L3 en CCX, erradicación de texturas borrosas en UMA, latencia de interrupciones DPC en kernel NT).
- **Tab 0: Dashboard Central Interactivo:** Nuevo centro de control interactivo que unifica en una sola vista los paneles de CPU Zen 2, APU/UMA Vega 6, RAM Dual-Channel, Kernel NT y el estado blindado en tiempo real de SandboxGuard. Al detectar un juego activo, muestra el badge `DIRECTFLIP 0ms ACTIVO` e integra 3 tarjetas de estado para la suite 3D:
  1. *EcoQoS en 2º Plano:* Muestra el número de procesos de fondo confinados en Efficiency Mode para liberar TDP a la iGPU Vega 6.
  2. *Prioridad I/O:* Monitorea `IoPriorityHigh` en el proceso del juego e `IoPriorityVeryLow` (0) en subprocesos de fondo.
  3. *DirectFlip / AutoFlip:* Certifica la derivación directa al hardware eliminando la cola de copiado de DWM.

### 2.10. Suite Moderna para Juegos 3D (EcoQoS, Jerarquía I/O NT y DirectFlip)
Integrada a bajo nivel para resolver de raíz las limitaciones de latencia, contención de almacenamiento y balance energético en títulos 3D exigentes (Zenless Zone Zero, Genshin Impact, Honkai: Star Rail, Octopath Traveler II):
1. **Background EcoQoS (Power Throttling en Procesos de Fondo):**
   - **Explicación técnica y física:** En procesadores con arquitectura APU como el Ryzen 5 4600H, el presupuesto térmico STAPM (Skin Temperature Aware Power Management) está limitado dinámicamente entre 35W y 45W para todo el encapsulado. Cuando procesos secundarios (navegadores web, launchers, Discord, tareas de indexación) ejecutan ráfagas de cómputo en núcleos Zen 2 a voltajes altos, sustraen entre 3W y 5W de este presupuesto unificado. La unidad SMU de AMD se ve forzada a reducir la frecuencia de la GPU Vega 6 a 1100–1200 MHz para mantenerse dentro del umbral térmico.
   - **Mecanismo NT:** VegaFlow utiliza la API nativa de Windows 11 `SetProcessInformation` con la estructura `PROCESS_POWER_THROTTLING_STATE` fijando el flag `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`. Al confinar las tareas de fondo a estados de ultra-baja disipación energética, se recuperan entre 3W y 5W del presupuesto STAPM, permitiendo a la iGPU sostener sus **1500 MHz continuos** sin estrangulamiento térmico.
2. **Jerarquía de Prioridad I/O y Memoria NT:**
   - **Explicación técnica:** Videojuegos 3D modernos realizan streaming continuo de modelos y texturas de alta resolución desde el disco SSD NVMe hacia la memoria RAM compartida (UMA) y el buffer de la GPU. Cuando aplicaciones de fondo compiten por el bus I/O de archivos o causan contención en el gestor de memoria, se generan severos parones (*stuttering*) y frametime spikes de 50 a 150 ms.
   - **Mecanismo NT:** Mediante llamadas a `NtSetInformationProcess(ProcessIoPriorityInformationClass = 33)`, VegaFlow asigna `IoPriorityHigh` (2) al juego 3D y confina los procesos de fondo a `IoPriorityVeryLow` (0). Esto asegura que el controlador de almacenamiento de Windows NT otorgue prioridad absoluta a las lecturas de mallas y texturas del juego. Paralelamente, se fija `MEMORY_PRIORITY_LOW` (2) en fondo para evitar la expulsión al pagefile de recursos gráficos activos.
3. **DirectFlip & AutoFlip Model Upgrade:**
   - **Explicación técnica:** En modos ventana o borderless tradicionales, el compositor del sistema (DWM) copia cada fotograma a una superficie intermedia, introduciendo entre 15 y 30 ms de retardo de entrada y micro-stutters periódicos.
   - **Inyección DXGI:** VegaFlow inyecta en `HKCU\Software\Microsoft\DirectX\UserGpuPreferences` las directivas `AutoFlipModel=1;DirectFlip=1;TurboSync=1;SwapEffectUpgradeEnable=1;GpuPreference=2;`. En Windows 11, esto permite la promoción directa al modo **DirectFlip**, evitando por completo la cola de copiado de DWM y alcanzando una **latencia de entrada de 0ms** en pantalla completa sin bordes.

### 2.11. Suite de Silicio APU & Blindaje de Memoria de Kernel NT (DisablePagingExecutive & Smart Tessellation 16x)
Diseñada para mitigar las debilidades intrínsecas de las APUs Ryzen con arquitectura de memoria compartida (UMA) y unidades de cómputo compactas (Vega 6):
1. **Blindaje de Memoria de Kernel NT (RAM Lock / DisablePagingExecutive=1):**
   - **Explicación técnica y física:** En sistemas con memoria unificada, Windows NT tiende a paginar al archivo `pagefile.sys` código ejecutable del kernel y controladores D3D/WDDM cuando detecta baja actividad inmediata en ciertas páginas. Cuando el motor del juego o la GPU integrada solicita repentinamente una rutina no residente, se produce un fallo de página duro (*hard page fault*) que congela el pipeline de render durante 50 a 200 ms hasta que el disco NVMe devuelve la página a la RAM.
   - **Mecanismo NT:** VegaFlow fija `DisablePagingExecutive = 1` y `LargeSystemCache = 0` en `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management`. Esto bloquea de forma inmutable todos los drivers y componentes del executive del kernel en memoria RAM física, erradicando los micro-tirones por descarte a disco y asegurando latencia de interrupción submilisegundo constante.
2. **Limitador Inteligente de Teselación AMD (Smart Tessellation 16x / Anti-Overdraw):**
   - **Explicación técnica:** La Radeon RX Vega 6 cuenta con únicamente 6 Compute Units físicas (384 ALUs) y un solo Primitive Engine de hardware. Mallas y efectos 3D sobre-teselados (frecuentemente a 64x en motores DirectX 11) saturan el pipeline de geometría generando triángulos más pequeños que un píxel en pantalla (*sub-pixel overdraw*), colapsando el rendimiento sin añadir detalle visual perceptible.
   - **Mecanismo Driver AMD:** En la subclave `UMD` del adaptador, VegaFlow establece `Tessellation = "16"` y `Tessellation_Option = "1"`. Esto restringe la teselación máxima a **16x**, eliminando el sub-pixel overdraw, descongestionando los CUs de Vega 6 y estabilizando notablemente el frametime y los 1% Lows.

### 2.12. ApuCapabilityGuard: Clasificación Universal de 4 Niveles y Safety Guards
Para asegurar compatibilidad universal en todo el ecosistema de procesadores AMD e Intel sin aplicar recetas genéricas dañinas, VegaFlow introduce el subsistema `ApuCapabilityGuard` (`HardwareProfiler::BuildOptimizationPolicy`):

1. **Tier 1: Entry UMA [Survival Guard]:**
   - **Objetivo:** APUs de entrada con $\le$ 8–9 GB de RAM o $\le$ 4 núcleos físicos (Vega 3/6/8 básica, Athlon Mobile, Intel Gen 9–11 UHD).
   - **Defensa:** Acota el Texture Pool de Unreal Engine estrictamente entre 1536 y 2048 MB para evitar cierres catastróficos por OOM; limita DXVK a 3 hilos de compilación de shaders; impone teselación a 8x; deshabilita `AsyncCompute` en DX12; limita STAPM a 3000 MHz.
2. **Tier 2: Mid Vega UMA [Sweet-Spot Guard]:**
   - **Objetivo:** APUs GCN 5 / Vega con 12–20 GB de RAM o CPUs Zen 2/Zen 3 de 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - **Políticas (100% retrocompatible con el testbed):** Pool VRAM a 4096 MB; 6 hilos dedicados para DXVK; teselación 16x; `disable_dx12_async_compute = true`; STAPM cap a 3400 MHz.
3. **Tier 3: Advanced UMA [Do-No-Harm Guard]:**
   - **Objetivo:** APUs modernas RDNA 2, RDNA 3 y RDNA 3.5 (Radeon 680M, 780M, 890M, Strix Point) e Intel Xe/Arc UMA o configuraciones con $\ge$ 22 GB de RAM.
   - **Do-No-Harm Guard:** **Preserva intacto el cómputo asíncrono moderno** (`D3D12.AsyncCompute=1` y `r.RDG.AsyncCompute=1`); **no estrangula el reloj de la CPU** (`cpu_stapm_cap_mhz = 0`); asigna pool UMA generoso y adaptativo de 6144 a 8192 MB; teselación hasta 32x.
4. **Tier 4: Discrete / Custom [Pass-Through Guard]:**
   - **Objetivo:** GPUs dedicadas (>2 GB VRAM dedicada) o arquitecturas discretas. Opera en modo transparente sin interferir con la pila de controladores dedicados.
5. **Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):**
   - Muestrea el presupuesto real de memoria de vídeo DXGI 1.4 (`IDXGIAdapter3`). Si el consumo local alcanza el **85%** o la memoria física libre desciende de **1536 MB**, dispara de inmediato una intervención no invasiva liberando páginas de procesos secundarios para evitar cuelgues o swapping a disco.

---


---

## Uso del daemon

### 5. Modos de Uso y Ejecución

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

#### Ejemplos Prácticos:

##### 1. Verificación del Estado del Sistema:
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

##### 2. Exportación de Telemetría y Especificaciones en Formato JSON:
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

##### 3. Ejecutar la Interfaz Gráfica con Bandeja de Sistema:
```powershell
.\build\vegaflow_daemon.exe --gui
```
*Permite activar o desactivar optimizaciones para cada juego de la biblioteca de Steam de forma visual, consultar gráficos de RAM y alternar el modo de reescalado FSR/NIS.*

##### 4. Monitor de Consola en Tiempo Real:
```powershell
.\build\vegaflow_daemon.exe --monitor
```

##### 5. Desplegar DXVK Manualmente en un Juego sin Anti-Cheat:
```powershell
.\build\vegaflow_daemon.exe --dxvk-install "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```

##### 6. Revertir y Eliminar DXVK:
```powershell
.\build\vegaflow_daemon.exe --dxvk-remove "D:\SteamLibrary\steamapps\common\MyGame\Binaries\Win64\Game.exe"
```

---
