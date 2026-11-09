# Gráficas y perfil del equipo

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

## Qué gráfica hay y cómo está conectada

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| DXCore expone `IsIntegrated` (12) e `IsDetachable` (13) desde Windows 10 build 18936; la página está marcada como versión previa, y avisa de que una GPU puede desaparecer aunque `IsDetachable` sea falso | [DXCoreAdapterProperty](https://learn.microsoft.com/en-us/windows/win32/api/dxcore_interface/ne-dxcore_interface-dxcoreadapterproperty) | Alta | Detectar integrada o externa con DXCore; si no hay DXCore, DXGI y conexión "desconocida" (fase 3) |
| `DXGI_ADAPTER_DESC1` no dice si la GPU es integrada o extraíble: solo memorias y `Flags` | [DXGI_ADAPTER_DESC1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_adapter_desc1) | Alta | No sirve para saber la conexión. Sin DXCore: conexión "desconocida" → perfil de dedicada sin recortes y sin guardián de memoria compartida; no se vuelve al corte de 2 GB. La regla exacta se fija en la fase 3 con ejemplos |
| `D3D12_FEATURE_DATA_ARCHITECTURE.UMA` indica memoria unificada pero no distingue una eGPU, y está sustituido por `ARCHITECTURE1` | [D3D12_FEATURE_DATA_ARCHITECTURE](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_architecture) | Alta | No se usa para la conexión |
| `DEVPKEY_Device_RemovalPolicy` (valores `CM_REMOVAL_POLICY_*`) existe desde Vista | [DEVPKEY_Device_RemovalPolicy](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/devpkey-device-removalpolicy) | Alta | Segunda opinión para confirmar una eGPU |
| Una eGPU por OCuLink probablemente no aparece como extraíble | Deducción, sin fuente | Sin verificar | Se trata como "dedicada" |

## Perfil del equipo

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| `DEVICE_SEEK_PENALTY_DESCRIPTOR` distingue disco con o sin penalización de búsqueda (HDD o SSD), no NVMe; NVMe sale de `BusType == BusTypeNvme` | [DEVICE_SEEK_PENALTY_DESCRIPTOR](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-device_seek_penalty_descriptor), [STORAGE_BUS_TYPE](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ne-winioctl-storage_bus_type) | Alta | Dos consultas; el tipo de disco es solo informativo |
| Para abrir `\\.\X:`, `CreateFileW` pide administrador en una sección y en otra permite `dwDesiredAccess = 0` para consultar atributos | [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew) | Media (se contradice) | Probar sin pedir escritura (pendiente) |
| `SYSTEM_POWER_STATUS`: `ACLineStatus` 0, 1 o 255 (desconocido); `BatteryFlag` 128 = sin batería; `SystemStatusFlag` = ahorro de batería. No dice si es portátil o sobremesa | [SYSTEM_POWER_STATUS](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-system_power_status) | Alta | La etiqueta es "con o sin batería" |
| `EnumDisplaySettings` da resolución y frecuencia en píxeles físicos; con `NULL` solo la pantalla del hilo; varios monitores con `EnumDisplayDevices` | [EnumDisplaySettingsW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaysettingsw) | Alta | Se lee desde la GUI (sesión del usuario) |
| Microsoft pide comprobar si una función existe, no mirar la versión; sin manifiesto, `GetVersionEx` devuelve 6.2. `RtlGetVersion` solo está documentada para drivers | [GetVersionExW](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getversionexw), [RtlGetVersion](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-rtlgetversion) | Alta | Activar por función disponible; la versión solo se muestra |
| Un proceso x64 emulado en ARM64 no es WOW64: hay que mirar que la máquina nativa sea ARM64 con `IsWow64Process2`; alternativa: `GetProcessInformation(ProcessMachineTypeInfo)`, build 22000 o posterior | [IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2), [PROCESS_MACHINE_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_machine_information) | Alta | En ARM, solo información |

## Presentación de fotogramas y overlays

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Si hay contenido encima del juego, DWM puede volver a componer, hacer "reverse compose" o usar MPO para mantener el independent flip; no se documenta cuál elige cada equipo | [DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model) | Alta | Se descarta el overlay de FPS |
| `OverlayTestMode` (desactivar MPO) no aparece en la página de valores del registro de DWM | [DWM registry values](https://learn.microsoft.com/en-us/windows/win32/dwm/registry-values) | Alta (ausencia) | Experimental; su ⓘ lo dice |
| En el proveedor DXGI, 42 = `Present_Start` y 43 = `Present_Stop`; 39 y 40 no son eventos de presentación | [PresentMon, Microsoft_Windows_DXGI.h](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/ETW/Microsoft_Windows_DXGI.h) | Alta | La captura del daemon contaba mal; se quita (fase 6) |
| "GPU Busy" de PresentMon sale solo de eventos ETW de DxgKrnl | [PresentMonTraceSession.cpp](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/PresentMonTraceSession.cpp) | Alta | No se usa: VegaFlow no es un medidor |

## Variables de Unreal Engine

| Variable | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| `r.ShaderPipelineCache.Enabled=1` activa las cachés de PSO empaquetadas | [Epic, Bundled PSO caches](https://dev.epicgames.com/documentation/en-us/unreal-engine/manually-creating-bundled-pso-caches-in-unreal-engine) | Alta | Perfil sin recortes para dGPU y eGPU |
| `r.GTSyncType`: 0 render thread, 1 RHI thread, 2 swap chain | [Epic, Low Latency Frame Syncing](https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing-in-unreal-engine) | Alta | No va por defecto: cambia latencia, no está medido |
| `r.Streaming.AmortizeCPUToGPUCopy` y `MaxNumTexturesToStreamPerFrame` solo funcionan juntos; la página oficial no los lista | Solo una wiki de terceros (UE 5.4.4) | Baja | Experimental para eGPU, pendiente de confirmar en el código del motor |
| `r.ShaderPipelineCache.AsyncCompileRate` | No aparece en ninguna fuente | Sin verificar | Descartada |
