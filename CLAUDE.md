# CLAUDE.md — VegaFlow Universal Developer & Architecture Guide

> **Guía Maestra de Arquitectura, Ingeniería de Sistemas y Estándares de Desarrollo**  
> *VegaFlow Universal — High-Performance APU & Gaming Optimization Framework*

---

## 1. Filosofía del Proyecto y Principios de Ingeniería

VegaFlow Universal nace de un principio fundamental de ingeniería de software y hardware: **no se trata de copiar código de terceros ni aplicar "tweaks" milagrosos a ciegas, sino de comprender con rigor técnico de raíz cómo funcionan el sistema operativo Windows NT, el subsistema gráfico DXGI/DWM y la arquitectura de hardware del procesador para optimizar el consumo de recursos y maximizar el rendimiento real de los videojuegos.**

```
+-----------------------------------------------------------------------------------+
|                        FILOSOFÍA DE DESARROLLO VEGAFLOW                           |
+-----------------------------------------------------------------------------------+
| 1. Rigor Técnico y Matemático: Cada cambio se fundamenta en especificaciones      |
|    oficiales de Microsoft, AMD y el estándar Vulkan/DirectX.                      |
| 2. Cero Overhead en Reposo: Menos de 15 MB de RAM privada (Private Working Set)   |
|    y 0.0% de uso de CPU mientras no haya un juego en ejecución activa.            |
| 3. Reversibilidad Total: Ninguna alteración es destructiva. Copias de seguridad    |
|    inmediatas (.vegaflow.bak, *.original.bak) y comandos de reversión completos.  |
| 4. Integridad y Seguridad Anti-Cheat: Respeto absoluto a los sistemas de juego    |
|    limpio. Cero inyecciones o alteraciones en títulos con protección kernel.     |
+-----------------------------------------------------------------------------------+
```

### Mandamientos del Desarrollador en VegaFlow:
- **Medición empírica antes de aseverar:** Ninguna optimización se considera válida sin evidencia medible (percentiles 1% Lows, 0.1% Lows, frametime delta y pruebas de estrés).
- **No reinventar el subsistema del kernel:** Usar las APIs nativas de Windows NT (`NtSetTimerResolution`, `NtSetSystemInformation`, `SetProcessDefaultCpuSets`, `SetProcessInformation`) en lugar de hacks o drivers inestables.
- **Transparencia hacia el usuario:** Todo cambio en registros o archivos del motor se registra en logs detallados y es revertible con un solo comando.

---

## 2. Arquitectura de Hardware y Restricciones APU (AMD Ryzen 5 4600H + Radeon RX Vega 6)

VegaFlow está optimizado a nivel de microarquitectura para APUs (Accelerated Processing Units) de AMD, con el banco de pruebas principal centrado en el procesador **AMD Ryzen 5 4600H** (6 núcleos, 12 hilos, arquitectura Zen 2 a 3.0 GHz base / 4.0 GHz boost) acoplado a gráficos integrados **AMD Radeon RX Vega 6** (6 Compute Units / 384 Stream Processors a 1500 MHz, arquitectura GCN 5.0).

```
+----------------------------------------------------------------------------------+
|                     ARQUITECTURA UMA (UNIFIED MEMORY)                            |
|                                                                                  |
|   +-----------------------+              +-----------------------+               |
|   |   CPU Zen 2 (6C/12T)  |              |   iGPU Vega 6 (6 CUs) |               |
|   |  35W-45W Thermal TDP  |              |  384 Stream Processors|               |
|   +-----------+-----------+              +-----------+-----------+               |
|               |                                      |                           |
|               +------------------+-------------------+                           |
|                                  |                                               |
|                    [ Infinity Fabric Bus / IMC ]                                 |
|                                  |                                               |
|                     Canal Dual DDR4-3200 MHz                                     |
|                  Ancho de Banda: ~38 - 42 GB/s                                   |
|                                                                                  |
|      [ Memoria Física Compartida: 16 GB Total ]                                  |
|      - Falso reporte de BIOS: 512 MB "VRAM Dedicada"                             |
|      - Memoria real compartida disponible para GPU: Hasta 8192 MB                |
+----------------------------------------------------------------------------------+
```

### 2.1. El Cuello de Botella de la Memoria UMA (Unified Memory Architecture)
A diferencia de una tarjeta gráfica dedicada que cuenta con su propio bus GDDR6 de 128 o 256 bits (ancho de banda de 200 a 500+ GB/s), la APU comparte la memoria RAM del sistema a través de un bus DDR4 de 128 bits dual-channel con un ancho de banda teórico de ~42 GB/s.
- La CPU y la iGPU **compiten por el mismo ancho de banda**. Si la CPU realiza lecturas excesivas de memoria o compresión en segundo plano, la GPU sufre caídas drásticas de tasa de llenado y framerate.
- **Mitigación VegaFlow:** Aislamiento de procesos secundarios mediante CPU Sets, desactivación de GameDVR y temporización de purgas de memoria únicamente en periodos de bajo I/O de disco (`< 15 MB/s`).

### 2.2. El Falso Límite de 512 MB de VRAM y el Texture Streaming
Por razones de compatibilidad con software legado de 32 bits, las BIOS y controladores de AMD reportan a menudo solo 512 MB de "VRAM Dedicada" (`DXGI_ADAPTER_DESC::DedicatedVideoMemory`).
- **El Problema:** Motores modernos como Unreal Engine 4 y Unreal Engine 5 leen esta propiedad y restringen automáticamente el tamaño del banco de texturas (`r.Streaming.PoolSize`). El resultado es una catástrofe visual: texturas borrosas que nunca terminan de cargar, pop-in constante y parones continuos de CPU mientras el motor descarga y recarga texturas continuamente.
- **La Solución Técnica de VegaFlow:** Desactivar la restricción del motor e instruirlo a utilizar la memoria física real:
  ```ini
  r.Streaming.LimitPoolSizeToVRAM=0
  r.Streaming.PoolSize=3000
  r.Streaming.FullyLoadUsedTextures=1
  r.Streaming.FramesForFullUpdate=1
  r.Streaming.MaxNumTexturesToStreamPerFrame=100
  r.Streaming.Boost=1.5
  ```

