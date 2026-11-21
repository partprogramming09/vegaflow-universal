# Fases E2 a E4: recursos, eGPU universal e IPC (solo planificadas)

**Lector:** personas y agentes. Estado: **sin empezar**; cada una se detalla en su archivo cuando le toque (código viejo frente al cambio, con `rg` de ese día).

## Origen
Documento de entrada: `vegaflow_plan_expansion_agentes.md` (auditoría de `v0.1.0`). Sus números de versión (v0.1.5 a v0.4.0) se renombran: la GUI «v0.2.0» de ese documento ya está construida en Fase G, así que E1 es un hotfix 0.2.x y el resto sigue en 0.x.

## E2 - Recursos y eventos (0.3)
- Purga escalonada `None` / `LowPriorityStandbyAndTrim` / `FullStandbyEmergency` en `policy/memory_purge.rs`; no recomendar quitar `MemCompression` con ≤16 GB.
- Disco del sistema y salud del Shader Cache (`--doctor`, GUI); prioridad de E/S baja a procesos `Throttle`.
- Bucle por eventos: `SetWinEventHook`, `CM_Register_Notification`, `PBT_APMPOWERSTATUSCHANGE`; temporizador de 2–3 s solo con juego activo y CPU 0 % en reposo.
- `--doctor` anonimizado y conflictos OEM. Cierre de `# Migration` en `check_layers.sh` pasando `gui_mode.cpp` a un controlador de sesión (esto choca con el trabajo pendiente de Fase G: se decide ahí).
- Extras GUI de Fase G aún sin cablear: ver [`14-fase-g.md`](14-fase-g.md).

## E3 - eGPU y HAL (0.4)
- Ejes ortogonales en `device_profile.rs`: `GpuConnection` (**ya hecho**, `device_profile.rs:79,331`), pendientes `PcieLinkClass`, `DisplayRouting`, `VfDecisionReason` (máscara `u64`).
- Sonda: ancho y velocidad PCIe (SetupAPI), loopback con `QueryDisplayConfig`, ReBAR, DirectStorage, SMBIOS de portátiles.
- Palancas eGPU: nada de recortes UMA; sin ASPM en AC; USB Selective Suspend reversible. Presets de Unreal bajo bandera experimental hasta medir ABBA.
- `IGpuVendorBackend`: AMD, NVIDIA (`nvapi64.dll` dinámica), Intel (tabla Arc y Core Ultra).

## E4 - Privilegios y tiendas (0.5)
- Daemon elevado + GUI estándar por Named Pipe con DACL (SYSTEM + usuario interactivo), mensajes versionados.
- `IGameLibraryScanner`: Steam, Epic, GOG, EA/Ubisoft, Xbox (pasivo), emuladores; motores Godot y RE Engine.

## Puntos transversales (se reparten entre fases)
`NtSetTimerResolution` por proceso en Win11 22H2+ (documentar en `SKILL.md`), `cbindgen` en CI, árbol padre-hijo de procesos, lista única de procesos excluidos desde Rust, log rotativo `logs/daemon.log` ≤2 MB.

## Riesgos
Ninguna eGPU real para probar E3; la IPC (E4) cambia el modelo de seguridad y exige revisión de `agy` y `/security-review`.

Volver: [E1](15-fase-e1.md) · [README](README.md)
