# scripts/remote: operar el laptop de pruebas desde la Mac

Herramienta **solo de desarrollo**. No forma parte de VegaFlow ni se distribuye con él; un usuario que solo quiere jugar no la necesita.

Permite lanzar, desde la Mac, un conjunto cerrado de trabajos en la sesión del escritorio del laptop de pruebas (abrir `drawstress`, medir con PresentMon, arrancar o parar el daemon, hacer el A/B). La sesión SSH sola no sirve para esto: corre en la sesión 0 de Windows, sin escritorio, y desde ahí PresentMon no ve cuadros y el detector del daemon no ve ningún juego.

Los trabajos corren en el **ejecutor de trabajos de WinBridge** (`docs/JOBS.md` del repositorio de WinBridge), con el paquete `vegaflow`. El ejecutor propio anterior (`vfjob`, tarea `VegaFlow-Run`) está retirado (queda solo en el historial de git).

## Piezas

| Archivo | Dónde corre | Qué hace |
| --- | --- | --- |
| `winbridge-package/manifest.json` | laptop (paquete `vegaflow`) | Los trabajos permitidos: scripts, parámetros con su expresión regular, límite de tiempo, sesión 1 |
| `jobs/*.ps1` | laptop | Los trabajos: `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bg-factors`, `gui`, `kill-gui`, `gui-keys`, `clip` (muestra el portapapeles solo si es el informe de VegaFlow), `gui-second` (segundo arranque con la GUI abierta), `package-test` (prueba del paquete con el zip y el script que deja `vfdev package-test` en `C:\ProgramData\VegaFlow\pkgtest`; comprueba su SHA-256 y guarda la salida entera en `pkgtest\out\package-test-<fecha>.txt`), `dpi` (`Percent=100/125/150/175/200` cambia la escala de pantalla y `Percent=original` devuelve la guardada), `octo` y `octo-close` (Octopath sin nadie delante: ver «Octopath sin intervención») |
| `deploy_package.ps1` | laptop, administrador | Instala el paquete en el ejecutor de WinBridge y los archivos que usan las tareas `VegaFlow-*` |
| `setup_capture.ps1` + `capture_dda.py` | laptop, administrador | Instala la herramienta del trabajo `screenshot`: copia el Python a `C:\ProgramData\VegaFlow\tools\capture\python`, crea el venv con `dxcam` (Desktop Duplication, versiones fijas) y deja `capture_dda.py` al lado. Todo en una carpeta solo de administradores, porque el trabajo corre elevado y un Python de usuario (uv, `%LOCALAPPDATA%`) lo puede escribir cualquiera. Se lleva a mano: `scp` de los dos archivos a `C:\dev\vf-pkg` y `powershell -File C:\dev\vf-pkg\setup_capture.ps1` por SSH de administrador |
| `vfdev` | Mac | `run`, `submit`, `wait`, `status`, `cancel`, `busy`, `list` (a `winbridge jobs`), más `deploy`, `sync`, `build-test`, `package-test`, `cross-test`, `octo`, `stats` |
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
scripts/remote/vfdev package-test [ZIP]     # prueba el paquete (tests/package/package_tests.ps1 de esta rama) en el laptop y trae la salida entera a build/package-test/
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

## Octopath sin intervención (`octo`)

```sh
scripts/remote/vfdev octo Conditions=full,full,full Prefix=base Captures=2          # 3 lanzamientos con el preset: línea base
scripts/remote/vfdev regress --make scripts/remote/baselines/x.json "escena" 'D/base-full-b01*.csv' 'D/base-full-b02*.csv' 'D/base-full-b03*.csv'
scripts/remote/vfdev octo Conditions=factory,streaming,visual,async,full,full,async,visual,streaming,factory Prefix=fac   # ABBA por factor
scripts/remote/vfdev octo Conditions=full Prefix=reg Captures=0 Warmup=0 Leave=1    # solo abrir y cargar; el juego queda abierto
scripts/remote/vfdev run ab Api=d3d11 Process=Octopath_Traveler2-Win64-Shipping.exe Reps=3 Seconds=120 Warmup=1 Load=none LabelPrefix=reg
scripts/remote/vfdev run octo-close                                                  # cierra el juego y devuelve Engine.ini
```

Cada condición es un lanzamiento (~7 min con `Warmup=1 Captures=1`): cierra el juego, escribe su `Engine.ini`, lo abre con `steam://rungameid/1971650` (por `explorer`, así corre como el usuario y no elevado; comprueba que el padre es `steam`), espera 45 s al título, pulsa Enter con esperas de 4, 4, 4 y 50 s, y comprueba la escena: captura `dda` no negra, juego en primer plano y 15-55 FPS en 15 s de PresentMon (título y menús van a 60). Antes de cada tecla mira que la pantalla no esté bloqueada (`LogonUI`) y que el juego esté en primer plano. Si algo no cuadra (ventana que no sale en 120 s, padre que no es Steam, pantalla bloqueada, captura negra, FPS fuera de rango) guarda `<Prefix>-bNN-fail.png`, cierra el juego, devuelve `Engine.ini` y sale con 1; no reintenta.

- Condiciones (grupos del preset de `src/optimize/steam/engine_presets.cpp`, con los valores de este laptop): `factory` sin bloque `[SystemSettings]`; `streaming` = `r.ShaderPipelineCache.Enabled`, los 7 `r.Streaming.*` y `r.OneFrameThreadLag` (líneas 144 y 164-171); `visual` = los 18 recortes de calidad (172-189); `async` = `r.RDG.AsyncCompute` y `D3D12.AsyncCompute` (190-191); `full` = las 29; `keep` no toca el archivo.
- El `Engine.ini` que encuentra se guarda en `C:\ProgramData\VegaFlow\octo\Engine.orig.ini`; `octo_close.ps1` lo devuelve, compara el SHA-256 y borra la copia. Se niega si esa copia ya trae `[SystemSettings]` (no sabría cuál es el de fábrica). Aun así, copia tú el `Engine.ini` fuera de la carpeta del juego antes de empezar.
- Mide con el daemon apagado (se niega si está corriendo). Etiquetas: `<Prefix>-<cond>-bNN[cK]` cuentan; `wu-…` (calentamiento) y `chk-…` (comprobación) no, y no comparten prefijo con las que cuentan, así que `vfdev fetch <Prefix>` trae solo las buenas.
- Capturas de verificación: `C:\ProgramData\VegaFlow\screens\<Prefix>-bNN-title.png`, `-loaded.png`; `vfdev octo` las trae a `build/octo/` (o `VFJOB_OCTO_OUT`).
- SCSKiller: la línea de inicio dice si corre y si su `d3d11.dll` está en la carpeta del juego; cerrarlo es a mano (procedimiento autorizado en la memoria del proyecto).

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
- Solo `gui-keys` pulsa teclas (Tab, Enter, Esc, Espacio, flechas, Ctrl+1..8) y solo si la ventana en primer plano es la GUI de prueba; `{SPACE}` activa los botones owner-draw, Enter no. `gui` abre `--gui --observe` (sin cambios automáticos). Para ver la pantalla: `vfdev run screenshot Name=x` y `scp` del PNG. Usa Desktop Duplication (`method=dda`), que ve juegos a pantalla completa; el GDI de antes salía negro en esos casos y queda como respaldo (`method=gdi`). Sin `setup_capture.ps1` instalado, cae al respaldo.
- El MCP `vfjob` ya no existe y se quitó de la configuración de Claude Code el 2026-11-05.