### 2.3. Ausencia de GPL (`VK_EXT_graphics_pipeline_library`) en Drivers AMD GCN 5 / Vega
Los controladores oficiales de AMD Adrenalin para la arquitectura Vega no exponen la extensión de Vulkan `VK_EXT_graphics_pipeline_library`.
- En emuladores y capas de traducción estándar como DXVK vanilla, la falta de GPL obliga a compilar los shaders en el hilo de renderizado principal en el momento exacto en que un efecto aparece en pantalla, produciendo parones severos (shader compilation stutter).
- **Solución VegaFlow:** Integración nativa del subsistema `DXVK-GPLAsync` con configuración multihilo para Vega 6:
  ```ini
  dxvk.gpl = True
  dxvk.enableAsync = True
  dxvk.gplAsyncCache = True
  dxvk.numCompilerThreads = 6
  dxgi.maxDeviceMemory = 4096
  dxgi.maxSharedMemory = 8192
  ```
  Esto delega la compilación de pipelines a 6 hilos de CPU en segundo plano, eliminando completamente los congelamientos durante el juego.

### 2.4. Inviabilidad de `AsyncCompute` en 6 Compute Units
`AsyncCompute` permite a GPUs de gran escala ejecutar computación y gráficos de manera concurrente en colas separadas. Sin embargo, en una iGPU con tan solo 6 Compute Units físicas (384 ALUs), el hardware scheduler no dispone de recursos ociosos suficientes. Forzar `AsyncCompute` satura los CUs, añade latencia de context-switching y empeora el frametime.
- **Solución VegaFlow:** En motores Unreal Engine, VegaFlow desactiva explícitamente el cómputo asíncrono para liberar los CUs exclusivamente para la rasterización:
  ```ini
  r.RDG.AsyncCompute=0
  D3D12.AsyncCompute=0
  ```

### 2.5. Balance Térmico CPU/GPU (Presupuesto STAPM de 35W-45W)
En laptops APU, la CPU y la GPU comparten el mismo disipador térmico y el mismo paquete de energía. Si la CPU sube a 4.0 GHz con voltajes de 1.35V consumiendo 35W, la iGPU se ve estrangulada a 600-800 MHz.
- **Solución VegaFlow:** Perfil de energía APU especializado (`SetApuLaptopPowerProfile`):
  - Limita el modo boost a *Efficient Aggressive* y la frecuencia máxima sostenida a 3.2 GHz con 99% throttle cap.
  - La CPU opera holgadamente consumiendo solo 12-14W, dejando 20W de TDP continuo para que la Radeon Vega 6 trabaje a sus 1500 MHz máximos estables sin caídas térmicas.
  - Desactiva PCIe ASPM en modo AC para eliminar latencias en el bus Infinity Fabric.

### 2.6. Motor de Clasificación Universal de 4 Niveles (4-Tier APU Engine) y Safety Guards (`ApuCapabilityGuard`)
Para trascender el soporte exclusivo a Ryzen 5 4600H sin introducir regresiones ni degradar silicios modernos, VegaFlow incorpora un clasificador determinista de 4 niveles (`ApuTier`) gestionado por `HardwareProfiler::BuildOptimizationPolicy()`:

1. **`Tier1_EntryUMA` [Survival Guard]:**
   - **Hardware objetivo:** APUs con RAM $\le$ 8–9 GB o $\le$ 4 núcleos físicos (Vega 3/6/8 básica, Athlon Mobile, Intel Gen 9–11 UHD).
   - **Directivas:** Techo estricto de textura pool VRAM entre 1536 y 2048 MB (`safe_vram_pool_mb`) para evitar cuelgues OOM del sistema; 3 hilos de compilación en DXVK; límite de teselación AMD a 8x; `disable_dx12_async_compute = true`; tope de frecuencia STAPM a 3000 MHz.
2. **`Tier2_MidVegaUMA` [Sweet-Spot Guard]:**
   - **Hardware objetivo:** APUs Vega/GCN5 con 12–20 GB de RAM o procesadores Zen 2/Zen 3 de 6C–8C (Ryzen 5 4600H, 5500U, 5600H).
   - **Directivas (100% retrocompatible con el testbed):** Pool VRAM a 4096 MB; 6 hilos dedicados de compilación en DXVK-GPLAsync; límite de teselación a 16x; `disable_dx12_async_compute = true`; tope de frecuencia STAPM a 3400 MHz.
3. **`Tier3_AdvancedUMA` [Do-No-Harm Guard]:**
   - **Hardware objetivo:** APUs modernas RDNA2/RDNA3/RDNA3.5 (Radeon 680M, 780M, 890M, Strix Point) e Intel Xe/Arc UMA, o sistemas UMA con $\ge$ 22 GB de RAM.
   - **Directivas ("Do-No-Harm"):** Preserva intacto el cómputo asíncrono moderno (`D3D12.AsyncCompute=1` y `r.RDG.AsyncCompute=1`); **no estrangula la CPU** (`cpu_stapm_cap_mhz = 0`); asigna pool UMA generoso y adaptativo de 6144 a 8192 MB; teselación permitida hasta 32x; 6 a 8 hilos en DXVK.
4. **`Tier4_DiscreteOrCustom` [Pass-Through Guard]:**
   - **Hardware objetivo:** GPUs dedicadas (>2 GB VRAM dedicada) o arquitecturas discretas.
   - **Directivas:** Modo pass-through transparente sin aplicar overrides forzados de UMA ni limitaciones de silicio integrado.

#### Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):
Supervisa en caliente el uso real del presupuesto de memoria local de vídeo (`local_usage_bytes / local_budget_bytes`) mediante DXGI 1.4 (`IDXGIAdapter3`). Si la saturación del presupuesto sobrepasa el **85%** o la memoria física libre cae por debajo del umbral crítico de **1536 MB**, activa el guard defensivo de forma no invasiva, registrando la intervención y recortando el working set de procesos en segundo plano para prevenir paging stalls hacia disco o cierres abruptos por OOM.

---

## 3. Seguridad Absoluta Frente a Sistemas Anti-Cheat y Subsistema SandboxGuard

VegaFlow Universal implementa una política de seguridad estricta e inviolable: **bajo ninguna circunstancia se inyectarán DLLs ni se modificarán archivos de juegos protegidos por sistemas Anti-Cheat.** Toda operación sobre el disco y los procesos se rige por un modelo de aislamiento y contención formalizado en el módulo [`SandboxGuard`](include/security/sandbox_guard.hpp).

