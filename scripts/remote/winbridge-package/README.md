# Paquete de VegaFlow para el ejecutor de WinBridge

Los 10 trabajos de `vfjob` (`state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bench`, `ab`, `bg-factors`) como paquete del ejecutor de WinBridge (`/Volumes/dev/winbridge/docs/JOBS.md`). `manifest.json` tiene los mismos parámetros, regex y límites que `install_runner.ps1`, más `env.PRESENTMON_EXE`. Los scripts son los mismos archivos de este repositorio, sin cambios:

| Trabajo | Script |
| --- | --- |
| `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bg-factors` | `scripts/remote/jobs/<nombre con _>.ps1` (`daemon_start.ps1`, `kill_game.ps1`, `bg_factors.ps1`...) |
| `bench` | `scripts/bench.ps1` |
| `ab` | `scripts/ab_loop_task.ps1` |

## Instalar o actualizar (sin UAC, por SSH como administrador)

En el laptop: copiar `manifest.json` a `C:\Users\testuser\AppData\Local\winbridge\bin\jobs\packages\vegaflow\` y los 10 scripts de la tabla a su subcarpeta `scripts\`; luego:

```
powershell -File C:\Users\testuser\AppData\Local\winbridge\bin\jobs\Install-WinBridgeJobs.ps1 -Packages C:\Users\testuser\AppData\Local\winbridge\bin\jobs\packages -AgentUser testuser -Update
```

Uso: `winbridge jobs run bench Process=drawstress.exe Label=x Seconds=15 WarmupSeconds=5`, `winbridge jobs run ab Api=d3d11 Reps=5 ...` (igual que `vfjob run`, con `Clave=valor`).

## Lo que cambia respecto a `vfjob`

- **Resultados de `bench` y `ab`:** los scripts los escriben en `<carpeta de scripts>\..\bench\results`, es decir `C:\ProgramData\WinBridgeJobs\bench\results` (antes `C:\ProgramData\VegaFlow\jobs\bench\results`). Hay que adaptar lo que los lea (`vfjob stats`, `sync`).
- **Entorno de los trabajos:** `PATH` solo de sistema, `PSModulePath` de sistema, `TEMP` = `C:\ProgramData\WinBridgeJobs\tmp`. Si un trabajo necesita algo más en el `PATH`, va en `env` del manifiesto.
- **Dependen de la instalación vieja** (sigue en `C:\ProgramData\VegaFlow`): las tareas `VegaFlow-Start/Stop/State/Restore`, el daemon, `tweaks_state.ps1`, `tools\` (PresentMon, drawstress, busy.html). No retirar esa carpeta ni esas tareas; solo la tarea `VegaFlow-Run` y `vfjob` cuando se haya comparado una medición.
- Un trabajo a la vez, cola máxima de 20, valores que no empiezan por `-` ni llevan comillas, 200 bytes como máximo.
- `game`/`drawstress` y `PresentMon` se lanzan elevados desde el ejecutor; con una ventana elevada delante, el `Click/Type/Move` de WinBridge falla (lo dice el error).

## Estado (2026-11-05)

`vfjob` está retirado: la tarea `VegaFlow-Run` está deshabilitada (no borrada) y sus archivos están en `scripts/remote/archive/`. Los trabajos corren aquí; se instalan con `scripts/remote/vfdev deploy`. Probado en el ejecutor nuevo: `state`, `doctor` y un `bg-factors` corto en D3D12 con `Diag=1`.
