# Plan maestro: arquitectura nueva, Rust, seguridad y soporte universal

**Lector:** personas. Fecha: 2026-11-08. Estado: **aprobado el 2026-11-09**. Hechas: fase 0, R1 y R2 (2026-11-09), fase 1 y fase 1b (2026-11-10), fase 2, fase 2b, fase 3, fase 4, fase 5 y fase 6 (2026-11-12), fase 7 y fase 8 (2026-11-13), fase R3 (2026-11-14). Fase G: pantallas construidas en el código el 2026-11-15, con partes del diseño sin construir y decisiones pendientes (ver [`14-fase-g.md`](14-fase-g.md)); la versión 0.2.0 aún no se publica.

Este es el único plan del proyecto. Junta tres cosas que antes iban por separado:
- la **arquitectura nueva** y la modularización (decisión en el [ADR 0001](../../adr/0001-nucleo-que-decide-y-puertos.md));
- la **fase 2 de Rust** con los arreglos de seguridad;
- el **soporte universal** para Intel, AMD multi-CCD, GPU dedicada y eGPU. Antes estaba en `universal-amd-intel-egpu.md`; ese archivo queda en el historial de git.

La evidencia del código (archivo y línea, commit `61eed56`) va al final de cada fase. Las fuentes externas están en [`docs/investigacion/`](../../investigacion/README.md), con su fecha y su confianza. Todo sale de leer el código y la documentación; nada se ha reproducido todavía en Windows. Por eso cada fase empieza con una prueba que demuestre el fallo.

## Cómo está organizado

Un archivo por fase, en el orden en que se hacen. Lo común va aparte:

| Archivo | Qué contiene |
|---|---|
| [`producto.md`](producto.md) | Secciones 1 a 1f: por qué, para quién, qué aplica según la gráfica, niveles de madurez, requisitos y modo desarrollador |
| [`arquitectura.md`](arquitectura.md) | Secciones 2 a 3c: qué pasa a Rust, arquitectura de destino, módulos y librerías |
| ~~`00-fase-0.md`~~ | Fase 0: **hecha el 2026-11-09** (`538950b`); su pendiente de las DLL de DXVK está en [`fuera-del-plan.md`](fuera-del-plan.md) |
| ~~`01-fase-r1.md`~~ | Fase R1: **hecha el 2026-11-09** (commits `8fb9daf` a la revisión final; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/modulos.md`](../../arquitectura/modulos.md) |
| ~~`02-fase-r2.md`~~ | Fase R2: **hecha el 2026-11-09** (política inyectada, `ports/file_system.hpp` y `ports/clock.hpp`, `windows/unique_handle.hpp`; partir `gui_mode` pasó a la fase G). Resultado en [`docs/arquitectura/modulos.md`](../../arquitectura/modulos.md) |
| ~~`03-fase-1.md`~~ | Fase 1: **hecha el 2026-11-10** (decisión entera en Rust `policy/file_access.rs`, lector de `libraryfolders.vdf` en Rust, junctions y 8.3 resueltos, E/S comprobada por handle, carpetas permitidas sin enlaces; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/seguridad.md`](../../arquitectura/seguridad.md); pendientes en [`fuera-del-plan.md`](fuera-del-plan.md) |
| ~~`04-fase-1b.md`~~ | Fase 1b: **hecha el 2026-11-10** (`da5b0ee`, `574324e`, `45326bf`, `03bf476`: una sola ruta junto al `.exe` que solo se lee si nadie más pudo escribirla, lector JSON propio en Rust, rangos y nombres de juego validados, guardado sin perder claves; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/seguridad.md`](../../arquitectura/seguridad.md), punto 5; pendientes en [`fuera-del-plan.md`](fuera-del-plan.md) |
| ~~`05-fase-2.md`~~ | Fase 2: **hecha el 2026-11-12** (editor `.ini` y `boot.config` en Rust `policy/ini_merge.rs`, BOM/UTF-16/CRLF preservados, copia `.vegaflow.bak` comprobada byte a byte, Prueba 48 y `tests/vectors/ini/`; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/seguridad.md`](../../arquitectura/seguridad.md), punto 7 |
| ~~`06-fase-2b.md`~~ | Fase 2b: **hecha el 2026-11-12** (diario atómico `<exe>\config\change_journal.vfj`, modo seguro tras dos cierres anormales, limpieza de `--once`, historial en GUI, punto de restauración opcional, Pruebas 49 y 50; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/seguridad.md`](../../arquitectura/seguridad.md), punto 8; pendientes en [`fuera-del-plan.md`](fuera-del-plan.md) |
| ~~`07-fase-3.md`~~ | Fase 3: **hecha el 2026-11-12** (perfil universal del equipo y GPU en Rust `policy/device_profile.rs`, puerto `ports/device_probe.hpp` con DXCore + SetupAPI + almacenamiento `dwDesiredAccess=0` + energía + ARM64, detección de hot-plug y paso estático/vivo en `session/game_session.cpp`, `Engine.ini` sin recortes para dGPU/eGPU, Pruebas 51 y 52 y `tests/vectors/device_profiles.tsv`; el archivo se borró, la historia queda en git) |
| ~~`08-fase-4.md`~~ | Fase 4: **hecha el 2026-11-12** (regla única de purga y guardián UMA en Rust `policy/memory_purge.rs`, salida de `ntdll` de Rust a `windows/memory.*`, puertos `ports/memory_probe.hpp` y `ports/memory_control.hpp`, umbral único 0.85, pausa compartida de 90s, lectura de disco por vuelta sin dilución, eliminación del fallback `ullAvailPhys`, exclusiones de fábrica No tocar para OBS/Discord/llamadas, Prueba 53 y `tests/vectors/memory_purge.tsv`; el archivo se borró, la historia queda en git) |
| ~~`09-fase-5.md`~~ | Fase 5: **hecha el 2026-11-12** (clasificación de procesos y acciones en Rust `policy/process_class.rs`, verificación de ruta completa + DLL 3D + señales genéricas anti-cheat, puerto `ports/process_control.hpp` + `windows/process_control.*` con verificación PID+`creation_time`, devolución del juego anterior al cambiar de juego, exclusión "Me fue mal en este juego" en `ExcludeGameAndRevert`, Pruebas 54 y 55 y `tests/vectors/process_class.tsv`; el archivo se borró, la historia queda en git) |
| ~~`10-fase-6.md`~~ | Fase 6: **hecha el 2026-11-12** (eliminada la captura ETW de fotogramas `src/safety/frame_telemetry.*`, `-ltdh`, Test 7 y los campos de FPS en `--monitor`, GUI y `session_reports.log`; `--monitor` conserva RAM y VRAM; el archivo se borró, la historia queda en git) |
| ~~`11-fase-7.md`~~ | Fase 7: **hecha el 2026-11-13** (eliminada la copia C++ de reparto de CPU Sets; reparto dentro del grupo del proceso vía `group` en `VfCpuCoreEntry` y `vf_rust_partition_cpu_topology_group`; exclusión de LP E-cores en Core Ultra y soporte multi-grupo/Strix Point; Prueba 56 y `tests/vectors/cpu_topology.tsv`; el archivo se borró, la historia queda en git) |
| ~~`12-fase-8.md`~~ | Fase 8: **hecha el 2026-11-13** (6 familias inyectables en `GraphicsOptimizer` sin métodos estáticos detrás de `RegistryPort` y `PowerPort`, proveedor AMD y plan de energía centralizados, retirados el temporizador NT de 0,5 ms y MMCSS `GPU Priority` conservando limpieza de copias legadas, `OverlayTestMode` apagado por defecto y documentado, Prueba 57; el archivo se borró, la historia queda en git) |
| ~~`13-fase-r3.md`~~ | Fase R3: **hecha el 2026-11-14** (ningún caso de uso de `session/` llama a Win32: las lecturas de Windows están en `windows/system_counters.*`, `text_convert` pasó a `windows/` y `ui/app_identity.hpp` guarda versión y nombre de la ventana; `scripts/check_layers.sh` pasa; ADR 0001 aceptado; el archivo se borró, la historia queda en git). Resultado en [`docs/arquitectura/modulos.md`](../../arquitectura/modulos.md) y [`docs/arquitectura/patron-universal.md`](../../arquitectura/patron-universal.md). La medición del consumo en reposo se hace aparte, en [`docs/mediciones/`](../../mediciones/) |
| [`14-fase-g.md`](14-fase-g.md) | Fase G: GUI nueva (las correcciones de nombre del laptop, «UXTU» y textos que prometen mejoras ya están hechas; las pantallas están construidas en el código para la 0.2.0; falta parte del diseño y hay decisiones pendientes, ver «Estado de la construcción») |
| [`15-fase-e1.md`](15-fase-e1.md) | Fase E1: multi-GPU, eGPU en caliente y seguridad base (hotfix 0.2.x). **Por aprobar** |
| [`16-fase-e2-e4.md`](16-fase-e2-e4.md) | Fases E2 a E4: recursos y eventos, eGPU universal, IPC y tiendas. Solo planificadas |
| [`fuera-del-plan.md`](fuera-del-plan.md) | Secciones 6, 9 y 10: lo descartado, las ideas sin fase y lo que viene después del plan |

Las fuentes externas están en [`docs/investigacion/`](../../investigacion/README.md). Cuando una fase termina, lo que implementó pasa a `docs/arquitectura/` o `docs/guias/` y su archivo se borra (la historia queda en git); cuando terminan todas, se borra la carpeta.

## 4. Resumen de fases

| Fase | Qué | Por qué va en este puesto | Gravedad de lo que arregla |
|---|---|---|---|
| 0 | Preparar el laptop | Todas las fases se prueban allí | — |
| R1 | Mover sin cambiar lógica: `model/`, separar `session/`, repartir `test_main.cpp` (con `main` propio; sin doctest ni cbindgen, ver la tabla de librerías) y el script de capas. **Hecha** | Deja el terreno listo y corta el ciclo. Solo mueve código, así que es lo menos arriesgado | — |
| R2 | Puerto de archivos, puerto de reloj y RAII de Windows. **Hecha** | Las fases 1 y 2 los necesitan | — |
| 1 | Seguridad de archivos (lista blanca). **Hecha** | Hoy el daemon, como administrador, puede escribir en todo `C:\ProgramData` | **Alta** |
| 1b | Configuración segura (`config.json`). **Hecha** | Hoy se lee con una ruta relativa a la carpeta de trabajo, se guarda borrando lo que no conoce y no se valida; las fases 5 y G dependen de ella | **Alta** |
| 2 | Editor de `.ini` de juegos. **Hecha** | Puede perder la configuración original del juego | **Alta** |
| 2b | Diario de cambios. **Hecha** | Si el daemon se cierra a la fuerza, los cambios se quedan aplicados y nadie lo sabe | **Alta** |
| 3 | Perfil del equipo: CPU, GPU (con su **tipo de conexión**), RAM, disco, energía, pantalla, Windows y arquitectura. **Hecha** | Clasifica mal varias APU modernas y no distingue una eGPU | Media |
| 4 | Decidir cuándo liberar memoria | Reglas repetidas en dos lenguajes y una purga sin pausa | Media |
| 5 | Elegir qué procesos frenar | Puede dejar frenado un proceso que no toca | Media |
| 6 | Quitar la medición de fotogramas del daemon | VegaFlow no es un medidor; la captura usa números de evento que no son los de un fotograma | Media |
| 7 | Núcleos de CPU universales (Intel híbrido, multi-CCD, más de 64 hilos). **Hecha** | La regla está **copiada** en C++ para más de 64 hilos, y en Intel manda todo el fondo a todos los E-cores | Media |
| 8 | Ajustes de sistema por fabricante, detrás de un puerto de registro. **Hecha** | 36 funciones estáticas; varios ajustes de AMD se intentan sin mirar el fabricante | Baja |
| R3 | Cierre: `session/` sin llamadas a Win32, documentación y paquete 0.1.0. **Hecha** | Cierra el refactor | — |
| G | GUI nueva: 7 pantallas en lenguaje claro, con el estado de evidencia de cada ajuste (versión 0.2.0) | Necesita la configuración segura (1b), el diario de cambios (2b), los datos limpios de las fases 3 a 8 y la carpeta `session/` | Media: la GUI actual ya no muestra el nombre del laptop de pruebas ni promete mejoras, pero sigue sin decir qué está medido |
| — | Lector de cabecera `.exe` | **Se descarta**: no tiene fallo de memoria y hoy no se ejecuta | Baja |

La GUI nueva es la fase G. Las ideas que todavía no tienen fase están en la [fuera del plan](fuera-del-plan.md).

El orden de las fases 1 a 8 va de lo que más daño puede hacer a lo que menos. Las fases 1 y 2 tocan la seguridad y los archivos del usuario, así que van primero. En cada una de esas fases, la decisión pasa a `rust/.../policy/` y la acción se pone detrás de su puerto en `src/windows/`. Así la arquitectura nueva crece pieza a pieza, y lo viejo se apaga al final (patrón *strangler*).

```
+---------------------------------------------+
| Fase 0: laptop listo, línea base en verde   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| R1: mover sin cambiar lógica                |
| (mismas pruebas, mismo resultado)           |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| R2: puertos de archivos y reloj             |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Fases 1 a 8: regla a Rust + acción tras     |
| su puerto + arreglo de los fallos           |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| R3: cierre (hecha el 2026-11-14)            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| G: GUI nueva (lee lo que prepara session/)  |
+---------------------------------------------+
```

## 5. Cómo se hace cada fase (método común)

```
+---------------------------------------------+
| 1. Prueba que demuestra el fallo            |
|    Se escribe y se ve fallar en el laptop   |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 2. Ejemplos en tests/vectors/<pieza>.tsv    |
|    Los usan C++ y Rust por igual            |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 3. Función Rust nueva, con la regla         |
|    corregida y prueba primero (TDD)         |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 4. Función vf_rust_* en el puente           |
|    El C++ la llama                          |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 5. Correcciones del lado C++ (acciones)     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 6. En el laptop: suite C++ y Rust en verde, |
|    y tweaks_state.ps1 sin ningún valor      |
|    fuera de fábrica. Se borra la versión    |
|    C++ vieja                                |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| 7. Un commit por fase (confirmado el        |
|    2026-11-09). Pasas a la siguiente        |
|    cuando la apruebas                       |
+---------------------------------------------+
```

---

## 7. Cifras del plan

- Rust pasaría del 7,5 % a algo más del 11-12 % que se estimó contando líneas para las fases 1 a 6. Las fases 3 y 7 añaden algo, sin contar todavía.
- Pruebas nuevas: al menos una por cada fallo de las tablas, y un archivo de ejemplos en `tests/vectors/` por cada capacidad de la tabla de módulos que tiene regla.
- No hay estimación de horas. Cada fase es un commit y se aprueba antes de pasar a la siguiente.

## 8. Límites y riesgos

- Todos los fallos salen de leer el código. Cada fase empieza demostrando el suyo en el laptop. Si uno no se reproduce, se descarta y se dice.
- Comprobado en la fase 1: `\\?\C:\...` se trata como `C:\...` y pasa por la misma decisión; los demás prefijos `\\` se rechazan.
- **Con un solo equipo solo se verifica lo determinista.** Las reglas de Intel, X3D, dGPU, eGPU y más de 64 hilos se prueban con equipos simulados; su efecto real queda sin medir.
- **Una palanca sin efecto medido no va por defecto.** O pasa la puerta de evidencia (`scripts/bench.ps1` con A/B intercalado), o queda como opción apagada.