```
+------------------------------------------------------------------------------------+
|                         MATRIZ DE DECISIÓN DE SEGURIDAD                            |
+------------------------------------------------------------------------------------+
|                                                                                    |
|                     ¿Se detecta tecnología Anti-Cheat?                             |
|       (HoYoKProtect, Vanguard, EAC, BattlEye, ACE, mhypbase, etc.)                 |
|                                                                                    |
|                   /                                   \                            |
|                 SÍ                                     NO                          |
|                /                                         \                         |
|   +-----------------------------+        +-----------------------------------+     |
|   |   RUTA DE OPTIMIZACIÓN      |        |   RUTA DE OPTIMIZACIÓN COMPLETA   |     |
|   |         NO INVASIVA         |        |         (NATIVA + MOTOR)          |     |
|   +-----------------------------+        +-----------------------------------+     |
|   | 1. DXGI Flip Model vía      |        | 1. DXGI Flip Model (Registro).    |     |
|   |    registro de Windows.     |        | 2. CPU Sets cooperativos.         |     |
|   | 2. CPU Sets cooperativos.   |        | 3. DXVK-GPLAsync en carpeta.      |     |
|   | 3. Temporizador NT a 0.5 ms |        | 4. Presets UMA Engine.ini /       |     |
|   | 4. MMCSS Gaming GPU Prio 8. |        |    boot.config con .vegaflow.bak  |     |
|   | 5. FSR por Overlay externo  |        | 5. FSR Espacial Alt+F11.          |     |
|   |    (cero memoria de juego). |        | 6. Purga de memoria inteligente.  |     |
|   | 6. PROHIBIDO MODIFICAR O    |        |                                   |     |
|   |    INYECTAR ARCHIVOS        |        |                                   |     |
|   |    (LOCKDOWN TOTAL SANDBOX) |        |                                   |     |
|   +-----------------------------+        +-----------------------------------+     |
|                                                                                    |
+------------------------------------------------------------------------------------+
```

### Firmas Monitoreadas en Tiempo Real:
- **EasyAntiCheat**: `easyanticheat`, `EasyAntiCheat_EOS.dll`
- **BattlEye**: `battleye`, `beservice.exe`
- **Riot Vanguard**: `vanguard`, `vgk.sys`
- **HoYoverse Protect**: `hoyokprotect`, `mhyprot`, `mhypbase` (Zenless Zone Zero, Genshin Impact, Honkai: Star Rail)
- **Tencent AntiCheatExpert**: `anticheatexpert`, `randgrid.sys`, `acebase.dll`
- **Otras tecnologías de integridad**: `blackcipher`, `equ8`, `nprotect`, `gameguard`, `xigncode`

Cualquier intento de desplegar DXVK o sobreescribir archivos en un directorio con estas firmas es **rechazado en seco por el código** (`blocked_by_anticheat = true`), salvaguardando la cuenta del usuario de cualquier riesgo de baneo.

### 3.1. Arquitectura de Aislamiento y Contención: `SandboxGuard`
Implementado en [`include/security/sandbox_guard.hpp`](include/security/sandbox_guard.hpp) y [`src/security/sandbox_guard.cpp`](src/security/sandbox_guard.cpp), el subsistema `SandboxGuard` formaliza el paradigma de seguridad **Zero-Trust File Access**:

1. **Normalización Canónica (`CanonicalizePath`)**:
   - Neutraliza de raíz vectores de ataque o errores de **Path Traversal** (`..`, `.`, dobles barras invertidas y symlinks).
   - Utiliza la API nativa Win32 `GetFullPathNameW` para resolver rutas relativas y el motor `std::filesystem::weakly_canonical` para resolver enlaces simbólicos y junctions en directorios existentes o en vías de creación.
   - Estandariza letras de unidad a mayúsculas y suprime separadores finales redundantes.

2. **Whitelist Estricta vs Blacklist de Sistema (Prioridad Inviolable)**:
   - **Blacklist (Máxima Prioridad)**:
     - Bloqueo categórico de escritura en la raíz de unidades de disco (`C:\`, `D:\`) y archivos hijo directos (`C:\bootmgr`, `C:\test.txt`) mediante `IsDriveRootOrDirectChild`.
     - Archivos críticos de arranque y paginación del sistema operativo (`bootmgr`, `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `ntldr`, `bootsect.bak`).
     - Directorios de Windows y del Sistema (`GetWindowsDirectoryPath()`, `GetSystemDirectoryPath()`, `C:\Windows\System32`, `SysWOW64`, `WinSxS` y equivalentes en cualquier volumen).
     - Carpetas de sistema compartidas: `Program Files\Common Files` y `System Volume Information`.
   - **Whitelist Estricta (Límites Autorizados)**:
     - Raíz del binario de VegaFlow y directorio de trabajo del proyecto.
     - Bibliotecas de juegos de Steam (`steamapps\common\...`).
     - Almacenamiento local de aplicaciones del usuario (`%LOCALAPPDATA%`).
     - Carpeta de archivos temporales del sistema (`temp_directory_path`).
     - Carpetas de documentos y partidas del usuario (`Saved Games`, `Documents\My Games`).
     - Lista blanca dinámica (`AddAllowedPath`, `RemoveAllowedPath`, `ClearCustomAllowedPaths`) protegida con `std::mutex`.

3. **Cuarentena y Lockdown Anti-Cheat**:
   - Si se detecta una tecnología de anti-cheat activa a través de `DirectoryContainsAntiCheat` (escaneo recursivo de hasta 4 niveles superiores en el árbol de carpetas) o `PathContainsAntiCheatSignature`, o si se pasa explícitamente el flag `is_anticheat_protected = true`, el sandbox activa un **Lockdown Inmediato**:
   - Se deniegan incondicionalmente las acciones de modificación, copia o borrado (`SandboxAction::Write`, `SandboxAction::Copy`, `SandboxAction::Delete`).
   - Solo se permite la lectura segura de metadatos (`SandboxAction::Read`).

