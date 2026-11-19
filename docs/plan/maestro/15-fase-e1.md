# Fase E1: multi-GPU, eGPU en caliente y seguridad base (hotfix 0.2.x)

**Lector:** personas y agentes. Fecha: 2026-11-18. Estado: **por aprobar**. Rama: `feat/expansion-e1` (sale de `feat/fase-g-gui`). Origen: [`vegaflow_plan_expansion_agentes.md`](16-fase-e2-e4.md#origen) §1 y la auditoría HTML; cada punto se re-comprobó con `rg` el 2026-11-18 (commit `4f5d34d`).

## Por qué
Con una sola APU todo cuadra. Al enchufar una eGPU o tener dos GPU AMD, el daemon mide, escribe y decide sobre el adaptador equivocado. Esta fase cierra eso sin cambiar el comportamiento en el banco de pruebas (Ryzen 5 4600H + Vega 6).

## Qué se comprobó (código vigente)

| # | Hallazgo | Estado | Evidencia |
|---|---|---|---|
| 1.1 | DXGI mide el primer adaptador de hardware | **vigente** (leído) | `src/windows/memory.cpp:172-193` (`EnumAdapters1` + `break`) |
| 1.2 | `policy` y `gfx` se crean una vez | **vigente** (leído) | `src/app/daemon_loop.cpp:36,40`; `gui_mode.cpp` igual |
| 1.3a | `AmdDriverTweaks` escribe en todas las subclaves AMD | **vigente** (leído) | `amd_driver_tweaks.cpp:82,122` (`kAdapterSubkeys`) |
| 1.3b | Fallback de 512 MB en VRAM | **ya hecho** (leído) | `vram_reporting.cpp:55` ya no escribe sin copia; solo falta la prueba y el bloqueo si no es `Integrated` |
| 1.4 | Flip: `GpuPreference=2`, `AutoHDREnable=0`, `AutoFlipModel`, `~ HIGHDPIAWARE` | **vigente** (leído) | `dxgi_flip.cpp:58,65-67,93,109,116-118` |
| 1.4b | `GameDVR_FSEBehaviorMode` | **vigente** (leído) | `display_tweaks.cpp:64` |
| 1.5a | Raíces con `contains` sin anclar (`C:\WindowsX`) | **vigente** (leído) | `process_class.rs:128-160` |
| 1.5b | `DisablePagingExecutive` activo por defecto | **vigente** (leído) | `kernel_tweaks.cpp:68` |
| 1.5c | `RestoreAllBackups` sin lista blanca | **por comprobar** (inferido) | `registry.cpp:463` |
| 1.5d | Callbacks FFI sin `noexcept`; subcarpetas en `CanModifyGameDir` | **por comprobar** (inferido) | `engine_presets.cpp`, `sandbox_guard.cpp` |
| 1.6 | Doble muestreo y GDI sin caché en `gui_mode.cpp` | **por comprobar** (inferido) | `gui_mode.cpp:155` |

Lo que resulte ya resuelto se marca «ya hecho» con su commit; no se borra en silencio.

## Subfases (cada una: prueba que falla → código → verde → commit)

| Sub | Función entregada | Regla pura (Rust) / efecto (C++) | Pruebas con casos borde |
|---|---|---|---|
| E1.1 | Telemetría VRAM del adaptador activo | C++: `SampleSystemMemory(target_luid)`; `EnumAdapterByLuid` y, si falla, recorrido por `AdapterLuid` | `FakeMemoryProbe`: LUID pedido, LUID cambia, LUID inexistente (cae a invalido, no al adaptador 0), `0` = comportamiento actual |
| E1.2 | `GraphicsOptimizer` se reconstruye al cambiar el hardware | `Rebind(hw, policy)`; restaura UMA antes de pasar a eGPU; asiento en `ChangeJournal` | Integrada → externa → integrada; dos cambios seguidos; cambio durante sesión de juego; `dry_run` |
| E1.3 | Escritura AMD solo en el adaptador objetivo | filtro por `MatchingDeviceId` (`ven_1002&dev_`); `ApplyVramReportingOverride` bloqueado si `gpu_connection != Integrated` | `FakeRegistryPort` con APU + eGPU AMD; sin respaldo nunca escribe 512 MB; subclave sin `MatchingDeviceId` |
| E1.4 | Flip, FSE y HDR sin pisar al usuario | solo `SwapEffectUpgradeEnable=1`; `HIGHDPIAWARE` se anexa y se quita solo ese token; `GameDVR_FSEBehaviorMode` solo como opción de compatibilidad | `GpuPreference` previo se conserva; `AppCompatFlags` con otras banderas; Windows 11 ≥22000 |
| E1.5 | Seguridad base | Rust: raíces ancladas con `\` (vectores adversarios en `process_class.tsv`); C++: lista blanca en `RestoreAllBackups`, `noexcept` + `try/catch` en callbacks, subcarpetas ≤4 en `CanModifyGameDir`, `DisablePagingExecutive` apagado por defecto en UMA | `C:\WindowsX\`, `D:\Games\Program Files\`; ruta de respaldo fuera de la lista; `bad_alloc` en callback; anti-cheat a profundidad 4 y 5 |
| E1.6 | GUI sin trabajo repetido | reutilizar telemetría de `RunSingleIteration`; `journal.Revision()`; `HBRUSH`/`HPEN` en `GuiWindowState` | revisión sin cambio no regenera; hardware sin cambio no reconstruye `SystemInfo` |

Orden: E1.5 (Rust puro) → E1.3 → E1.4 → E1.1 → E1.2 → E1.6. El que más depende es E1.2, que usa E1.1 y E1.3.

## Cómo se prueba y cuándo se cierra
- Rust: `cargo test --manifest-path rust/vegaflow-core/Cargo.toml` (hoy 252).
- C++ y suite completa: `scripts/remote/vfdev cross-test` (hoy 310); no interrumpir el guardián de restauración.
- `bash scripts/check_layers.sh` en `[OK]`.
- Real en el laptop (un solo adaptador): `vfdev run state` antes y después; el registro vuelve igual con `vfdev run restore`. **No** se prueba una eGPU real: no hay una; E1.2 se prueba con puertos falsos. Eso queda como «no probado en hardware» en `docs/mediciones/estado-de-verificacion.md`.
- Se cierra cuando: cada fila tiene su prueba en verde, la memoria en reposo sigue `< 15 MB`, docs sincronizadas y `agy` revisó E1.3, E1.5 (atacante en Rust).

## Fuera de esta fase
Purga escalonada, eventos Win32, `--doctor`, eGPU/PCIe/HAL (E2 a E4, ver [`16-fase-e2-e4.md`](16-fase-e2-e4.md)).

Volver: [README](README.md) · Siguiente: [E2 a E4](16-fase-e2-e4.md)
