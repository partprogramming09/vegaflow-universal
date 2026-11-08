# Casos de estudio del README antiguo (sin medir)

> **Archivado el 2026-11-08.** Cifras del README original que no salen de las mediciones del repo. Zenless Zone Zero: aquí se afirman ~35 FPS desde 27-29; la medición con protocolo dio 58.57 a 58.89 FPS, sin efecto ([`../mediciones/2026-11-juegos-reales.md`](../mediciones/2026-11-juegos-reales.md)). Las cifras vigentes de Octopath están en [`../mediciones/`](../mediciones/).

## 3. Casos de Estudio Reales y Evidencia Empírica

Los resultados a continuación fueron medidos y verificados en hardware real (**AMD Ryzen 5 4600H, 16 GB DDR4-3200 Dual Channel, Radeon RX Vega 6, Windows 11 x64**).

### Caso 1: Octopath Traveler II (Unreal Engine 4 — Steam)

```
RENDIMIENTO EN OCTOPATH TRAVELER II (FPS Y FLUIDEZ)
-----------------------------------------------------------------
Nativo (DirectX 11):      | [======== 28-30 FPS ] (Stuttering recurrente, texturas borrosas)
VegaFlow (DXVK + UMA):    | [============= 40-44 FPS ] (Nítido, 0 pop-in, 1% Lows +65%)
-----------------------------------------------------------------
0 FPS                    10 FPS                 30 FPS                 45 FPS
```

- **Condición Inicial:** El juego corría a 28-30 FPS con caídas severas por debajo de 20 FPS en transiciones de ciudades y combates. Las texturas de escenarios permanecían borrosas debido al falso límite de 512 MB de VRAM impuesto por la BIOS.
- **Intervención VegaFlow:**
  1. Despliegue de `DXVK-GPLAsync` con 6 hilos de compilación paralela de shaders.
  2. Inyección del Preset UMA en `Engine.ini`: `r.Streaming.PoolSize=3000`, `r.Streaming.LimitPoolSizeToVRAM=0`, desactivación de `AsyncCompute`.
  3. Upgrade a DXGI Hardware Flip Model y temporizador NT a 0.5 ms.
  4. Activación de la **Jerarquía de Prioridad I/O NT** (`IoPriorityHigh` en el proceso del juego y `IoPriorityVeryLow` en procesos de fondo vía `NtSetInformationProcess`), blindando el bus de disco SSD NVMe durante la carga de assets 3D.
- **Resultado Obtenido:**
  - **40+ FPS constantes** en 1080p nativo.
  - Texturas cargadas en su máxima resolución sin retraso visual ni pop-in.
  - Desaparición total de micro-tirones al compilar habilidades y magias o cruzar puertas de mazmorras.

---

### Caso 2: Zenless Zone Zero (Unity / HoYoKProtect — HoYoverse)

```
RENDIMIENTO EN ZENLESS ZONE ZERO (FPS Y LATENCIA)
-----------------------------------------------------------------
Nativo (DirectX 11):      | [======= 27-29 FPS ] (Input lag de DWM, micro-congelamientos)
VegaFlow (DirectFlip+Eco):| [========== ~35 FPS ] (Fluidez notable, latencia 0ms, 100% SEGURO)
-----------------------------------------------------------------
0 FPS                    10 FPS                 25 FPS                 35 FPS
```

- **Condición Inicial:** 27-29 FPS en combates intensos con marcada pesadez en la respuesta de controles debido a la composición de ventanas de DWM. El juego está protegido por el controlador kernel **HoYoKProtect**.
- **Intervención VegaFlow:**
  1. **Anti-Cheat Sentinel** detecta la presencia de `HoYoKProtect.sys` y bloquea automáticamente la inyección de DLLs o la edición de archivos locales.
  2. Activación de **DirectFlip & AutoFlip Model Upgrade** (`AutoFlipModel=1;DirectFlip=1;TurboSync=1;`) en `UserGpuPreferences`, permitiendo la entrega directa de fotogramas a la GPU y eliminando la cola de copiado de DWM con 0ms de latencia en pantalla completa sin bordes.
  3. Aplicación de **Background EcoQoS** (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED`) a procesos de fondo para recuperar de 3W a 5W de presupuesto STAPM, manteniendo la Radeon Vega 6 a 1500 MHz continuos.
  4. Asignación de **Jerarquía de Prioridad I/O NT** (`IoPriorityHigh` en ZZZ e `IoPriorityVeryLow` en fondo) para erradicar tirones de streaming de modelos 3D y purga inteligente de Standby List en RAM.
- **Resultado Obtenido:**
  - Aumento a **~35 FPS estables**, con frametimes homogéneos, cero parones de texturas y una respuesta al mando inmediata.
  - **Cero riesgo de sanción:** Los archivos del juego y su espacio de memoria permanecen estrictamente inalterados.

---

