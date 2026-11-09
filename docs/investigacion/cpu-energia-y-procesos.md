# CPU, energía y procesos

**Lector:** personas y agentes. Comprobado el 2026-11-09 salvo que se diga otra cosa. Reglas en el [README](README.md).

## Prioridad y modo ahorro (EcoQoS)

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Con `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` (vía `SetProcessInformation`) un proceso pasa a EcoQoS: Windows baja la frecuencia o lo manda a núcleos eficientes, también enchufado. Antes de Windows 11 ese nivel se llamaba LowQoS | [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation), [Quality of Service](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service) | Alta | Es la palanca principal para el fondo (fase 7); el requisito mínimo es Windows 10 1709 (plan §1e) |
| El control de velocidad de ejecución existe desde Windows 10 1709 | [POWER_THROTTLING_PROCESS_STATE](https://learn.microsoft.com/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_power_throttling_process_state) | Alta | Requisito mínimo: Windows 10 1709 |
| Microsoft dice que EcoQoS no debe usarse para trabajo en primer plano | [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) | Alta | Nunca se aplica al juego ni a "Trabajo pesado" |
| `ProcessMemoryPriority` baja la prioridad de memoria de un proceso de fondo | [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) | Alta | Forma parte de "Frenar" (fase 5) |

## Reparto de núcleos (CPU Sets)

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Los CPU Sets son afinidad "blanda", compatible con la gestión de energía; una máscara de afinidad restrictiva manda sobre ellos | [CPU Sets](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets) | Alta | Se mantienen como refuerzo de EcoQoS (fase 7) |
| `SetProcessDefaultCpuSets` y `GetSystemCpuSetInformation` existen desde Windows 10 | [SetProcessDefaultCpuSets](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessdefaultcpusets), [GetSystemCpuSetInformation](https://learn.microsoft.com/en-us/windows/win32/procthread/getsystemcpusetinformation) | Alta | Requisito mínimo |
| `EfficiencyClass`: el valor más alto es el núcleo más rápido; los E-cores tienen el más bajo. También da `CoreIndex`, `LastLevelCacheIndex` (separa CCD), `NumaNodeIndex`, `Group` y `Parked` | [SYSTEM_CPU_SET_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_cpu_set_information) | Alta | No hace falta hwloc |
| Desde Windows 11 los procesos abarcan todos los grupos de procesadores; con varios grupos, Windows ignora los CPU Sets que estén en un grupo distinto del del hilo | [Processor Groups](https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups), [CPU Sets](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets) | Alta | Repartir dentro del grupo; añadir `group` a `VfCpuCoreEntry`; el límite de 64 basta (fase 7) |
| `SetProcessDefaultCpuSetMasks` (por grupos) existe desde Windows 11 | [SetProcessDefaultCpuSetMasks](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessdefaultcpusetmasks) | Alta | Solo si hace falta más de un grupo |

## Intel híbrido y AMD X3D

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Intel recomienda evitar afinidades duras y usar EcoQoS o afinidad blanda para el fondo | [Intel, 12th Gen Game Dev Guide](https://www.intel.com/content/www/us/en/developer/articles/guide/12th-gen-intel-core-processor-gamedev-guide.html) | Media | La regla actual (fondo en E-cores con CPU Sets) no choca con Intel |
| En Core Ultra, los LP E-cores están pensados para que Windows los use a su criterio | [Intel, Optimizing Threading for Gaming Performance](https://www.intel.com/content/www/us/en/developer/articles/technical/optimizing-threading-for-gaming-performance.html) | Media | Excluirlos del fondo si se pueden identificar (pendiente) |
| AMD tiene un driver "3D V-Cache Performance Optimizer"; cómo aparca el CCD sin caché con la Game Bar solo aparece en fuentes de terceros | [AMD, notas del chipset 8.09.28.1124](https://www.amd.com/en/resources/support-articles/release-notes/RN-RYZEN-CHIPSET-8-09-28-1124.html) | Baja (el mecanismo) | No tocar X3D por defecto (fase 7) |

## Temporizador y MMCSS

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Desde Windows 10 2004 la resolución de reloj que pide un proceso ya no es global; en Windows 11 se puede ignorar si la ventana está tapada o minimizada. `NtSetTimerResolution` no está documentada | [timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod), [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) | Alta | Se retira el temporizador de 0,5 ms (fase 8) |
| MMCSS: "GPU Priority" no se usa todavía; con Scheduling Category = High la prioridad se trata como 2 (pensado para audio profesional); solo afecta a hilos que se registran con `AvSetMmThreadCharacteristics` | [Multimedia Class Scheduler Service](https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service) | Alta | Se retira GPU Priority (fase 8) |
| `Win32PrioritySeparation` (0x0 a 0x3F, por defecto 0x2) solo tiene documentación archivada; `DisablePagingExecutive` solo un blog archivado de Microsoft | [TechNet Cc976120](https://technet.microsoft.com/library/Cc976120), [SYSK 318](https://learn.microsoft.com/en-us/archive/blogs/irenak/sysk-318-registry-key-that-controls-device-driver-and-system-code-pagination-to-disk) | Media | Experimentales; su ⓘ dice que la documentación es archivada |

## Medido en este proyecto (no buscar fuera)

Las mediciones propias viven en [`../mediciones/`](../mediciones/); resumen en [`estado-de-verificacion.md`](../mediciones/estado-de-verificacion.md). Lo que más pesa en las decisiones:
- Confinar al juego a un subconjunto de hilos costó entre un 19 % y un 24 % de FPS: opción apagada.
- Matar el daemon con `Stop-Process -Force` deja aplicados los CPU Sets, EcoQoS y `Win32PrioritySeparation` ([`2026-11-juegos-reales.md`](../mediciones/2026-11-juegos-reales.md), línea 90): por eso existe la fase 2b.
