# Fase E1: multi-GPU, eGPU en caliente y seguridad base (hotfix 0.2.x)

**Lector:** personas y agentes. Fecha: 2026-11-18. Estado: **por aprobar**. Rama: `feat/expansion-e1` (sale de `feat/fase-g-gui`). Origen: [`vegaflow_plan_expansion_agentes.md`](16-fase-e2-e4.md#origen) §1 y la auditoría HTML; cada punto se re-comprobó con `rg` el 2026-11-18 (commit `4f5d34d`).

## Por qué
Con una sola APU todo cuadra. Al enchufar una eGPU o tener dos GPU AMD, el daemon mide, escribe y decide sobre el adaptador equivocado. Esta fase cierra eso sin cambiar el comportamiento en el banco de pruebas (Ryzen 5 4600H + Vega 6).

## Qué se comprobó (código vigente)

| # | Hallazgo | Estado | Evidencia |
|---|---|---|---|
| 1.1 | DXGI mide el primer adaptador de hardware | **hecho E1.1** (`f758a97`, `eb59ab0`) | `src/windows/memory.cpp:172-193` (`EnumAdapters1` + `break`) |
| 1.2 | `policy` y `gfx` se crean una vez | **hecho E1.2** (`8c97032`, Prueba 99) | `src/app/daemon_loop.cpp:36,40`; `gui_mode.cpp` igual |
| 1.3a | `AmdDriverTweaks` escribe en todas las subclaves AMD | **hecho E1.3** (`9d185e6`) | `amd_driver_tweaks.cpp:82,122` (`kAdapterSubkeys`); además `vram_reporting.cpp:37,71,132` filtra `DirectX` solo por `IsAmdDirectXSubkey` (`VendorId==0x1002`, `graphics_internal.hpp:113`): también hay que filtrar por adaptador (reproducido) |
| 1.3b | Fallback de 512 MB en VRAM | **ya hecho** (leído) | `vram_reporting.cpp:55` ya no escribe sin copia; solo falta la prueba y el bloqueo si no es `Integrated` |
| 1.4 | Flip: `GpuPreference=2`, `AutoHDREnable=0`, `AutoFlipModel`, `~ HIGHDPIAWARE` | **hecho E1.4** (2026-11-20, Pruebas 94 y 95) | `dxgi_flip.cpp:58,65-68,93,109,116-119,144`; `ApplyDirectFlipUpgrade` también escribe `DirectFlip=1` y sobrescribe `~ HIGHDPIAWARE` (reproducido) |
| 1.4b | `GameDVR_FSEBehaviorMode` | **hecho E1.4** (2026-11-20, Pruebas 94-96): Flip solo escribe `SwapEffectUpgradeEnable=1`, `HIGHDPIAWARE` se anexa, FSE pasa a la fila oculta `fse_compat`; `ApplyDirectFlipUpgrade` y `RemoveFlipModelUpgrade` borrados | `display_tweaks.cpp:64` |
| 1.5a | Raíces con `contains` sin anclar (`C:\WindowsX`) | **hecho E1.5** (`dfab8e5`; ataque de cierre `bb9752d`: `cmd.exe` y `powershell.exe` aceptaban `\windows\` anidado, UNC y `..`) | `process_class.rs:128-160` |
| 1.5b | `DisablePagingExecutive` activo por defecto | **ya hecho** (leído, 2026-11-18): solo se escribe desde la GUI (`settings_actions.cpp:122`), con la fila oculta como experimental, sin aplicarse sola y sin «Recomendado»; se añadió el aviso de APU al texto | `settings_catalog.cpp:33`, `kernel_tweaks.cpp:68` |
| 1.5c | **Crítico**: `RegRestoreRecord` toma `hive` y `subkey` del propio respaldo (`registry.cpp:234-262`); un respaldo plantado en HKCU puede escribir en HKLM. Pasa por `RestoreAllBackups` (`:463`) y por `RestoreBackedUpValue` (`:456`) | **hecho E1.5** (`0aa9d45`, `eb59ab0`) | `registry.cpp:234-262,456-485` |
| 1.5d | Callbacks FFI sin `noexcept` (0 coincidencias con `rg`); subcarpetas en `CanModifyGameDir` | **hecho E1.5** (`06fcab2`, `486325e`; desde `db98ef4` un juego con más de 20 000 entradas se trata como protegido) | `engine_presets.cpp`, `src/session/config.cpp:108,214`, `apu_shadow_config.cpp:134,268,315`; `windows/safe_file_system.cpp:41` (no `sandbox_guard.cpp`) |
| 1.6 | Doble muestreo y GDI sin caché | **hecho E1.6** (2026-11-21, Pruebas 100 y 101): cada sondeo de la GUI muestreaba la memoria 3 veces, releía los ajustes del registro con un `GraphicsOptimizer` nuevo y leía la tabla SMBIOS; ahora `SystemInfo::FromHardware(hw, mem, tweaks)` usa lo que la GUI ya tiene y los DIMM se leen una vez; el historial se regenera solo si cambia `Revision()`. GDI: cada `Create*` tiene su `DeleteObject` (sin fuga), sin caché por no tener beneficio medible | `gui_mode.cpp:174,264`; pinceles y plumas en `src/ui/gui_components.cpp:18-178` y `src/ui/*_view.cpp` |

Lo que resulte ya resuelto se marca «ya hecho» con su commit; no se borra en silencio.

## Subfases (cada una: prueba que falla → código → verde → commit)

| Sub | Función entregada | Regla pura (Rust) / efecto (C++) | Pruebas con casos borde |
|---|---|---|---|
| E1.1 | Telemetría VRAM del adaptador activo | C++: `SampleSystemMemory(target_luid)`; `EnumAdapterByLuid` y, si falla, recorrido por `AdapterLuid` | `FakeMemoryProbe`: LUID pedido, LUID cambia, LUID inexistente (cae a invalido, no al adaptador 0), `0` = comportamiento actual |
| E1.2 | `GraphicsOptimizer` se reconstruye al cambiar el hardware | `Rebind(hw, policy)`; restaura UMA antes de pasar a eGPU; asiento en `ChangeJournal` | Integrada → externa → integrada; dos cambios seguidos; cambio durante sesión de juego; `dry_run` |
| E1.3 | Escritura AMD solo en el adaptador objetivo | filtro por `MatchingDeviceId` (`ven_1002&dev_`) en `Class\{4d36e968}` **y** por `DeviceId`/LUID en `DirectX` (`vram_reporting`); `ApplyVramReportingOverride` bloqueado si `gpu_connection != Integrated` | `FakeRegistryPort` con APU + eGPU AMD; sin respaldo nunca escribe 512 MB; subclave sin `MatchingDeviceId` |
| E1.4 | Flip, FSE y HDR sin pisar al usuario | solo `SwapEffectUpgradeEnable=1`; `HIGHDPIAWARE` se anexa y se quita solo ese token; `GameDVR_FSEBehaviorMode` solo como opción de compatibilidad | `GpuPreference` previo se conserva; `AppCompatFlags` con otras banderas; Windows 11 ≥22000 |
| E1.5 | Seguridad base | Rust: raíces ancladas **al inicio tras la letra de unidad** con `starts_with` sobre ruta normalizada (anclar con `\` no basta: `\program files\` ya lo tiene y sigue casando `D:\Games\Program Files\`; vectores adversarios en `process_class.tsv`); C++: `RegRestoreRecord` exige que el `hive` del respaldo sea el de su registro y lista blanca en `RestoreAllBackups` **y** `RestoreBackedUpValue`, `noexcept` + `try/catch` en callbacks de `engine_presets.cpp`, `config.cpp` y `apu_shadow_config.cpp`, subcarpetas ≤4 (y junctions) en `CanModifyGameDir`, `DisablePagingExecutive` apagado por defecto en UMA | `C:\WindowsX\`, `D:\Games\Program Files\`; respaldo HKCU con `hive=HKLM` (debe rechazarse); ruta de respaldo fuera de la lista; `bad_alloc` en callback; anti-cheat a profundidad 4 y 5 |
| E1.6 | GUI sin trabajo repetido | reutilizar telemetría de `RunSingleIteration`; `journal.Revision()`; `HBRUSH`/`HPEN` en `GuiWindowState` | revisión sin cambio no regenera; hardware sin cambio no reconstruye `SystemInfo` |

Orden: E1.5 (Rust puro y respaldos) → E1.1 → E1.3 → E1.4 → E1.2 → E1.6 (E1.3 necesita la identidad del adaptador de E1.1; E1.2 usa las dos). E1.2 añade pruebas de fallo parcial al restaurar UMA y de `Rebind` en `gui_mode.cpp:56`. Límite de E1.2 (decidido 2026-11-20): `Rebind` solo devuelve el override de VRAM de la iGPU anterior; al volver a la iGPU no lo reaplica (eso queda en el arranque con admin), para no escribir el registro en cada conexión y desconexión; el plan de energía conserva su objeto y su copia guardada. E1.4 prueba también la reversión conservando otras banderas de `AppCompatFlags`.

## Cómo se prueba y cuándo se cierra
- Rust: `cargo test --manifest-path rust/vegaflow-core/Cargo.toml` (hoy 252).
- C++ y suite completa: `scripts/remote/vfdev cross-test` (hoy 310); no interrumpir el guardián de restauración.
- `bash scripts/check_layers.sh` en `[OK]`.
- Real en el laptop (un solo adaptador): `vfdev run state` antes y después; el registro vuelve igual con `vfdev run restore`. **No** se prueba una eGPU real: no hay una; E1.2 se prueba con puertos falsos. Eso queda como «no probado en hardware» en `docs/mediciones/estado-de-verificacion.md`.
- Se cierra cuando: cada fila tiene su prueba en verde, la memoria en reposo sigue `< 15 MB`, docs sincronizadas y `agy` revisó E1.3, E1.5 (atacante en Rust).

## Cierre (2026-11-21)

| Condición | Resultado | Etiqueta |
|---|---|---|
| Rust | 270 pruebas en verde (258 + 12 del atacante) | medido |
| C++ | 323/323 con `vfdev cross-test` | medido |
| Capas | `check_layers.sh` `[OK]`, 392 includes | medido |
| Laptop | `vfdev run state`: todas las filas `ok` | medido |
| Memoria en reposo | binario de E1 en `--observe` por SSH, 3 muestras a 30/60/90 s: 1,9 MB privada y 10,0 MB de working set (antes, 2026-11-15: 2,0 y 10,2) | medido |
| Revisión `agy` | revisor sobre todo E1: 3 hallazgos reproducidos y arreglados en `db98ef4` (escaneo anti-cheat que paraba en 20 000 entradas y daba «limpio», override de VRAM con `dry_run` en una eGPU, falso «no se pudo devolver la VRAM» sin pool) | medido |
| Atacante `agy` | sobre el anclaje de rutas de E1.5 en Rust: 7 de sus 9 pruebas rojas eran fallos reales y se arreglaron en `bb9752d`; se descartaron 3 casos (2 pruebas y una línea de otra) porque Windows nunca entrega esas rutas (`C:` sin barra con otro nombre de ejecutable, prefijo `\\?/` mezclado, unidad con el signo Kelvin) | medido |
| eGPU real | no probada en hardware: no hay una; E1.2 solo con puertos falsos | — |

## Fuera de esta fase
Purga escalonada, eventos Win32, `--doctor`, eGPU/PCIe/HAL (E2 a E4, ver [`16-fase-e2-e4.md`](16-fase-e2-e4.md)).

Diagrama: [`expansion-fases.svg`](../../diagrams/expansion-fases.svg). Revisión de `agy` (`neon-medium`) aplicada el 2026-11-18: 8 hallazgos reproducidos.

Volver: [README](README.md) · Siguiente: [E2 a E4](16-fase-e2-e4.md)
