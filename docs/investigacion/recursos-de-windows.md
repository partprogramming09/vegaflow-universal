# Recursos de Windows: qué quita rendimiento y qué se puede ajustar

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

Este documento repasa qué hace Windows con la CPU, la memoria, la energía y la gráfica mientras se juega, qué palancas existen y qué dice la fuente oficial de cada una. Complementa a [`cpu-energia-y-procesos.md`](cpu-energia-y-procesos.md), que ya cubre EcoQoS, CPU Sets, el temporizador y MMCSS; esos temas no se repiten aquí.

## Cómo leerlo

Windows reparte cuatro recursos: **CPU** (quién corre y a qué frecuencia), **memoria** (qué páginas se quedan en RAM), **energía** (cuánto deja subir el procesador) y **gráfica** (cómo se presenta cada fotograma). Una palanca puede actuar sobre **todo el equipo** o sobre **un solo proceso**. Las de un proceso se deshacen solas cuando ese proceso termina; las globales sobreviven al daemon y por eso necesitan el diario de la fase 2b.

Cada palanca termina en uno de cuatro destinos:

| Destino | Qué significa |
|---|---|
| **Estable** | Documentada, se puede deshacer y no tiene contrapartida de seguridad |
| **Experimental** | Documentada a medias o sin medir; oculta tras el interruptor, con aviso |
| **Solo informar** | VegaFlow la lee y la explica, pero nunca la cambia (seguridad o firmware) |
| **Descartada** | Sin fuente, peligrosa o fuera del alcance del producto |

## Lo que VegaFlow ya hace hoy (revisado contra la fuente)

