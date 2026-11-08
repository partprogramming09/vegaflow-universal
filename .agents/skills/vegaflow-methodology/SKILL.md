---
name: vegaflow-methodology
description: Use when developing, modifying, debugging, or reviewing architecture, optimizations, kernel tweaks, or APU/hardware policies in VegaFlow Universal. Covers AMD APU microarchitecture, UMA memory constraints, zero-overhead daemon invariants (<15MB RAM, 0% CPU idle), native Windows NT APIs, strict Anti-Cheat safety, reversible operations, and empirical test validation.
---

# Metodología y Guía de Ingeniería: VegaFlow Universal

Esta skill es la guía de referencia técnica y metodológica para agentes de IA e ingenieros de software que desarrollen, depuren o extiendan **VegaFlow Universal**. Define los principios de ingeniería de sistemas, arquitectura de hardware APU/UMA, estándares de seguridad anti-cheat y protocolos de validación empírica del proyecto.

---

## 1. Filosofía de Desarrollo y Principios de Ingeniería

VegaFlow Universal se rige por cuatro pilares inquebrantables:

1. **Rigor Técnico y Matemático**: Ninguna optimización es un "truco mágico" ni una copia ciega de foros. Cada ajuste se fundamenta en especificaciones oficiales de Microsoft Windows Internals (Windows NT Architecture), directivas de AMD SMU/WDDM y estándares oficiales DirectX 11/12 y Vulkan.
2. **Cero Overhead en Reposo**:
   - Consumo de memoria privada (`Private Working Set`): **< 15.0 MB** en todo momento.
   - Uso de CPU en reposo: **0.0%**. Prohibido estrictamente cualquier bucle de sondeo activo (*busy-wait*, *polling* agresivo de CPU o *sleep* en bucle). Se debe emplear exclusivamente `CreateWaitableTimerExW` con temporizador de alta resolución (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) señalizado por eventos NT.
3. **Reversibilidad Total y Cero Daño**:
   - Todo cambio en el Registro de Windows o en archivos de configuración de motores (e.g. `Engine.ini`) debe generar una copia de seguridad automática (`.vegaflow.bak` o `.bak`).
   - Cada optimización cuenta con una función espejo de restauración (`Restore...()`) que restablece los valores originales sin dejar residuos.
4. **Seguridad Absoluta Frente a Anti-Cheat**:
   - **Regla Inviolable**: Jamás inyectar DLLs, ganchos (*hooks*), ni modificar archivos en directorios de juegos protegidos por sistemas Anti-Cheat a nivel kernel o usuario (Easy Anti-Cheat, BattlEye, HoYoKProtect, Vanguard, ACE, mhypbase, etc.).
   - Toda interacción con procesos protegidos se realiza mediante APIs no invasivas a nivel de sistema operativo (DXGI Flip Model en registro, asignación cooperativa de `CPU Sets`, resolución del temporizador NT y jerarquía de I/O NT).

---

## 2. Microarquitectura de Hardware APU y Restricciones UMA

### 2.1. El Cuello de Botella del Bus UMA (Unified Memory Architecture)
A diferencia de GPUs discretas con bus dedicado de 256 bits y 300–1000 GB/s (GDDR6), las APUs (como AMD Ryzen 5 4600H con Radeon Vega 6) comparten la memoria RAM del sistema mediante un bus DDR4/DDR5 de 128 bits dual-channel (~38–42 GB/s en DDR4-3200).
- **Problema de Contención**: La CPU y la iGPU compiten continuamente por el mismo ancho de banda. Si procesos en segundo plano realizan lecturas masivas o Windows activa la compresión de memoria (*MemCompression*), la tasa de llenado de la GPU colapsa, provocando caídas de 1% Lows y *frametime stutter*.
- **Directiva VegaFlow**:
  - Aislar procesos secundarios en núcleos exclusivos (`background_cpu_set_ids`).
  - Aplicar `ProcessPowerThrottling` (`EcoQoS`) a procesos en segundo plano.
  - Temporizar purgas de lista de espera (*Standby List*) únicamente cuando la actividad de I/O de disco sea baja (< 15 MB/s) para evitar *disk paging spikes*.

### 2.2. Falso Límite de 512 MB de VRAM y Texture Streaming
Por compatibilidad con sistemas legados, el controlador de vídeo reporta 512 MB de VRAM dedicada en `DXGI_ADAPTER_DESC::DedicatedVideoMemory`. Motores como Unreal Engine 4/5 limitan el banco de texturas (`r.Streaming.PoolSize`) a 512 MB, produciendo *texture pop-in* severo.
- **Solución VegaFlow**: Sincronizar dinámicamente el pool de texturas con la memoria real compartida mediante `ApuOptimizationPolicy::safe_vram_pool_mb` (típicamente 4096 MB en sistemas de 16 GB), anulando el límite con `r.Streaming.LimitPoolSizeToVRAM=0`.