4. **Wrappers Seguros de I/O**:
   - `SafeWriteFile(path, content, is_anticheat_protected)`: Valida la ruta canónica contra la lista negra, whitelist y bloqueo anti-cheat antes de abrir el stream binario.
   - `SafeCopyFile(src, dst, overwrite, is_anticheat_protected)`: Valida el origen para lectura y el destino para copia segura, creando directorios intermedios necesarios.
   - `SafeRemoveFile(path, is_anticheat_protected)`: Verifica límites y protección anti-cheat antes de eliminar archivos o backups temporales.
   - `SafeReadFile(path, out_content)`: Comprueba la validez canónica antes de transferir datos a memoria.

5. **Auditoría y Registro de Violaciones (`SandboxViolationRecord`)**:
   - Cada intento de acceso no autorizado registra un evento con marca de tiempo precisa en milisegundos (`timestamp_ms`), la acción solicitada (`SandboxAction`), el motivo técnico (`SandboxDenialReason`) y un mensaje explicativo para trazabilidad forense.

### 3.2. Subsistema `SystemInfo`: Telemetría Desacoplada y Detección de Hardware
Implementado en [`include/core/system_info.hpp`](include/core/system_info.hpp) y [`src/core/system_info.cpp`](src/core/system_info.cpp), este módulo aísla toda la lógica de recopilación de hardware y sistema operativo en una biblioteca reutilizable:
- **Detección de CPU**:
  - Lectura de marca mediante la instrucción x86 `CPUID` (registros EAX, EBX, ECX, EDX) reportando el procesador completo (e.g. *AMD Ryzen 5 4600H with Radeon Graphics*).
  - Conteo riguroso de núcleos físicos (6) y procesadores lógicos (12) mediante `GetLogicalProcessorInformationEx`.
  - Microarquitectura identificada: *Zen 2 Renoir 7nm*.
  - Tamaño de memoria caché L3 (8 MB / 8,388,608 bytes).
  - Frecuencia objetivo de boost sostenido APU STAPM a **3.4 GHz** (35W-45W).
  - Máscaras y CPU Sets activos para juegos y procesos de fondo.
- **Gráficos & Memoria UMA (Vega 6)**:
  - Enumeración de adaptadores DirectX mediante `IDXGIFactory4` / `IDXGIAdapter3`.
  - Diferenciación explícita entre la falsa "VRAM Dedicada" reportada por BIOS (512 MB) y la memoria UMA compartida real del sistema disponible para la iGPU (~7.7 GB).
  - Consulta en tiempo real de presupuestos de memoria de vídeo local y no local con `QueryVideoMemoryInfo`.
- **Topología de Canales de Memoria RAM (SMBIOS Type 17)**:
  - Lectura directa de las tablas de firmware del sistema mediante `GetSystemFirmwareTable('RSMB', 0, ...)` para inspeccionar los registros SMBIOS Type 17 (Memory Device).
  - Detecta la presencia de 2 ranuras DIMM pobladas en configuración **Dual-Channel con bus de 128 bits** (~42 GB/s de ancho de banda).
  - Desglose instantáneo de páginas físicas: memoria en uso, memoria libre real (Free + Zeroed), Standby List en caché y páginas modificadas.
- **Kernel NT & Sistema Operativo**:
  - Lectura de versión de Windows (e.g. *Windows 11 Build 26300* con Update Build Revision UBR).
  - Telemetría de resolución del temporizador NT en milisegundos (0.50 ms / 5000 unidades de 100ns).
  - Estado del servicio MMCSS para juegos (GPU Priority = 8) y compresión de memoria (MMAgent).
- **Serialización Estándar a JSON**:
  - Función `SystemInfo::ToJson()` que exporta toda la telemetría en un esquema estructurado para interoperabilidad con scripts, frontends web o herramientas CLI externas (`--system-info`).

### 3.3. Arquitectura y Rediseño Visual de la GUI (Clean Slate / Nordic Dark Mode)
Implementado en [`include/gui/gui_window.hpp`](include/gui/gui_window.hpp) y [`src/gui/gui_window.cpp`](src/gui/gui_window.cpp):
- **Cero Dependencias & Ultra Bajo Consumo**: GUI construida enteramente sobre la API pura de Win32/GDI con doble búfer (`BitBlt`), sin marcos pesados (Qt, WPF, Electron). Consume menos de **15 MB de RAM privada** y **0.0% de CPU** en reposo. Al detectar un juego en primer plano, la interfaz suspende sus llamadas gráficas de telemetría para ceder el 100% de los ciclos a la GPU.
- **Paleta Slate / Zinc Neutra ("Clean Slate / Nordic Dark Mode")**:
  - Fondo principal: `#111317` (`RGB(17, 19, 23)`).
  - Barra lateral: `#16191E` (`RGB(22, 25, 30)`).
  - Tarjetas de contenido: `#1B1E24` (`RGB(27, 30, 36)`).
  - Bordes sutiles: `#2B303C` (`RGB(43, 48, 60)`, trazo fino de 1px).
  - Cabeceras de panel: `#20242C` (`RGB(32, 36, 44)`).
  - Jerarquía tipográfica: Títulos `#F3F4F6`, texto de cuerpo `#D1D5DB`, texto atenuado `#9CA3AF`, notas técnicas `#6B7280`.
  - Acentos funcionales sobrios: Azul acero `#3B82F6` (activo), Verde esmeralda `#10B981` (óptimo), Ámbar `#F59E0B` (advertencia), Rojo suave `#EF4444` (crítico), Púrpura/Índigo `#8B5CF6`.
- **Geometría y Espaciados Simétricos**:
  - Márgenes perimetrales de 16px (`padX = 16`, `padY = 16`).
  - Separación entre tarjetas con gutters simétricos de 12px.
  - Divisores horizontales internos de 1px (`CLR_CARD_BORDER`).
- **Subtítulos Técnicos Educativos**:
  - Cada tarjeta y control dispone de descripciones breves que instruyen al usuario sobre el impacto físico de la optimización (aislamiento de CCX para evitar contención de caché L3, supresión del límite de 512 MB de UMA para evitar texturas borrosas en UE4/UE5, temporización a 0.5 ms para neutralizar jitter en llamadas `Sleep()`).