| Qué hace el código | Dónde | Qué dice la fuente | Confianza | Qué se decidió |
|---|---|---|---|---|
| Quita el modo ahorro al juego: `ProcessPowerThrottling` con `ControlMask` puesto y `StateMask = 0` | `src/optimize/cpu_orchestrator.cpp:159` | Así se marca un proceso como HighQoS de forma explícita ([Quality of Service](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service)) | Alta | Estable; se mantiene |
| Sube el juego a `HIGH_PRIORITY_CLASS` si es DX11 | `src/optimize/cpu_orchestrator.cpp:155` | Microsoft pide "extremo cuidado" con HIGH porque puede acaparar casi toda la CPU ([SetPriorityClass](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass)) | Alta | **Revisar en la fase 8:** dejar `ABOVE_NORMAL` para todos. Medido el 2026-11-09 sin carga de fondo: HIGH no aporta nada ([`estado-de-verificacion.md`](../mediciones/estado-de-verificacion.md)); con carga, sin aislar |
| Vacía la memoria de los procesos de fondo con `EmptyWorkingSet` | `src/optimize/memory_manager.cpp:251` | Microsoft lo describe como "útil sobre todo para pruebas y ajuste"; las páginas quitadas vuelven con fallos de página cuando el proceso se despierta ([Working Set Information](https://learn.microsoft.com/en-us/windows/win32/psapi/working-set-information), [Working Set](https://learn.microsoft.com/en-us/windows/win32/memory/working-set)) | Alta | **Revisar en la fase 8:** solo cuando falte memoria de verdad, no por calendario. Sin medir |
| Vacía la lista de memoria en espera con `NtSetSystemInformation` | `src/optimize/memory_manager.cpp:40` | No documentada | Sin verificar | Experimental |
| Apaga la compresión de memoria (`Disable-MMAgent -MemoryCompression`) | `src/optimize/graphics/kernel_tweaks.cpp:16` | El cmdlet está documentado, pero no su efecto en juegos ([Disable-MMAgent](https://learn.microsoft.com/powershell/module/mmagent/disable-mmagent)) | Alta (el cmdlet) / sin fuente (el efecto) | Experimental. En una APU con poca RAM, apagarla puede hacer que se use más el archivo de paginación. Sin medir |
| Ajusta el plan de energía: arranque turbo (`PERFBOOSTMODE`), núcleos aparcados (`CPMINCORES`), preferencia energía/rendimiento (`PERFEPP`), frecuencia máxima y ASPM de PCIe | `src/optimize/graphics/power_mode.cpp:16-22` | Todos documentados ([PERFBOOSTMODE](https://learn.microsoft.com/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfboostmode), [CPMinCores](https://learn.microsoft.com/windows-hardware/customize/power-settings/options-for-core-parking-cpmincores), [PerfEnergyPreference](https://learn.microsoft.com/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfenergypreference)) | Alta | Estable mientras se guarde y se restaure el valor anterior (fase 2b). **Medido:** ASPM, EPP y el modo de boost no tuvieron efecto medible en `drawstress` (≈1 %, [`estado-de-verificacion.md`](../mediciones/estado-de-verificacion.md), línea 6); falta medirlos en juegos reales. Ver las notas de abajo |
| Escribe `SwapEffectUpgradeEnable=1` en `UserGpuPreferences` para el juego | `src/optimize/graphics/dxgi_flip.cpp:19` | Es lo que activa "Optimizaciones para juegos en ventana" para un programa: pasa los juegos DX10/DX11 en ventana del modelo blt al modelo flip ([Microsoft Support](https://support.microsoft.com/en-gb/windows/hardware/display-graphics/optimizations-for-windowed-games-in-windows-11), [DirectX blog, 2022](https://devblogs.microsoft.com/directx/updates-in-graphics-and-gaming/)) | Media (la clave del registro sale de la interfaz, no de una página de API) | Estable |
| Añade `AutoFlipModel=1;DirectFlip=1` a la misma clave | `src/optimize/graphics/dxgi_flip.cpp:160` | No aparecen en ninguna fuente oficial | Sin verificar | **Quitar** si nadie encuentra fuente; mientras tanto, experimental |
| Escribe `GpuPreference=2` (alto rendimiento) para el juego | `src/optimize/graphics/dxgi_flip.cpp:19` | Equivale a elegir "Alto rendimiento" en Configuración > Pantalla > Gráficos; Windows ya elige la dGPU por su cuenta en la mayoría de los casos ([Surface Book 3 GPU overview](https://learn.microsoft.com/surface/surface-book-gpu-overview)) | Media | Útil solo con dos gráficas (APU + dGPU o eGPU). Con una sola gráfica no cambia nada: el perfil de la fase 3 no debería escribirlo |

Notas sobre el plan de energía:
- `PERFEPP` solo tiene efecto en procesadores con CPPC v2 en modo autónomo; qué procesadores lo cumplen no está en la fuente (pendiente). 0 = todo rendimiento, 100 = todo ahorro ([PerfEnergyPreference](https://learn.microsoft.com/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfenergypreference)).
- Con `CPMINCORES = 100` no se aparca ningún núcleo. Microsoft señala que aparcar puede **mejorar** el uso de la caché cuando un programa usa pocos hilos ([PPM tuning](https://learn.microsoft.com/windows-server/administration/performance-tuning/hardware/power/processor-power-management-tuning)). No siempre gana desactivarlo: hay que medirlo.
- Microsoft dice que la limitación de energía queda apagada para todas las aplicaciones cuando el control deslizante está en "Mejor rendimiento" ([Customize the power slider](https://learn.microsoft.com/windows-hardware/customize/desktop/customize-power-slider#power-throttling)). No se sabe si eso anula también el EcoQoS que VegaFlow pone al fondo de forma explícita; es la palanca principal de la fase 7, así que está en pendientes.
- En un portátil con batería, cada palanca de energía tiene coste en batería y temperatura. El ⓘ lo dice.

## Palancas nuevas que se pueden añadir

### Para un solo proceso

| Palanca | Qué hace | Fuente | Confianza | Destino propuesto |
|---|---|---|---|---|
| `PROCESS_MODE_BACKGROUND_BEGIN` en **el propio VegaFlow** | Baja a la vez la prioridad de CPU, de disco y de memoria del proceso que la llama. Solo sirve para uno mismo, no para otros procesos | [SetPriorityClass](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass) | Alta | **Estable**, en el modo "Siempre activo": así el daemon nunca le quita disco ni memoria al juego (fase 7). Cuidado: el diario de cambios de la fase 2b se escribe **antes** de aplicar un cambio; con prioridad de disco muy baja y el juego cargando, esa escritura podría esperar. Escribir el diario fuera del modo de fondo |
| Prioridad de disco de **otros** procesos | Solo existe mediante `NtSetInformationProcess`, que no está documentada | — | Sin verificar | Descartada |
| `SetProcessPriorityBoost` | Activa o desactiva el empujón temporal de prioridad cuando un hilo sale de una espera | [SetProcessPriorityBoost](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocesspriorityboost) | Alta (la API) / sin medir (el efecto) | Descartada por ahora: no hay ningún motivo medido para tocarla |

### Para todo el equipo

| Palanca | Qué hace | Fuente | Confianza | Destino propuesto |
|---|---|---|---|---|
| Leer el **modo de energía efectivo** (`PowerRegisterForEffectivePowerModeNotifications`) | Avisa cuando cambian el modo de energía, el ahorro de batería o el Modo Juego. Solo lee | [PowerRegisterForEffectivePowerModeNotifications](https://learn.microsoft.com/en-us/windows/win32/api/powersetting/nf-powersetting-powerregisterforeffectivepowermodenotifications) (V2 desde Windows 10 1903) | Alta | **Estable** (fase 3): si el usuario activa el ahorro de batería, VegaFlow no fuerza el plan de rendimiento y lo muestra en Inicio |
| `DisableUserPresenceQos` | Con batería, Windows puede bajar la calidad de servicio del programa en primer plano si no detecta entrada del usuario durante un rato. Este valor lo desactiva. Microsoft lo recomienda para medir con batería | [Quality of Service](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service) | Alta | **Experimental**, y en el modo desarrollador para pruebas A/B con batería. Solo existe desde la actualización de mayo de 2025 ("Ge 2025.05" en la tabla de Microsoft): en Windows 10 no hace nada. Falta saber si un mando cuenta como entrada (pendiente) |
| `PowerThrottlingOff` (directiva) | Apaga la limitación de energía para todos los procesos | [Policy CSP ADMX_Power](https://learn.microsoft.com/windows/client-management/mdm/policy-csp-admx-power) | Alta | Descartada: choca con la idea de frenar el fondo con EcoQoS |
| Programación de GPU acelerada por hardware (HAGS) | Pasa la planificación de la GPU a un procesador de la propia tarjeta. Desde Windows 10 2004; depende del driver | [DirectX blog, 2020](https://devblogs.microsoft.com/directx/hardware-accelerated-gpu-scheduling/) | Media (blog oficial, sin página de referencia) | **Solo informar** (fase 3): mostrar si está activa. Cambiarla exige reiniciar y depende del driver |

### Seguridad que cuesta rendimiento

| Palanca | Qué hace | Fuente | Confianza | Destino propuesto |
|---|---|---|---|---|
| Integridad de memoria (HVCI) y Plataforma de máquina virtual | Microsoft publicó en 2022 una página de soporte que dice que pueden afectar al rendimiento en juegos, y sugiere apagarlas para jugar y volver a encenderlas después | Página de soporte de Microsoft, conocida por [Tom's Hardware](https://www.tomshardware.com/news/microsoft-windows-11-gaming-boost-tips) y [TechSpot](https://www.techspot.com/news/96246-microsoft-releases-guide-optimize-windows-11-gaming-performance.html); no se pudo abrir la página original | Media | **Solo informar.** VegaFlow lee el estado con `Win32_DeviceGuard` ([Microsoft Learn](https://learn.microsoft.com/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity)) y explica la contrapartida; nunca lo cambia. Microsoft avisa de que los procesadores sin MBEC/GMET (antes de Intel Kaby Lake y AMD Zen 2) lo emulan y pagan más ([misma página](https://learn.microsoft.com/windows/security/hardware-security/enable-virtualization-based-protection-of-code-integrity)) |

### Hardware que conviene vigilar (solo informar)

| Qué | Por qué importa | Fuente | Confianza | Destino propuesto |
|---|---|---|---|---|
| **Memoria en un solo canal** | En una APU la gráfica usa la RAM del sistema; con un solo módulo tiene la mitad de ancho de banda | Se lee de `Win32_PhysicalMemory` (`DeviceLocator`, `BankLabel`, `Capacity`) ([Win32_PhysicalMemory](https://learn.microsoft.com/windows/win32/cimwin32prov/win32-physicalmemory)). El canal se **deduce** de las etiquetas, que cada fabricante escribe a su manera | Media (los datos) / baja (deducir el canal) | **Solo informar** (fase 3) con texto prudente: "Hay un solo módulo de memoria; en gráficas integradas suele rendir más con dos". Nunca afirmar "canal único" sin certeza |
| **Pantalla por debajo de su frecuencia máxima** | Un monitor de 144 Hz configurado a 60 Hz | `EnumDisplaySettings` ya está en [`graficas-y-perfil-del-equipo.md`](graficas-y-perfil-del-equipo.md) | Alta | **Solo informar** (fase 3) |
| **Memoria integrada reservada (UMA) en la BIOS** | Ya está en el plan, §10 | — | — | Fuera del plan |

## Descartado

| Idea | Por qué no | Confianza |
|---|---|---|
| Parar servicios (SysMain, Windows Search, Windows Update) mientras se juega | No hay fuente que lo respalde para juegos; parar servicios del sistema puede romper actualizaciones o búsquedas, y si el daemon muere quedan parados | Sin verificar (no se buscó fuente) |
| Exclusiones de Defender para las carpetas de juegos | Contrapartida de seguridad: deja sin analizar archivos que se ejecutan | Sin verificar (razonamiento propio) |
| Afinidad de interrupciones o modo MSI de la gráfica | Toca drivers y el registro de dispositivos; un error puede dejar el equipo sin imagen | Sin verificar (razonamiento propio) |
| `REALTIME_PRIORITY_CLASS` | Microsoft avisa de que puede dejar sin respuesta al ratón y a la caché de disco ([SetPriorityClass](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass)) | Alta |

## Qué cambia en el plan si se adopta

- **Fase 3 (perfil del equipo):** leer el modo de energía efectivo, HAGS, HVCI/VBS, los módulos de memoria y la frecuencia de la pantalla. Todo es de lectura.
- **Fase 7 (CPU):** `PROCESS_MODE_BACKGROUND_BEGIN` para el propio daemon.
- **Fase 8 (familias de ajustes):** prioridad HIGH a experimental, `EmptyWorkingSet` solo con poca memoria, quitar `AutoFlipModel` y `DirectFlip`, `GpuPreference` solo con dos gráficas, y `DisableUserPresenceQos` como experimental.

Nada de esto está aprobado en el plan todavía: es investigación.
