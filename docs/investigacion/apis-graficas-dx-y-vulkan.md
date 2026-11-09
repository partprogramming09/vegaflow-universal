# APIs gráficas: DirectX 9 a 12 y Vulkan

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

Qué le quita rendimiento a un juego por la API que usa (DirectX 9, 10, 11, 12 o Vulkan), qué funciones nuevas no aprovecha un juego viejo, qué restos de código abandonado hay en Windows y en los drivers, y qué puede hacer una herramienta externa como VegaFlow sin tocar el juego. Complementa a [`graficas-y-perfil-del-equipo.md`](graficas-y-perfil-del-equipo.md) (modelo flip, overlays, variables de Unreal) y a [`recursos-de-windows.md`](recursos-de-windows.md).

## La idea en una frase

Un juego se queda con la API y las costumbres del año en que se hizo. Lo que vino después (cachés de shaders mejores, memoria compartida CPU-GPU, sombreadores precompilados) **solo lo aprovecha el juego si su código lo pide**. Desde fuera no se puede reescribir el juego, pero sí se puede:

1. **Detectar e informar** de por qué va mal ("tu driver ya no recibe optimizaciones", "hay una capa de Vulkan rota").
2. **Mantener sano lo que el juego ya usa** (cachés de shaders, elección de gráfica).
3. **Limpiar restos** que dejaron otros programas (capas e ICD de Vulkan huérfanos).
4. **Traducir la API** con DXVK donde está permitido (ya existe en VegaFlow, bloqueado si hay anti-cheat: [`seguridad.md`](../arquitectura/seguridad.md)).

## Lo que solo el juego puede aprovechar

Estas funciones existen, pero las activa el código del juego. VegaFlow no puede hacerlo por él; como mucho, puede informar si el equipo las soporta.