- **Tab 0: Dashboard Central Interactivo**:
  - Proporciona una vista consolidada en tiempo real:
    1. *Procesador & Topología*: Estado de núcleos Zen 2, L3, boost APU STAPM 3.4 GHz y asignación de CPU Sets.
    2. *Gráficos & Memoria UMA*: VRAM BIOS vs UMA compartida y barra de presupuesto DXGI.
    3. *Memoria RAM Dual-Channel*: Barra multi-segmento con desglose en uso / standby / modificada / libre y ancho de banda de 128 bits.
    4. *Kernel NT & SO*: Build de Windows, resolución de temporizador, MMCSS y compresión de memoria.
    5. *Seguridad SandboxGuard*: Indicador de blindaje activo, contador de violaciones y cuarentena anti-cheat.
    6. *Monitor de Juego Activo*: Indicador de primer plano, badge `DIRECTFLIP 0ms ACTIVO` y cajas KPI para AVG FPS, 1% Low, 0.1% Low y desviación estándar.
    7. *Tarjetas de Estado de la Suite 3D*: Sub-panel inferior con 3 tarjetas especializadas:
       - **EcoQoS en 2º Plano**: Procesos de fondo confinados en Efficiency Mode para recuperar 3W-5W de STAPM.
       - **Prioridad I/O**: Estado de `IoPriorityHigh` en el juego e `IoPriorityVeryLow` (0) en procesos de fondo para eliminar stuttering de texturas.
       - **DirectFlip / AutoFlip**: Indicador de derivación directa de swap chain (0ms de latencia, bypass de cola DWM).

### 3.4. Suite Moderna para Juegos 3D (EcoQoS, Jerarquía I/O NT y DirectFlip)
Implementada de forma coordinada en [`src/optimizers/cpu_orchestrator.cpp`](src/optimizers/cpu_orchestrator.cpp), [`src/optimizers/graphics_optimizer.cpp`](src/optimizers/graphics_optimizer.cpp), [`src/main.cpp`](src/main.cpp) y [`src/gui/gui_window.cpp`](src/gui/gui_window.cpp):

1. **Background EcoQoS (Power Throttling en Procesos de Fondo)**:
   - **Explicación técnica y física:** En procesadores con arquitectura APU como el Ryzen 5 4600H, el presupuesto térmico STAPM (Skin Temperature Aware Power Management) está limitado dinámicamente entre 35W y 45W para todo el encapsulado. Cuando procesos secundarios (navegadores web, launchers, Discord, tareas de indexación) ejecutan ráfagas de cómputo en núcleos Zen 2 a voltajes altos, sustraen entre 3W y 5W de este presupuesto unificado. La unidad SMU de AMD se ve forzada a reducir la frecuencia de la GPU Vega 6 a 1100–1200 MHz para mantenerse dentro del umbral térmico.
   - **Mecanismo NT:** VegaFlow utiliza la API `SetProcessInformation` con la estructura `PROCESS_POWER_THROTTLING_STATE` fijando el flag `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`:
     ```cpp
     PROCESS_POWER_THROTTLING_STATE powerThrottling{};
     powerThrottling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
     powerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
     powerThrottling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
     SetProcessInformation(hBg, ProcessPowerThrottling, &powerThrottling, sizeof(powerThrottling));
     ```
   - Al confinar las tareas de fondo a estados de ultra-baja disipación energética, se recuperan entre 3W y 5W del presupuesto STAPM, permitiendo a la iGPU sostener sus **1500 MHz continuos** sin estrangulamiento térmico ni caídas súbitas de framerate. Al cerrar el juego, `CpuOrchestrator::RestoreAll()` revierte las directivas limpiamente.

2. **Jerarquía de Prioridad I/O y Memoria NT**:
   - **Explicación técnica:** Videojuegos modernos de mundo abierto y motores 3D (como Zenless Zone Zero, Genshin Impact y Octopath Traveler II) realizan streaming continuo de modelos 3D y texturas de alta resolución desde el disco SSD NVMe hacia la memoria RAM (UMA) y el buffer de la GPU. Cuando aplicaciones de fondo compiten por el ancho de banda del subsistema de archivos o provocan descarte de memoria, se generan severos parones (stuttering) y frametime spikes de 50 a 150 ms.
   - **Mecanismo NT:** VegaFlow aplica una jerarquía estricta invocando la llamada nativa del kernel:
     ```cpp
     PFN_NtSetInformationProcess pfnNtSetInformationProcess = 
         (PFN_NtSetInformationProcess)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess");
     
     // 1. Proceso del juego 3D: IoPriorityHigh (2) y MemoryPriorityNormal (5)
     ULONG io_high = 2; // IoPriorityHigh
     pfnNtSetInformationProcess(hGame, ProcessIoPriorityInformationClass /* 33 */, &io_high, sizeof(io_high));
     
     // 2. Procesos en segundo plano: IoPriorityVeryLow (0) y MemoryPriorityLow (2)
     ULONG io_very_low = 0; // IoPriorityVeryLow
     pfnNtSetInformationProcess(hBg, ProcessIoPriorityInformationClass /* 33 */, &io_very_low, sizeof(io_very_low));
     ```
   - Esta jerarquía garantiza que las peticiones de lectura del juego tengan prioridad absoluta en la cola de I/O de Windows NT, mientras que los procesos de fondo solo acceden al almacenamiento durante ciclos ociosos (`IoPriorityVeryLow = 0`). Además, `MEMORY_PRIORITY_LOW` (2) en fondo previene que la memoria de trabajo del juego sea desplazada al archivo de paginación, erradicando los micro-parones en streaming de texturas 3D.

3. **DirectFlip & AutoFlip Model Upgrade**:
   - **Explicación técnica:** En modos ventana sin bordes tradicionales, el Desktop Window Manager (DWM) intercepta la swap chain y efectúa una copia intermedia de cada fotograma en una superficie de composición DWM, introduciendo entre 15 y 30 ms de retardo de entrada y stutters por contención de sincronización vertical.
   - **Inyección DXGI:** VegaFlow inyecta directamente en `HKCU\Software\Microsoft\DirectX\UserGpuPreferences`:
     ```ini
     AutoFlipModel=1;DirectFlip=1;TurboSync=1;SwapEffectUpgradeEnable=1;GpuPreference=2;
     ```
   - Esto habilita la promoción directa a **DirectFlip** en Windows 11, desacoplando la swap chain de la cola de copiado de DWM. El motor gráfico proyecta sus fotogramas directamente a la controladora de vídeo, logrando **0ms de latencia de entrada adicional** (equivalente a pantalla completa exclusiva) preservando la comodidad de la multitarea borderless y la compatibilidad con superposiciones de sistema.

