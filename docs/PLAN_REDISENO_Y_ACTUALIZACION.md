# Plan de rediseño y método de actualización

Estado: borrador del 2026-11-04. Cada punto indica si está verificado (con su evidencia) o por verificar.

## 1. Lo que ya se sabe

Fuente: `docs/MEDICION_BASE.md` (Octopath Traveler II, escena fija, PresentMon 2.x).

- Con 12 tweaks viejos aplicados contra Windows de fábrica, el FPS medio no cambia y el 1% Low de fábrica es igual o algo mejor. Verificado.
- Un temporizador NT de 0.5 ms pedido por otro proceso no cambia `Sleep(1)` de un tercero (~15.4 ms en ambos casos). Verificado.
- Una ventana propia delante de un juego en `Legacy Flip` destruye los lows (1% Low de ~38 a ~16). Verificado.
- Las corridas justo tras reiniciar son atípicas; hay que descartar los primeros ~6 minutos. Verificado.
- La línea base de `drawstress` (bgfx, `dim` 30) varía ~10% entre corridas (D3D11 112 a 124.5 FPS, D3D12 108.9 a 119.2). Verificado, 3 corridas por API.

## 2. Problemas no considerados al diseñar el proyecto

| Problema | Estado | Evidencia |
| --- | --- | --- |
| Los efectos de proceso (CPU Sets, EcoQoS, prioridad I/O) solo alcanzan procesos que el detector clasifica como juego | Por verificar con `drawstress.exe` | `process_detector.cpp`: primer plano con `d3d11.dll`/`d3d12.dll`/`vulkan-1.dll` o `custom_game_exes` |
| `Tasks\Games` (MMCSS) solo afecta a hilos que se registran como "Games"; un motor que no lo hace no recibe nada | Por verificar | Mecanismo documentado de MMCSS, sin medición propia |
| El daemon continuo aplica GameDVR/FSE y el perfil de energía APU al arrancar y, al cerrar, solo restaura si había un juego activo | Verificado por lectura de código | `main.cpp`: llamadas al arrancar frente al bloque de cierre condicionado a `game_active` |
| La línea base nunca fue un Windows de fábrica: siguen puestos `UMD\Tessellation=16`, `TurboSync`, `FlipQueueSize`, `TFQ`, `Main3D_DEF` y 13 entradas de `UserGpuPreferences` | Verificado | Informe de `VegaFlow-State` del 2026-11-04 |
| Esos valores del driver no tienen valor de fábrica confirmado, así que `VegaFlow-Restore` no los toca | Verificado | `tweaks_state.ps1` los marca "solo informe" |
| Una acción elevada necesita un UAC que WinBridge no puede pulsar | Verificado | `Diagnose` con `input_desktop: secure` |
| Una sesión SSH no tiene credenciales de GitHub ni `g++` en el PATH | Verificado | `git fetch` falló con `wincredman`; `g++` está en `scoop\apps\mingw\16.2.0-rt_v14-rev1\bin` |
| `CLAUDE.md` enlaza rutas `file:///C:/Users/testuser/OneDrive/...` del proyecto original | Verificado | Aparecen en las secciones 3 y 4 |
| El estado de los tweaks no se comprueba antes de medir | Resuelto con `VegaFlow-State` | `scripts/tweaks_state.ps1` |

## 3. Principios del rediseño

1. Ninguna afirmación de rendimiento sin una medición propia que supere el ruido entre corridas.
2. Cada tweak tiene un interruptor individual y una reversión verificable, y se informa si no tiene valor de fábrica confirmado.
3. Las pruebas A/B se intercalan (A, B, A, B) y leen el estado del sistema antes de cada captura.
4. Nada elevado ejecuta código de una carpeta escribible por el usuario: todo vive en `C:\ProgramData\VegaFlow` con hash registrado.
5. Lo que no mejora de forma demostrable se retira o se deja apagado por defecto.

## 4. Método de actualización

Modelo: `winbridge update` (paquete verificado por hash, por SSH, sin UAC, con reversión).

1. En la Mac: compilar o cross-compilar, generar `bundle.zip` con el daemon, los scripts y un `manifest.json` con SHA-256 de cada archivo.
2. Enviar el paquete por `scp` a una carpeta de preparación del laptop (sin credenciales de GitHub).
3. En el laptop, sin elevación: verificar los hashes y las 26 pruebas del paquete; si fallan, no se instala nada.
4. Con la tarea elevada `VegaFlow-Update` (se registra una sola vez): copiar a `C:\ProgramData\VegaFlow\staging`, conservar la versión anterior en `previous\`, intercambiar y registrar el hash instalado en `setup.log`.
5. Humo posterior: `VegaFlow-State` sin diferencias inesperadas; si falla, reversión automática a `previous\`.

Estado: pasos 1 a 3 posibles hoy; el paso 4 necesita una tarea nueva y un UAC único.

## 5. Fases

1. Instrumento: confirmar que `perf_record` con `drawstress.exe` produce lows con PresentMon o usar `scripts/bench.ps1`.
2. A/B intercalado de `drawstress` (D3D11 y D3D12): fábrica contra daemon activo, con `VegaFlow-Start` y `VegaFlow-Stop`.
3. Auditoría: contrastar cada afirmación de `CLAUDE.md` con una medición y marcar las no probadas.
4. Reorganización: retirar o apagar lo que no se demuestre; actualizar `CLAUDE.md` y las rutas.
5. Método de actualización por paquete (sección 4).
