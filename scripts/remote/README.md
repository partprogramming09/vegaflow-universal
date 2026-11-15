# scripts/remote: operar el laptop de pruebas desde la Mac

Herramienta **solo de desarrollo**. No forma parte de VegaFlow ni se distribuye con él; un usuario que solo quiere jugar no la necesita.

Permite lanzar, desde la Mac, un conjunto cerrado de trabajos en la sesión del escritorio del laptop de pruebas (abrir `drawstress`, medir con PresentMon, arrancar o parar el daemon, hacer el A/B). La sesión SSH sola no sirve para esto: corre en la sesión 0 de Windows, sin escritorio, y desde ahí PresentMon no ve cuadros y el detector del daemon no ve ningún juego.

Los trabajos corren en el **ejecutor de trabajos de WinBridge** (`docs/JOBS.md` del repositorio de WinBridge), con el paquete `vegaflow`. El ejecutor propio anterior (`vfjob`, tarea `VegaFlow-Run`) está retirado (queda solo en el historial de git).

## Piezas

| Archivo | Dónde corre | Qué hace |
| --- | --- | --- |
| `winbridge-package/manifest.json` | laptop (paquete `vegaflow`) | Los trabajos permitidos: scripts, parámetros con su expresión regular, límite de tiempo, sesión 1 |
| `jobs/*.ps1` | laptop | Los trabajos: `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bg-factors` |
| `deploy_package.ps1` | laptop, administrador | Instala el paquete en el ejecutor de WinBridge y los archivos que usan las tareas `VegaFlow-*` |
| `setup_capture.ps1` + `capture_dda.py` | laptop, administrador | Instala la herramienta del trabajo `screenshot`: copia el Python a `C:\ProgramData\VegaFlow\tools\capture\python`, crea el venv con `dxcam` (Desktop Duplication, versiones fijas) y deja `capture_dda.py` al lado. Todo en una carpeta solo de administradores, porque el trabajo corre elevado y un Python de usuario (uv, `%LOCALAPPDATA%`) lo puede escribir cualquiera. Se lleva a mano: `scp` de los dos archivos a `C:\dev\vf-pkg` y `powershell -File C:\dev\vf-pkg\setup_capture.ps1` por SSH de administrador |
| `vfdev` | Mac | `run`, `submit`, `wait`, `status`, `cancel`, `busy`, `list` (a `winbridge jobs`), más `deploy`, `sync`, `build-test`, `cross-test`, `stats` |
| `vfstats.py` | Mac | Agrupa filas de resultados: n, media, mediana y rango |
| `vfframes.py` | Mac | Compara dos condiciones con los CSV de cuadros de `bench.ps1`: cuadros agrupados, intervalo de confianza del 1% Low y veredicto con los umbrales del plan de pruebas |

`pack` (`scripts/bench_pack.ps1`) mide una sola condicion con el juego ya abierto y empaqueta CSV, JSON, `--system-info` y el estado de los ajustes para llevarlos a otro equipo o analizarlos con `vfdev frames` y `vfdev regress`. `bench` y `ab` salen de `scripts/bench.ps1` y `scripts/ab_loop_task.ps1`. Las tareas `VegaFlow-Start`, `-Stop`, `-State` y `-Restore` (interactivas y elevadas, instaladas una vez por el usuario) siguen en uso: las usan `daemon-start`, `daemon-stop` y `ab`.

## Uso

Hace falta el cliente `winbridge` de la Mac (`cd <winbridge>/cli && go build -o ~/.local/bin/winbridge .`) o `WINBRIDGE_BIN` con su ruta.

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
scripts/remote/vfdev cross-test             # compila en la Mac, envía solo los .exe y sus datos al laptop y allí solo ejecuta las pruebas (el laptop no compila)
scripts/remote/vfdev stats ARCHIVO [PREFIJO]
scripts/remote/vfdev fetch PREFIJO [DESTINO]   # trae los JSON y CSV de cuadros de las capturas con esa etiqueta
scripts/remote/vfdev frames 'A/*.csv' 'B/*.csv' # compara dos condiciones con los cuadros agrupados
scripts/remote/vfdev regress scripts/remote/baselines/octopath_world.json 'NUEVAS/*.csv'   # PASS o FAIL contra la linea base de la escena
scripts/remote/vfdev run ab Api=d3d11 Process=juego.exe Reps=5 Seconds=120 Warmup=1 LabelPrefix=run1   # juego real: no se cierra al terminar
scripts/remote/vfdev run pack Process=juego.exe Label=escena-condicion Captures=5 Seconds=125   # paquete portatil: calentamiento + capturas + huella del equipo en bench\pack-*.zip
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

## Despliegue y hash del ejecutor

Las versiones nuevas de `Install-WinBridgeJobs.ps1` exigen el SHA-256 de `runner.ps1`. Hay que tomarlo del repositorio fuente de WinBridge (`bootstrap/jobs/runner.ps1`), nunca del `runner.ps1` instalado en el laptop, porque entonces la comprobación compararía un archivo consigo mismo:

```sh
export VFJOB_RUNNER_SHA256=$(shasum -a 256 <winbridge>/bootstrap/jobs/runner.ps1 | cut -c1-64)
scripts/remote/vfdev deploy
```

## Límites

- Un trabajo a la vez (cola máxima de 20) y solo sesión 1: no hay trabajos de sesión 0.
- Tras un reinicio hace falta iniciar sesión en el laptop; el inicio automático no funciona con la cuenta de Microsoft actual.
- No pulsa teclas (eso lo hace WinBridge). Para ver la pantalla: `vfdev run screenshot Name=x` y `scp` del PNG. Usa Desktop Duplication (`method=dda`), que ve juegos a pantalla completa; el GDI de antes salía negro en esos casos y queda como respaldo (`method=gdi`). Sin `setup_capture.ps1` instalado, cae al respaldo.
- El MCP `vfjob` ya no existe y se quitó de la configuración de Claude Code el 2026-11-05.