### 3.5. Suite de Silicio APU & Blindaje de Memoria de Kernel NT (DisablePagingExecutive & Smart Tessellation 16x)
Implementada en [`src/optimizers/graphics_optimizer.cpp`](src/optimizers/graphics_optimizer.cpp), [`src/main.cpp`](src/main.cpp) y [`src/gui/gui_window.cpp`](src/gui/gui_window.cpp):

1. **Blindaje de Memoria de Kernel NT (`DisablePagingExecutive=1` & `LargeSystemCache=0`)**:
   - **Explicación técnica y física:** En sistemas APU con arquitectura de memoria unificada (UMA), Windows NT por defecto traslada al archivo de paginación (`pagefile.sys`) el código ejecutable de drivers y subsistemas del kernel inactivos temporalmente. Cuando la GPU integrada Vega 6 o el pipeline D3D demanda recursos de inmediato, un fallo de página duro (*hard page fault*) al disco NVMe provoca un congelamiento perceptible (micro-stutter) de 50 a 200 ms.
   - **Mecanismo NT:** VegaFlow configura en `HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management` el valor `DisablePagingExecutive = 1` y `LargeSystemCache = 0`. Esto bloquea de forma inmutable todos los drivers y componentes del executive del kernel en memoria RAM física, impidiendo cualquier descarte a disco y asegurando latencia submilisegundo en las rutinas de interrupción DPC de la GPU.

2. **Limitador Inteligente de Teselación AMD (`Tessellation="16"` & `Tessellation_Option="1"`)**:
   - **Explicación técnica:** La iGPU Radeon RX Vega 6 integra únicamente 6 Compute Units físicas (384 ALUs) y un único *Primitive Engine / Tessellator* de hardware. Títulos modernos o efectos gráficos con teselación excesiva (ej. mallas sobre-teseladas a 64x) saturan el Primitive Engine con micro-triángulos menores a un píxel (*sub-pixel overdraw*), generando un severo cuello de botella de geometría sin ganancia visual apreciable.
   - **Mecanismo Driver AMD:** En la subclave `UMD` del driver de vídeo, VegaFlow establece `Tessellation = "16"` y `Tessellation_Option = "1"`. Esto restringe la teselación máxima a **16x**, eliminando el sub-pixel overdraw, descongestionando los CUs de Vega 6 y estabilizando notablemente el frametime y los 1% Lows.

---

## 4. Estructura del Repositorio y Módulos de Código

```
direct/
├── CMakeLists.txt                # Configuración de compilación para CMake 3.20+
├── .gitignore                    # Reglas de exclusión para Git (artefactos, temporales, logs)
├── config/
│   └── config.json               # Configuración persistente del daemon y lista de juegos
├── assets/
│   └── dxvk/dxvk-2.5.3/          # Binarios originales DXVK-GPLAsync (x32 y x64)
├── scripts/
│   └── build.ps1                 # Script automatizado de compilación y pruebas PowerShell
├── include/
│   ├── core/
│   │   ├── nt_defs.hpp           # Typedefs y llamadas nativas indocumentadas de NTDLL
│   │   ├── privileges.hpp        # Gestión de privilegios NT y elevación UAC
│   │   ├── hardware_profiler.hpp # Análisis de hardware universal y topología de CPU
│   │   ├── system_info.hpp       # Subtema modular SystemInfo (hardware, UMA, SMBIOS RAM, OS y JSON)
│   │   └── process_detector.hpp  # Detección no invasiva de juegos y APIs (DX11/12/VK)
│   ├── optimizers/
│   │   ├── graphics_optimizer.hpp# Flip Model, Timer NT (0.5ms), MMCSS, MPO y Tweaks
│   │   ├── memory_manager.hpp    # Telemetría de RAM/VRAM y purga de Standby List
│   │   ├── cpu_orchestrator.hpp  # Asignación de CPU Sets y aislamiento de procesos
│   │   ├── fsr_scaler.hpp        # Escalador espacial zero-hook (FSR 1.0, NIS, Anime4K)
│   │   ├── steam_engine_optimizer.hpp # Detección de juegos Steam, motores (UE/Unity) y presets
│   │   └── dxvk_manager.hpp      # Despliegue seguro de DXVK-GPLAsync y backups
│   ├── security/
│   │   ├── sandbox_guard.hpp     # Aislamiento Zero-Trust, normalización canónica, whitelist y cuarentena anti-cheat
│   │   └── frame_telemetry.hpp   # Monitoreo estadístico de frametimes (1% y 0.1% Lows)
│   └── gui/
│       ├── gui_window.hpp        # Interfaz gráfica moderna Win32/GDI Slate/Zinc y System Tray
│       └── gui_components.hpp    # Componentes modulares de UI y renderizado gráfico
├── src/
│   ├── main.cpp                  # Punto de entrada del daemon, CLI handler y bucle
│   ├── core/
│   │   ├── privileges.cpp
│   │   ├── hardware_profiler.cpp
│   │   ├── process_detector.cpp
│   │   └── system_info.cpp
│   ├── optimizers/
│   │   ├── graphics_optimizer.cpp
│   │   ├── memory_manager.cpp
│   │   ├── cpu_orchestrator.cpp
│   │   ├── fsr_scaler.cpp
│   │   ├── steam_engine_optimizer.cpp
│   │   └── dxvk_manager.cpp
│   ├── security/
│   │   ├── sandbox_guard.cpp
│   │   └── frame_telemetry.cpp
│   └── gui/
│       ├── gui_window.cpp
│       ├── gui_components.cpp
│       └── gui_tabs.cpp
├── tests/
│   └── test_main.cpp             # Suite de 26 pruebas unitarias e integradas
├── logs/
│   └── session_reports.log       # Registro persistente de sesiones y telemetría
└── build/
    ├── vegaflow_daemon.exe       # Ejecutable principal
    └── vegaflow_tests.exe        # Ejecutable de verificación de pruebas
```

---

## 5. Compilación, Enlace y Verificación de Pruebas

VegaFlow Universal está escrito en **C++20 estándar** con enlace estático en Windows, sin dependencias pesadas de runtime externas (MSVCRT/VCRedist no requeridos con MinGW).