| Función | Qué hace | Fuente | Confianza | Qué puede hacer VegaFlow |
|---|---|---|---|---|
| Advanced Shader Delivery (DX12) | Descarga los sombreadores ya compilados para tu gráfica en vez de compilarlos al jugar; ataca los tirones por compilación. Empezó en 2025 solo en ROG Xbox Ally; en 2026 se amplió a AMD RDNA 3, 3.5 y 4, Intel y Qualcomm, con NVIDIA anunciada | [DirectX blog, GDC 2026](https://devblogs.microsoft.com/directx/advanced-shader-delivery-whats-new-at-gdc-2026/), [Microsoft Game Dev](https://developer.microsoft.com/en-us/games/articles/2026/03/gdc-2026-advanced-shader-delivery-on-windows/) | Alta (anuncio) / media (lista de gráficas, por prensa) | Nada directo: depende del juego y de la tienda. Por la lista publicada, GCN (Vega) no aparece |
| GPU upload heaps (DX12) | La CPU escribe directamente en la memoria de la gráfica. En una dGPU exige Resizable BAR activado en la BIOS; en una integrada no hace falta | [DirectX-Specs](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html), [NVIDIA](https://developer.nvidia.com/blog/optimizing-dx12-resource-uploads-to-the-gpu-using-cpu-visible-vram), [GPUOpen](https://gpuopen.com/learn/using-d3d12-heap-type-gpu-upload/) | Alta | **Solo informar** (fase 3, dGPU y eGPU): leer `GPUUploadHeapSupported` y, si es falso, explicar que suele depender de Resizable BAR. La especificación decía que en 2023 solo funcionaba con el modo desarrollador de Windows: comprobar si sigue así (pendiente) |
| Precarga de PSO, DirectStorage, Agility SDK | Decisiones del motor del juego | — | — | Nada |

## Por API

### DirectX 9 y 10

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Intel Arc y las integradas Intel de 12.ª generación en adelante no tienen driver DX9 propio: usan D3D9On12, una traducción de Microsoft a DX12 que gasta más CPU. Intel optimiza después juego a juego | [Tom's Hardware, 2022](https://www.tomshardware.com/news/intel-xe-arc-swap-to-dx9-emulation), [revisión con medidas](https://tomshardware.com/features/intel-arc-graphics-performance-revisited-dx9-steps-forward-dx12-steps-back) | Media (prensa; los datos son de 2022–2023) | En Intel Arc o Xe con un juego DX9, DXVK (DX9 → Vulkan) es una alternativa a D3D9On12. **Experimental**: sin medir en este proyecto |
| En AMD y NVIDIA, DX9 tiene driver propio con años de ajustes | Mismas fuentes | Media | Sin cambios |

### DirectX 11

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| Los juegos DX10 y DX11 en ventana usaban el modelo de presentación "blt", más lento; "Optimizaciones para juegos en ventana" los pasa al modelo flip | Ya en [`recursos-de-windows.md`](recursos-de-windows.md) | Media | Ya se aplica (`SwapEffectUpgradeEnable`) |
| Se dice que el driver DX11 de AMD gasta más CPU por llamada de dibujo que el de NVIDIA, y que por eso DXVK ayuda en APUs | Solo foros y prensa; no se buscó una fuente oficial | Sin verificar | Es la base del DXVK de VegaFlow: **medirlo** con A/B en el laptop antes de llamarlo "Recomendado" |

### DirectX 12

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| El "tirón por compilación de shaders" es, según Microsoft, uno de los mayores problemas del PC; el driver guarda lo compilado en una caché | [DirectX blog](https://devblogs.microsoft.com/directx/advanced-shader-delivery-whats-new-at-gdc-2026/) | Alta | — |
| La caché de sombreadores de DirectX se puede borrar con el Liberador de espacio; Windows la regenera cuando hace falta, y mientras tanto el juego vuelve a compilar | [Microsoft Q&A](https://learn.microsoft.com/en-us/answers/questions/3195734/directx-shader-cache) (respuestas de la comunidad), [Wikipedia: Disk Cleanup](https://en.wikipedia.org/wiki/Disk_Cleanup) | Baja | VegaFlow **nunca** borra cachés de sombreadores sin que lo pida el usuario. Si el Sensor de almacenamiento también la borra, no está documentado (pendiente) |

### Vulkan

| Dato | Fuente | Confianza | Qué se decidió |
|---|---|---|---|
| El cargador de Vulkan lee las **capas implícitas** de `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (y de HKCU; los programas de 32 bits, de `WOW6432Node`). Cada una se carga **en todos los juegos Vulkan** sin que el juego lo pida | [Khronos, LoaderLayerInterface](https://fuchsia.googlesource.com/third_party/Vulkan-Loader/+/refs/tags/v1.4.327/docs/LoaderLayerInterface.md), [LunarG](https://vulkan.lunarg.com/doc/view/latest/mac/loader_and_layer_interface.html) | Alta | Base de la detección de capas huérfanas (abajo) |
| Overlays y tiendas instalan capas implícitas; una capa mal hecha provoca avisos o fallos en todos los juegos Vulkan. Caso documentado: GOG Galaxy; que OBS, Steam y otros también las instalen está sin verificar | [Caso GalaxyOverlayVkLayer, 2025](https://alegruz.github.io/graphics/2025/03/22/galaxyoverlayvklayer-issue.html) | Media (GOG) / sin verificar (los demás) | **Solo informar** (fase 3): listar las capas implícitas y marcar las que apuntan a un archivo que ya no existe |
| `VK_LOADER_LAYERS_DISABLE=~implicit~` desactiva todas las capas implícitas en un proceso; acepta comodines como `*OBS*` | [Khronos, vk_loader_platform.h](https://fuchsia.googlesource.com/third_party/Vulkan-Loader/+/HEAD/loader/vk_loader_platform.h), [LoaderDebugging](https://fuchsia.googlesource.com/third_party/Vulkan-Loader/+show/refs/tags/v1.4.327/docs/LoaderDebugging.md) | Alta | Cada capa trae además su propia variable para desactivarla (`disable_environment` en el JSON, p. ej. `DISABLE_VK_LAYER_VALVE_steam_overlay_1`), comprobado en el laptop. Útil para el modo desarrollador ("probar este juego sin capas") con el juego lanzado desde VegaFlow. No sirve para un juego ya abierto |
| En Windows, el cargador ordena las gráficas siguiendo el orden de DXGI (por LUID) cuando el driver lo soporta | [Khronos, commit "Sort physical devices on Windows"](https://fuchsia.googlesource.com/third_party/Vulkan-Loader/+/ac70a648811fef8efdb7a63d0869facb24f889d3%5E%21/), [VK_MSFT_layered_driver](https://docs.vulkan.org/features/latest/features/proposals/VK_MSFT_layered_driver.html) | Media (código fuente, sin documento de usuario) | Si `GpuPreference` de Windows también cambia la gráfica que elige un juego Vulkan, no está confirmado (pendiente). Hasta entonces, el ⓘ no promete que la preferencia de GPU afecte a Vulkan |
| Los drivers Vulkan registrados (ICD) de una gráfica que ya no está pueden quedar registrados | En el laptop, `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` no existe: el driver de AMD se registra en la clave de la clase de pantalla (`VulkanDriverName`) ([diagnóstico](../mediciones/2026-11-09-diagnostico-laptop.md)) | Media (un equipo) | Revisar la clase del adaptador, no solo `Khronos\Vulkan\Drivers` |

## Código abandonado fuera del juego

| Qué | Por qué importa | Fuente | Confianza | Qué se decidió |
|---|---|---|---|---|
| **Drivers de AMD Vega y Polaris en mantenimiento** desde noviembre de 2023: solo correcciones de seguridad y estabilidad, sin optimizaciones para juegos nuevos. La prensa dice que afecta también a las APU con gráfica Vega (sería el caso de la **Ryzen 5 4600H del banco de pruebas**), pero no lo confirma para ningún chip concreto | [AnandTech, 2023](https://www.anandtech.com/show/21126), [Tom's Hardware](https://tomshardware.com/pc-components/gpus/its-curtains-for-polaris-and-vega-as-amd-reduces-driver-support) | Media (las gráficas dedicadas, por prensa que cita a AMD) / baja (las APU) | **Solo informar** (fase 3), con texto prudente: "Tu gráfica recibe solo actualizaciones de mantenimiento". Es otra razón para que el perfil APU dependa de medidas propias y no del driver |
| RDNA 1 y 2 pasaron también a mantenimiento con el driver 25.10.2 | Una sola mención en prensa, sin comprobar | Sin verificar | Comprobar con las notas de AMD antes de mostrarlo (pendiente) |
| Grabación en segundo plano de la Game Bar ("Grabar lo que pasó") mantiene un búfer continuo de vídeo | Solo guías de terceros | Baja | **Solo informar** con confianza baja; nunca cambiarlo sin que lo pida el usuario |

## Errores encontrados en el código de VegaFlow

Revisados el 2026-11-09 leyendo el código; no se han probado en el laptop.

| Error | Dónde | Por qué falla | Propuesta |
|---|---|---|---|
| Un juego DX11 con `vulkan-1.dll` cargado se clasifica como Vulkan | `src/detect/process_detector.cpp:208` | La prioridad es DX12 > Vulkan > DX11 > DX9. **Con DXVK es correcto:** el juego dibuja con Vulkan y sus hilos de compilación encajan con el trato de DX12/Vulkan. **Con un overlay:** comprobado el 2026-11-09 que Octopath Traveler II (DX11) con el overlay de Steam **no** carga `vulkan-1.dll` ([diagnóstico](../mediciones/2026-11-09-diagnostico-laptop.md)). Además, un `d3d11.dll` en la carpeta del juego **no** implica DXVK: allí había uno de SCSKiller | No cambiar la regla por ahora; para distinguir DXVK, mirar la información de versión del DLL, no su carpeta. La única consecuencia es la prioridad (HIGH o ABOVE_NORMAL) y el hilo de la ventana; si la fase 8 deja ABOVE_NORMAL para todos ([`recursos-de-windows.md`](recursos-de-windows.md)), esta clasificación deja de tener efecto práctico |
| **El "hilo presentador" es el hilo de la ventana** | `src/detect/process_detector.cpp:256`, `src/app/run_cycle.cpp:55` | `GetWindowThreadProcessId` devuelve el hilo que creó la ventana. En motores como Unreal, el render y el RHI van en hilos aparte ([Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/low-latency-frame-syncing-in-unreal-engine)), así que se sube la prioridad del hilo de entrada, no del que presenta | Fase 8: cambiar el nombre a `window_thread_id` y medir si subirle la prioridad aporta algo; si no, quitarlo |
| El detector de juegos marca como juego programas que no lo son | Registro `UserGpuPreferences` del laptop | VegaFlow escribió preferencias de GPU para `mmc.exe`, `LockApp.exe`, `ShellHost.exe`, Radeon Software, launchers de EA y Epic, SCSKiller y para sí mismo ([diagnóstico](../mediciones/2026-11-09-diagnostico-laptop.md)) | Fase 3 o 5: lista de exclusión ampliada y test con esos nombres; no escribir `GpuPreference` con una sola gráfica |
| `AutoFlipModel` y `DirectFlip` sin fuente | `src/optimize/graphics/dxgi_flip.cpp:160` | Ya en [`recursos-de-windows.md`](recursos-de-windows.md) | Fase 8 |

## Qué podemos hacer que Windows no hace

Microsoft no puede ajustar cada equipo a mano ni tocar programas de terceros. VegaFlow sí puede, siempre con datos locales y deshaciendo lo que cambie:

- **Conoce este equipo concreto:** si la gráfica es Vega en mantenimiento, si falta Resizable BAR, si hay un solo módulo de RAM. Windows aplica la misma política a todos.
- **Recuerda cada juego:** el botón "Me fue mal en este juego" (fase G) guarda qué se aplicó y lo quita la próxima vez.
- **Ve los restos de otros programas:** capas Vulkan huérfanas o cachés borradas. Windows no avisa de ellas.
- **Traduce la API** con DXVK en juegos sin anti-cheat, si se demuestra con medidas que ayuda en ese equipo.

## Qué cambia en el plan si se adopta

- **Fase 3:** leer `GPUUploadHeapSupported`, el estado de mantenimiento del driver, las capas Vulkan implícitas (y marcar las huérfanas) y corregir la detección DX11/Vulkan.
- **Fase 8:** `window_thread_id`, `AutoFlipModel` y `DirectFlip`.
- **Modo desarrollador:** "probar este juego sin capas Vulkan" y "DXVK en un juego DX9 con Intel", como pruebas A/B.

Nada de esto está aprobado en el plan: es investigación.
