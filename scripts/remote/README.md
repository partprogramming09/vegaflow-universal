# scripts/remote: operar el laptop de pruebas desde la Mac

Herramienta **solo de desarrollo**. No forma parte de VegaFlow ni se distribuye con él; un usuario que solo quiere jugar no la necesita.

Permite lanzar, desde la Mac, un conjunto cerrado de trabajos en la sesión del escritorio del laptop de pruebas (abrir `drawstress`, medir con PresentMon, arrancar o parar el daemon, hacer el A/B). La sesión SSH sola no sirve para esto: corre en la sesión 0 de Windows, sin escritorio, y desde ahí PresentMon no ve cuadros y el detector del daemon no ve ningún juego.

Los trabajos corren en el **ejecutor de trabajos de WinBridge** (`/Volumes/dev/winbridge/docs/JOBS.md`), con el paquete `vegaflow`. El ejecutor propio anterior (`vfjob`, tarea `VegaFlow-Run`) está retirado y archivado en [`archive/`](archive/).

## Piezas

| Archivo | Dónde corre | Qué hace |
| --- | --- | --- |
| `winbridge-package/manifest.json` | laptop (paquete `vegaflow`) | Los trabajos permitidos: scripts, parámetros con su expresión regular, límite de tiempo, sesión 1 |
| `jobs/*.ps1` | laptop | Los trabajos: `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bg-factors` |
| `deploy_package.ps1` | laptop, administrador | Instala el paquete en el ejecutor de WinBridge y los archivos que usan las tareas `VegaFlow-*` |
| `vfdev` | Mac | `run`, `submit`, `wait`, `status`, `cancel`, `busy`, `list` (a `winbridge jobs`), más `deploy`, `sync`, `build-test`, `stats` |
| `vfstats.py` | Mac | Agrupa filas de resultados: n, media, mediana y rango |

`bench` y `ab` salen de `scripts/bench.ps1` y `scripts/ab_loop_task.ps1`. Las tareas `VegaFlow-Start`, `-Stop`, `-State` y `-Restore` (interactivas y elevadas, instaladas una vez por el usuario) siguen en uso: las usan `daemon-start`, `daemon-stop` y `ab`.

## Uso

Hace falta el cliente `winbridge` de la Mac (`cd /Volumes/dev/winbridge/cli && go build -o ~/.local/bin/winbridge .`) o `WINBRIDGE_BIN` con su ruta.

```sh
scripts/remote/vfdev run doctor
scripts/remote/vfdev run game Api=d3d11 Dim=30
scripts/remote/vfdev run ab Api=d3d11 Reps=5 Seconds=30 LabelPrefix=run1 Load=chrome
scripts/remote/vfdev run bg-factors Mode=confine Workers=4 Reps=3 Seconds=30 Diag=1
scripts/remote/vfdev busy                   # que hay en curso o en cola
scripts/remote/vfdev cancel ID              # cancela un trabajo y su arbol de procesos
scripts/remote/vfdev deploy                 # instala el paquete y el daemon compilado (administrador por SSH, sin UAC)
scripts/remote/vfdev sync                   # lleva los commits de la rama actual al repositorio del laptop (bundle + fast-forward)
scripts/remote/vfdev build-test             # compila y ejecuta las pruebas en el laptop; sale con error si alguna falla
scripts/remote/vfdev stats ARCHIVO [PREFIJO]
```

`ab` y `bg-factors` abren `drawstress` solos si no está abierto y lo cierran al terminar. `build-test` se rechaza mientras hay un trabajo en curso (`VFJOB_QUEUE=1` lo salta). Variables: `VFJOB_HOST` (usuario@ip), `VFJOB_KEY` (llave SSH), `WINBRIDGE_BIN`, `VFJOB_REPO` (repositorio en el laptop), `VFJOB_MINGW` (carpeta `bin` de g++), `VFJOB_BASE` (rama base del bundle, `main`).

## Cómo medir

- Compara condiciones solo dentro del mismo trabajo (el mismo juego abierto): con el fondo confinado el FPS es bimodal entre trabajos (~100-105 o ~107-114 en el mismo escenario) y pasa igual en cualquier ejecutor.
- Al menos 3 pasadas por condición, intercaladas, y reporta la mediana (`vfdev stats` la da).
- Un trabajo a la vez y no usar el laptop mientras corre.

## Seguridad

- El ejecutor corre elevado en la sesión del escritorio. Solo ejecuta scripts del paquete instalado en la carpeta del ejecutor (solo administradores pueden escribirla) y comprueba su SHA-256; los parámetros se validan con una expresión regular y no pueden empezar por `-` ni llevar comillas.
- Cambiar qué trabajos se permiten exige `vfdev deploy` con la llave SSH de un administrador.
- Los trabajos reciben un `TEMP` propio y protegido del ejecutor: cualquier cosa que deba escribir el usuario (perfiles de navegador) necesita una ruta explícita, como hacen `bg_factors.ps1` y `ab_loop_task.ps1`.
- La carpeta de VegaFlow se fija con SID (`*S-1-5-32-544`, `*S-1-5-18`, `*S-1-5-32-545`): los nombres en texto fallan en un Windows en español. `doctor` informa `carpeta_escribible_por_usuarios`.
- PresentMon y `drawstress` se copiaron desde una carpeta del usuario sin verificar su firma.

## Límites

- Un trabajo a la vez (cola máxima de 20) y solo sesión 1: no hay trabajos de sesión 0.
- Tras un reinicio hace falta iniciar sesión en el laptop; el inicio automático no funciona con la cuenta de Microsoft actual.
- No ve la pantalla ni pulsa teclas (eso lo hace WinBridge).
- El MCP `vfjob` ya no existe: si sigue registrado en Claude Code, quitarlo con `claude mcp remove vfjob`.