### 5.1. Toolchain Recomendada
- **Compilador:** GCC 13+ / 16.2.0 (MinGW-w64) con soporte C++20 completo.
- **Librerías del Sistema Requeridas:**
  `-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh`

### 5.2. Compilación Automatizada (PowerShell)

El proyecto incluye el script automatizado [`scripts/build.ps1`](scripts/build.ps1), el cual detecta automáticamente MinGW g++ (en PATH o en Scoop), gestiona las rutas modulares y permite compilar ambos ejecutables y opcionalmente ejecutar la suite de pruebas:

```powershell
# Compilar vegaflow_daemon.exe y vegaflow_tests.exe:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# Compilar y ejecutar inmediatamente los 26 tests unitarios:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```

### 5.3. Compilación Manual con MinGW G++

#### Compilar el Daemon de Fondo (`vegaflow_daemon.exe`):
```bash
g++ -std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ \
    -Iinclude -Iinclude/core -Iinclude/optimizers -Iinclude/security -Iinclude/gui \
    src/core/privileges.cpp \
    src/core/hardware_profiler.cpp \
    src/core/process_detector.cpp \
    src/core/system_info.cpp \
    src/optimizers/graphics_optimizer.cpp \
    src/optimizers/dxvk_manager.cpp \
    src/optimizers/steam_engine_optimizer.cpp \
    src/optimizers/fsr_scaler.cpp \
    src/optimizers/cpu_orchestrator.cpp \
    src/optimizers/memory_manager.cpp \
    src/security/sandbox_guard.cpp \
    src/security/frame_telemetry.cpp \
    src/gui/gui_components.cpp \
    src/gui/gui_tabs.cpp \
    src/gui/gui_window.cpp \
    src/main.cpp \
    -o build/vegaflow_daemon.exe \
    -lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh
```

#### Compilar la Suite de Pruebas Unitarias (`vegaflow_tests.exe`):
```bash
g++ -std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ \
    -Iinclude -Iinclude/core -Iinclude/optimizers -Iinclude/security -Iinclude/gui \
    src/core/privileges.cpp \
    src/core/hardware_profiler.cpp \
    src/core/process_detector.cpp \
    src/core/system_info.cpp \
    src/optimizers/graphics_optimizer.cpp \
    src/optimizers/dxvk_manager.cpp \
    src/optimizers/steam_engine_optimizer.cpp \
    src/optimizers/fsr_scaler.cpp \
    src/optimizers/cpu_orchestrator.cpp \
    src/optimizers/memory_manager.cpp \
    src/security/sandbox_guard.cpp \
    src/security/frame_telemetry.cpp \
    src/gui/gui_components.cpp \
    src/gui/gui_tabs.cpp \
    src/gui/gui_window.cpp \
    tests/test_main.cpp \
    -o build/vegaflow_tests.exe \
    -lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh
```

---

## 6. La Suite de Pruebas Integrada (Obligatoria para Todo Commit)

Cualquier cambio en el repositorio debe ser validado ejecutando `build/vegaflow_tests.exe`. Todos los tests deben pasar en verde sin excepciones:

```
[PASS] Test 1: PrivilegeManager elevation check and privilege adjustment
[PASS] Test 2: HardwareProfiler universal hardware & CPU set detection
[PASS] Test 3: MemoryManager telemetry, dry-run optimization, and Zero-Overhead KPI (< 15 MB)
[PASS] Test 4: ProcessDetector anti-cheat safe inspection, process filtering, and API mapping
[PASS] Test 5: GraphicsOptimizer timer resolution manipulation, shader cache inspection, and DXGI flip upgrade dry-run
[PASS] Test 6: CpuOrchestrator dry-run optimization and state restoration
[PASS] Test 7: FrameTelemetryMonitor mathematical rigor, 1% Low, circular buffer, and shutdown latency
[PASS] Test 8: Phase 1-2 Enhancements & System Tweaks (Admin relaunch, process liveness, MPO, manual purge)
[PASS] Test 9: FSR 1.0 Spatial Upscaler HLSL Compilation & Zero-VRAM Lifecycle
[PASS] Test 10: Phase 4 System Tweaks & Session Report Persistence
[PASS] Test 11: Stability, Memory Stress & CpuOrchestrator Deduplication
[PASS] Test 12: FSR 1.0 Dynamic Sharpness Update & Value Clamping
[PASS] Test 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore
[PASS] Test 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync
[PASS] Test 15: DX11/DX12 FPS Boost Suite (MMCSS GPU Priority=8, Win32PrioritySeparation Quantum & AMD Driver Boost Dry-Run)
[PASS] Test 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)
[PASS] Test 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O
[PASS] Test 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization
[PASS] Test 19: Modern 3D Gaming Suite (EcoQoS Background Throttling, I/O Priority Hierarchy, DirectFlip & Low-Latency D3D11 Queues)
[PASS] Test 20: APU Silicon & UMA Bus Optimization Suite (DisablePagingExecutive Kernel Shield & Smart Tessellation 16x)
[PASS] Test 21: MMCSS Audio & Gaming Priority Shield
[PASS] Test 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming
[PASS] Test 23: Memory Manager Standby List Purge & UMA Bus Prioritizer
[PASS] Test 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)
[PASS] Test 25: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards
[PASS] Test 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync

================================================================================
TEST RESULTS: 26 PASSED, 0 FAILED
================================================================================
```

---

## 7. Instrucciones para Asistentes y Desarrolladores

1. **Nunca omitir verificaciones en seco (dry_run):** Todas las funciones que modifican el sistema (`ApplyEngineUmaPreset`, `DeployDxvk`, `SetMpoDisabled`, etc.) deben disponer de un parámetro `dry_run = false` para pruebas unitarias sin impacto real.
2. **Preservar el Zero-Overhead:** No añadir dependencias de frameworks masivos (Qt, WxWidgets, Boost). La GUI debe seguir siendo puramente Win32/GDI nativa para garantizar `< 15 MB` de consumo de RAM.
3. **Manejo de rutas con espacios y Unicode:** Usar siempre `std::wstring` para interactuar con la API Win32 y convertir limpiamente con `Utf8ToWide` / `WideToUtf8`.
4. **Respaldar antes de escribir:** Cualquier modificación a un archivo `.ini`, `.config` o DLL debe ir precedida de la creación de un archivo `.vegaflow.bak` o `.original.bak`.