### 2.3. Carencia de GPL (`VK_EXT_graphics_pipeline_library`) en AMD Vega (GCN 5.0)
Los drivers AMD Adrenalin para arquitectura Vega no implementan GPL nativo en Vulkan, provocando congelamientos al compilar shaders en tiempo real.
- **Solución VegaFlow**: Integrar la capa `DXVK-GPLAsync` configurando 6 hilos dedicados de compilación asíncrona en segundo plano (`dxvk.numCompilerThreads = 6`), eliminando los parones de compilación sin comprometer la fluidez.

### 2.4. Inviabilidad de `AsyncCompute` en iGPUs Pequeñas ($\le$ 8 CUs)
En procesadores integrados con 6 a 8 Compute Units (ej. Vega 6 con 384 Stream Processors), el cómputo asíncrono sobrecarga los planificadores de hardware e introduce latencias de cambio de contexto.
- **Directiva**: Desactivar explícitamente `AsyncCompute` en Tier 1 y Tier 2 (`r.RDG.AsyncCompute=0`, `D3D12.AsyncCompute=0`).
- **Excepción "Do-No-Harm"**: En APUs modernas RDNA2/3/3.5 (Tier 3, ej. Radeon 680M/780M con 12 CUs), **preservar** el cómputo asíncrono activo (`D3D12.AsyncCompute=1`).

### 2.5. Smart Uncapped Boost (100% CPU + 100% GPU)
Para evitar el estrangulamiento térmico mutuo de 35W–45W entre CPU y GPU en portátiles sin frenar artificialmente la frecuencia:
- **No limitar MHz**: `policy.cpu_stapm_cap_mhz = 0;` (100% CPU hasta 4.0 GHz).
- **Curva SMU Eficiente**: Configurar el modo boost en `Efficient Aggressive` (`PERFBOOSTMODE = 4`).
- **Preferencia Energética SMU**: Establecer `PERFEPP = 25` vía `GUID_PROCESSOR_ENERGY_PERF_PREFERENCE` (`36687f9e-e3a5-4dbf-b1dc-15eb381c6863`), otorgando respuesta instantánea en hilos del juego.
- **PCIe ASPM Deshabilitado**: `GUID_PCIEXPRESS_ASPM = 0` para eliminar la latencia de despertar en el bus Infinity Fabric.
- **Ahorro Térmico Real**: El margen térmico se obtiene reduciendo el consumo de los núcleos de fondo con EcoQoS y contención de CPU Sets, no limitando el reloj del juego.

---

## 3. Partición Topológica de Núcleos e Islas L3 (`HardwareProfiler::PartitionCpuSetsFromTopology`)

La afinidad de núcleos debe ser inteligente y consciente de la topología de caché L3 y la arquitectura de núcleos:

```
[AMD Ryzen 5 4600H: 2x CCX Simétricos de 3C/6T (8 MB L3 por CCX)]
+-----------------------------------+   +-----------------------------------+
|         CCX 0 (Isla L3 #0)        |   |         CCX 1 (Isla L3 #1)        |
|  Core 0 (DPC/ISR) | Core 1 | Core 2 |   |  Core 3 | Core 4 | Core 5 (Fondo)   |
|  [256, 257]       | [258..261]     |   |  [262..265]    | [266, 267]       |
+-----------------------------------+   +-----------------------------------+
       ^                                                         ^
       |                                                         |
  Escudo DPC/ISR                                       Aislamiento de Fondo
  (Excluido del juego)                                (Navegadores, Discord, etc.)
```

1. **Escudo DPC e Interrupciones (Core 0)**:
   - Si el procesador cuenta con $\ge$ 4 núcleos físicos, el `Core 0` se excluye de `game_optimal_cpu_set_ids` para proteger el juego de la latencia provocada por interrupciones de hardware (DPC/ISR), controladores de red y subsistema de almacenamiento.
2. **Topología Híbrida (P-Cores vs E-Cores)**:
   - Núcleos con `efficiency_class == max_eff` se asignan al juego.
   - Núcleos de menor eficiencia se asignan a `background_cpu_set_ids`.
3. **Islas L3 Asimétricas (Zen 4 + Zen 4c / Zen 5 + Zen 5c)**:
   - Si la isla secundaria posee $\le$ 33% de los núcleos totales, se aísla por completo para tareas de fondo.
4. **Islas L3 Simétricas (Multi-CCX, ej. Ryzen 5 4600H)**:
   - Se preserva el CCX principal intacto para el juego.
   - Se toma el último núcleo físico del CCX secundario (Core 5) para tareas de fondo, asignando 8 hilos al juego con mínima latencia inter-CCX.

---

## 4. Motor de Clasificación Universal de 4 Niveles (`ApuTier`)

Nunca asumas parámetros fijos. Toda decisión de optimización debe consultar `ApuOptimizationPolicy`:

| Nivel APU | Perfil de Seguridad | VRAM Pool Seguro | Hilos DXVK | Teselación | AsyncCompute | Tope STAPM MHz |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Tier 1: Entry UMA** | Survival Guard | 1536–2048 MB | 3 hilos | 8x | Desactivado | 0 (Uncapped Smart Boost) |
| **Tier 2: Mid Vega UMA** | Sweet-Spot Guard | 4096 MB | 6 hilos | 16x | Desactivado | 0 (Uncapped Smart Boost) |
| **Tier 3: Advanced UMA** | Do-No-Harm Guard | 6144–8192 MB | 6–8 hilos | 32x | **Activado** | 0 (Sin límites) |
| **Tier 4: Discreta / Custom** | Pass-Through Guard | Driver Default | 8 hilos | Nativo | Nativo | 0 (Sin límites) |

### Runtime DXGI Pressure Guard (`MemoryManager::EvaluateUmaPressureGuard`):
Supervisa en caliente el uso del presupuesto de memoria de vídeo local vía `IDXGIAdapter3::QueryVideoMemoryInfo`. Si:
- `dxgi_budget_utilization >= 0.85` (85%), o
- `free_and_zero_bytes < 1536 MB`,
activa el **Guard Preventivo**: recorta de inmediato el *working set* de aplicaciones en segundo plano y purga la lista de espera para prevenir congelamientos por paginación o cierres OOM.

---

## 5. Subsistema de Seguridad `SandboxGuard`

Toda manipulación de rutas o archivos debe pasar por `SandboxGuard`:

1. **Resolución Canónica**: Prohibido usar rutas relativas o sin validar. Utilizar `SandboxGuard::CanonicalizePath()` para desenrollar enlaces simbólicos (*symlinks*), *junctions* y secuencias `..`.
2. **Lista Negra del Sistema**: Denegar tajantemente cualquier acceso a:
   - Raíces de disco (`C:\`, `D:\`).
   - Directorios de Windows (`C:\Windows`, `System32`, `SysWOW64`).
   - ProgramData o directorios de arranque de Windows.
3. **Escudo Anti-Cheat**: Si `SandboxGuard::IsGameAntiCheatProtected(path)` devuelve verdadero, **todas las operaciones de escritura o despliegue de DLLs quedan vetadas**. Solo se permite la ruta no invasiva.

---

## 6. APIs Nativas de Windows NT Recomendadas

Al interactuar con el sistema operativo, prefiere siempre APIs nativas frente a utilidades externas:

- **Temporizador de Alta Precisión (0.5 ms)**:
  `NtSetTimerResolution(5000, TRUE, &current_res)` (vía `ntdll.dll`).
- **Asignación de CPU Sets Cooperativos**:
  `SetProcessDefaultCpuSets(hProcess, cpu_set_ids.data(), count)`.
- **Throttling Eficiente (EcoQoS)**:
  `SetProcessInformation(hProcess, ProcessPowerThrottling, &power_throttling, sizeof(power_throttling))`.
- **Telemetría de Presupuesto VRAM**:
  `IDXGIAdapter3::QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mem_info)`.
- **Esquemas de Energía de Windows**:
  `PowerWriteACValueIndex` / `PowerWriteDCValueIndex` con `powrprof.dll`.

---

## 7. Flujo de Trabajo y Validación de Cambios (TDD & Verificación)

Cada cambio en VegaFlow Universal debe cumplir con el siguiente protocolo estricto:

### 1. Compilación
VegaFlow utiliza MinGW-w64 (`g++` 16.x) bajo un entorno Windows. Dado que el compilador reside en el perfil de usuario, compila siempre ejecutando:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1
```

### 2. Ejecución de la Suite de Pruebas Unitarias
El proyecto contiene 33 tests unitarios rigurosos que validan todas las invariantes matemáticas y de seguridad:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```
**Criterio de Aceptación Innegociable**: El resultado final debe ser exactamente:
`TEST RESULTS: 30 PASSED, 0 FAILED`.
Cualquier fallo en un test invalida la tarea de inmediato.

### 3. Verificación de Estado en Vivo
Comprueba que el daemon y el reporte de telemetría sincronicen correctamente:
```powershell
.\build\vegaflow_daemon.exe --status
```
Verifica que las secciones de privilegios, topología de CPU, tier APU, telemetría RAM/VRAM y huella de memoria privada (< 15 MB) muestren valores óptimos y coherentes.

---

## 8. Resumen de Mandamientos para Agentes Externos

- **NUNCA** rompas la compatibilidad con el hardware de referencia (Ryzen 5 4600H + Vega 6).
- **NUNCA** hardcodees constantes de memoria (4096 MB) o frecuencia (3.4 GHz); utiliza siempre `ApuOptimizationPolicy`.
- **NUNCA** introduzcas hilos de sondeo activo ni bucles pesados en el daemon.
- **NUNCA** toques carpetas de juegos protegidos por Anti-Cheat.
- **SIEMPRE** valida tus cambios ejecutando la suite completa de 33 tests (`build.ps1 -RunTests`).
