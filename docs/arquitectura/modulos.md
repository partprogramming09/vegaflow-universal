# Módulos y estructura del repositorio

### 3.2. Subsistema `SystemInfo`: Telemetría Desacoplada y Detección de Hardware
Implementado en [`src/session/system_info.hpp`](../../src/session/system_info.hpp) y [`src/session/system_info.cpp`](../../src/session/system_info.cpp), este módulo aísla toda la lógica de recopilación de hardware y sistema operativo en una biblioteca reutilizable:
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
Implementado en [`src/ui/gui_window.hpp`](../../src/ui/gui_window.hpp) y [`src/ui/gui_window.cpp`](../../src/ui/gui_window.cpp):
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
Implementada de forma coordinada en [`src/optimize/cpu_orchestrator.cpp`](../../src/optimize/cpu_orchestrator.cpp), [`src/optimize/graphics/`](../../src/optimize/graphics/), [`src/app/main.cpp`](../../src/app/main.cpp) y [`src/ui/gui_window.cpp`](../../src/ui/gui_window.cpp):

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
Implementada en [`src/optimize/graphics/`](../../src/optimize/graphics/), [`src/app/main.cpp`](../../src/app/main.cpp) y [`src/ui/gui_window.cpp`](../../src/ui/gui_window.cpp):

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
├── .gitignore                    # Reglas de exclusión para Git (artefactos, temporales, logs)
├── config/
│   └── config.json               # Configuración persistente del daemon y lista de juegos
├── assets/
│   └── dxvk/                     # Aquí se colocan las DLL de DXVK (no se incluyen; ver su README)
├── scripts/
│   └── build.ps1                 # Script automatizado de compilación y pruebas PowerShell
├── rust/
│   └── vegaflow-core/            # Núcleo híbrido Rust de alta seguridad y zero-allocation FFI
│       ├── Cargo.toml            # staticlib/rlib con LTO y panic = unwind (ffi_guard captura los pánicos)
│       └── src/
│           ├── lib.rs            # Declara los módulos y las pruebas de la frontera FFI
│           ├── ffi.rs            # Todas las funciones vf_rust_* exportadas a C++ (punteros y ffi_guard)
│           ├── nt_memory.rs      # Llamadas a ntdll: leer las listas de memoria y descartar la Standby List
│           └── policy/           # Las reglas, una por archivo, sin punteros ni llamadas al sistema
│               ├── apu_tier.rs     # Tier de la APU (1 a 4)
│               ├── cpu_governor.rs # CPU Sets según topología L3 / CCX e histéresis del confinamiento
│               ├── path_policy.rs  # Lista negra de rutas de SandboxGuard
│               ├── sandbox_core.rs # Firmas de anti-cheat y archivos críticos del sistema
│               └── uma_memory.rs   # Presión de memoria UMA
├── src/                          # Cada .hpp vive junto a su .cpp, agrupado por función
│   ├── app/                      # Entradas del programa (main.cpp solo orquesta)
│   │   ├── main.cpp              # Punto de entrada: lee la línea de comandos y elige el modo
│   │   ├── cli.*                 # Banderas y órdenes de un solo uso (--status, --purge-now, ...)
│   │   ├── daemon_loop.*         # Modo por defecto: bucle del daemon y apagado limpio
│   │   ├── gui_mode.*            # --gui: ventana nativa con bandeja
│   │   ├── monitor.*             # --monitor: panel de consola en vivo
│   │   └── app_state.*           # Estado compartido y manejador de Ctrl+C
│   ├── session/                  # Casos de uso: lo que hace el daemon con un juego
│   │   ├── run_cycle.*           # Un ciclo de evaluación: detectar juego, aplicar o soltar
│   │   ├── reports.*             # --status, --dry-run y --purge-now
│   │   ├── system_info.*         # Informe SystemInfo (hardware, UMA, SMBIOS RAM, OS y JSON)
│   │   ├── config.*              # config.json y lista de juegos
│   │   └── text_convert.*        # UTF-8 <-> UTF-16
│   ├── model/                    # Tipos compartidos, sin lógica
│   │   ├── hardware.hpp          # GPU, CPU, Tier de la APU y su política
│   │   └── game.hpp              # Juego detectado y su API gráfica
│   ├── detect/                   # Qué hardware y qué juego hay
│   │   ├── hardware_profiler.*   # Análisis de hardware universal y topología de CPU
│   │   └── process_detector.*    # Detección no invasiva de juegos y APIs (DX11/12/VK)
│   ├── optimize/                 # Las optimizaciones
│   │   ├── graphics/             # Ajustes de Windows y del driver (un tema por archivo)
│   │   │   ├── graphics_optimizer.hpp   # API pública (clase GraphicsOptimizer)
│   │   │   ├── timer.cpp                # Resolución del temporizador NT (0.5 ms)
│   │   │   ├── dxgi_flip.cpp            # Flip Model y DirectFlip por juego
│   │   │   ├── tweaks_query.cpp         # Lectura del estado de todos los ajustes
│   │   │   ├── kernel_tweaks.cpp        # Compresión de memoria, prioridad y paging shield
│   │   │   ├── display_tweaks.cpp       # MPO, GameDVR y pantalla completa
│   │   │   ├── mmcss.cpp                # Perfil MMCSS de juegos
│   │   │   ├── amd_driver_tweaks.cpp    # Teselación y boost del driver AMD
│   │   │   ├── power_mode.cpp           # Perfil de energía de la APU
│   │   │   ├── vram_reporting.cpp       # Reporte de VRAM de la UMA
│   │   │   ├── shader_cache.cpp         # Caché de shaders
│   │   │   ├── registry_backup.cpp      # Copia y restauración de valores del registro
│   │   │   └── graphics_internal.hpp    # Ayudantes compartidos (no es API pública)
│   │   ├── steam/                # Biblioteca de Steam y presets por motor
│   │   │   ├── steam_engine_optimizer.hpp  # API pública
│   │   │   ├── steam_library.cpp       # Instalación de Steam y escaneo de juegos
│   │   │   ├── engine_detection.cpp    # Qué motor usa cada juego
│   │   │   ├── engine_presets.cpp      # Aplicar y restaurar presets de UE/Unity
│   │   │   ├── apu_shadow_config.cpp   # Configuración "sombra" para APU
│   │   │   └── steam_internal.*        # Ayudantes compartidos
│   │   ├── memory_manager.*      # Telemetría de RAM/VRAM y purga de Standby List
│   │   ├── cpu_orchestrator.*    # Asignación de CPU Sets y aislamiento de procesos
│   │   └── dxvk_manager.*        # Despliegue seguro de DXVK-GPLAsync y backups
│   ├── safety/                   # Lo que protege al sistema y al juego
│   │   ├── sandbox_guard.*       # Aislamiento Zero-Trust, normalización canónica, whitelist y cuarentena anti-cheat
│   │   └── frame_telemetry.*     # Monitoreo estadístico de frametimes (1% y 0.1% Lows)
│   ├── ui/                       # Ventana nativa Win32/GDI
│   │   ├── gui_window.*          # Interfaz Slate/Zinc y System Tray
│   │   ├── gui_components.*      # Componentes modulares de UI y renderizado gráfico
│   │   └── gui_tabs.cpp          # Contenido de cada pestaña
│   ├── windows/                  # Envolturas de la API de Windows
│   │   ├── nt_defs.hpp           # Typedefs y llamadas nativas indocumentadas de NTDLL
│   │   └── privileges.*          # Gestión de privilegios NT y elevación UAC
│   └── bridge/
│       └── rust_bridge.h         # Definición de tipos y firmas FFI C/Rust (Zero-Allocation)
├── tests/
│   └── test_main.cpp             # Suite de 32 pruebas unitarias e integradas (C++20 + Rust FFI)
├── logs/
│   └── session_reports.log       # Registro persistente de sesiones y telemetría
└── build/
    ├── vegaflow_daemon.exe       # Ejecutable principal
    └── vegaflow_tests.exe        # Ejecutable de verificación de pruebas
```

---