---

## 8. Hoja de Ruta y Enfoque Actual de Ingeniería

El desarrollo activo de VegaFlow Universal se rige por el documento índice maestro [`docs/ROADMAP_FOCUS.md`](docs/ROADMAP_FOCUS.md) y la especificación de diseño [`docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md`](docs/superpowers/specs/2026-11-01-apu-dx11-dx12-universal-guards-design.md).

### 8.1. Base Consolidada (`26/26` Pruebas en Verde)
- **Modularidad Integral:** Arquitectura desacoplada en cuatro subdominios (`core/`, `optimizers/`, `security/`, `gui/`) tanto en `include/` como en `src/`, con compilación automatizada en [`scripts/build.ps1`](scripts/build.ps1).
- **Motor `ApuCapabilityGuard`:** Clasificación en 4 Tiers con políticas acotadas:
  - *Tier 1 (`Survival Guard`):* APUs $\le$ 8–9 GB RAM o $\le$ 4 núcleos (protección OOM en Unreal Engine, teselación a 8x, DXVK a 3 hilos).
  - *Tier 2 (`Sweet-Spot Guard`):* APUs Vega / GCN 5 (Ryzen 5 4600H, 16 GB Dual-Channel, 4096 MB VRAM pool, 6 hilos DXVK, teselación a 16x).
  - *Tier 3 (`Do-No-Harm Guard`):* APUs RDNA 2 / RDNA 3 / RDNA 3.5 e Intel Xe/Arc (protección de `D3D12.AsyncCompute=1`, sin limitación artificial de frecuencia de CPU, pool UMA de 6 a 8 GB).
  - *Tier 4 (`Pass-Through Guard`):* GPUs dedicadas (>2 GB VRAM dedicada).
- **Runtime DXGI Pressure Guard:** Intervención reactiva al rebasar el 85% del presupuesto local de vídeo DXGI o cuando la memoria libre desciende de 1536 MB.
- **Aislamiento `SandboxGuard`:** Validación canónica de rutas, whitelist estricta y cuarentena total ante firmas de anti-cheat a nivel de kernel.

### 8.2. Próximos 4 Pilares de Enfoque
1. **Pilar 1 — Integración Visual del `ApuCapabilityGuard` en la GUI ([`src/gui/`](src/gui)):** Representación gráfica del Tier detectado (`Tier 1..4`), insignia cromática del Guard activo (*Survival / Sweet-Spot / Do-No-Harm*), barra dinámica de presupuesto DXGI 1.4 con alerta al superar el 85% y contador de intervenciones reactivas.
2. **Pilar 2 — Perfiles Dinámicos por Juego y Detección Automática de Cuello de Botella:** Identificación en tiempo real de estados *CPU-Bound* vs *GPU-Bound* en `FrameTelemetryMonitor`, modulando dinámicamente el factor de escalado espacial FSR 1.0 / NIS y la agresividad de `EcoQoS` en procesos secundarios.
3. **Pilar 3 — Topología Híbrida para APUs de Nueva Generación ([`src/optimizers/cpu_orchestrator.cpp`](src/optimizers/cpu_orchestrator.cpp)):** Particionamiento avanzado de CPU Sets para procesadores heterogéneos (Zen 5 + Zen 5c en AMD Strix Point; P-Cores + E-Cores en Intel Core Ultra), confinando los hilos de renderizado D3D/DXVK exclusivamente en los núcleos de alta frecuencia con caché L3 completa.
4. **Pilar 4 — Telemetría In-Game Zero-Hook sobre DirectFlip:** Superposición ultraligera tipo *click-through* (`< 0.2 ms` de coste de renderizado) para visualizar frametime, 1% Lows, presupuesto UMA y estado del Guard, **100% inmune a baneos** al no inyectar DLLs en juegos con protección anti-cheat.

---

## 9. Estado de verificación (2026-11-04)

Las secciones anteriores describen lo que el código hace y la intención de cada optimización; no todo está medido. Medido con `scripts/bench.ps1` (PresentMon) y A/B intercalado ABBA; detalle en [`docs/MEDICION_AB_DAEMON.md`](docs/MEDICION_AB_DAEMON.md) y [`docs/MEDICION_BASE.md`](docs/MEDICION_BASE.md).

- **Quitado por costar rendimiento medido:** `CPMINCORES=100` en el perfil APU (-21% en `drawstress` D3D11) y confinar al juego a 8 de 12 hilos (entre -19% y -24%; ahora es opción `restrict_game_cpu_sets`, apagada por defecto).
- **Sin efecto medible en `drawstress`:** `Win32PrioritySeparation`, PCIe ASPM, EPP y el modo de boost (≈1%). El temporizador NT de 0.5 ms pedido por otro proceso no cambia `Sleep(1)` de un tercero.
- **Sin medición propia todavía:** EcoQoS y prioridad de I/O del fondo (A/B con carga de fondo en curso), DirectFlip y MPO (el juego medido ya presentaba en `Legacy Flip` o `Independent Flip`), `DisablePagingExecutive`, teselación 16x, AsyncCompute desactivado, DXVK-GPLAsync, FSR y purga de standby.
- **Cifras de las secciones 2.5 y 3.4** (12-14 W de CPU, 3.2 GHz, 1500 MHz sostenidos, 3-5 W recuperados): no están medidas en este repositorio.
- **Reglas de medición:** el juego debe abrirse en el escritorio (sesión 1), no por SSH (sesión 0: PresentMon no ve cuadros y el detector del daemon no ve ningún juego); usar `scripts/ab_loop_task.ps1` y descartar los primeros minutos tras un reinicio; leer el estado con `scripts/tweaks_state.ps1` antes de medir.
- **Cuidados conocidos:** la suite de pruebas ejecuta cambios reales y deja el equipo modificado; los restauradores escriben valores fijos, no los previos; en Windows en español usar SID en `icacls` y `WindowsBuiltInRole::Administrator`, porque los nombres en inglés fallan sin avisar.
- **Herramienta de desarrollo remoto:** [`scripts/remote/`](scripts/remote/README.md), que no forma parte del producto.
