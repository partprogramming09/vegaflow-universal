# VegaFlow Universal — Developer & Architecture Guide

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


## 7. Instrucciones para Asistentes y Desarrolladores

1. **Nunca omitir verificaciones en seco (dry_run):** Todas las funciones que modifican el sistema (`ApplyEngineUmaPreset`, `DeployDxvk`, `SetMpoDisabled`, etc.) deben disponer de un parámetro `dry_run = false` para pruebas unitarias sin impacto real.
2. **Preservar el Zero-Overhead:** No añadir dependencias de frameworks masivos (Qt, WxWidgets, Boost). La GUI debe seguir siendo puramente Win32/GDI nativa para garantizar `< 15 MB` de consumo de RAM.
3. **Manejo de rutas con espacios y Unicode:** Usar siempre `std::wstring` para interactuar con la API Win32 y convertir limpiamente con `Utf8ToWide` / `WideToUtf8`.
4. **Respaldar antes de escribir:** Cualquier modificación a un archivo `.ini`, `.config` o DLL debe ir precedida de la creación de un archivo `.vegaflow.bak` o `.original.bak`.

---

