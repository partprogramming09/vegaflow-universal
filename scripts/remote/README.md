# scripts/remote: operar el laptop de pruebas desde la Mac

Herramienta **solo de desarrollo**. No forma parte de VegaFlow ni se distribuye con él; un usuario que solo quiere jugar no la necesita.

Permite lanzar, desde la Mac, un conjunto cerrado de trabajos en la sesión del escritorio del laptop de pruebas (abrir `drawstress`, medir con PresentMon, arrancar o parar el daemon, hacer el A/B). La sesión SSH sola no sirve para esto: corre en la sesión 0 de Windows, sin escritorio, y desde ahí PresentMon no ve cuadros y el detector del daemon no ve ningún juego.

## Piezas

| Archivo | Dónde corre | Qué hace |
| --- | --- | --- |
| `runner.ps1` | laptop, sesión 1, elevado (tarea `VegaFlow-Run`) | Procesa `jobs\inbox\*.json` y ejecuta solo trabajos de `manifest.json` |
| `install_runner.ps1` | laptop | Copia todo a `C:\ProgramData\VegaFlow`, escribe el manifiesto con hashes y registra las tareas |
| `jobs/*.ps1` | laptop | Los trabajos: `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game` |
| `vfjob` | Mac | CLI: `deploy`, `update`, `run JOB Clave=valor`, `status ID` |
| `vfjob_mcp.py` | Mac | Servidor MCP con `vfjob_list`, `vfjob_submit`, `vfjob_status`, `vfjob_wait`, `vfjob_run` |

`bench` y `ab` salen de `scripts/bench.ps1` y `scripts/ab_loop_task.ps1`.

## Uso

```sh
scripts/remote/vfjob deploy                 # primera vez: instala y registra las tareas
scripts/remote/vfjob update                 # después: actualiza archivos y manifiesto, sin tareas
scripts/remote/vfjob run doctor
scripts/remote/vfjob run game Api=d3d11 Dim=30
scripts/remote/vfjob run ab Api=d3d11 Reps=5 Seconds=30 LabelPrefix=run1 Load=chrome
claude mcp add --scope user vfjob -- python3 -I "$PWD/scripts/remote/vfjob_mcp.py"
```

```bash
scripts/remote/vfjob sync        # lleva los commits de la rama actual al repositorio del laptop (bundle + fast-forward)
scripts/remote/vfjob build-test  # compila y ejecuta las pruebas en el laptop; sale con error si alguna falla
```

```bash
scripts/remote/vfjob stats ARCHIVO [PREFIJO]   # agrupa filas de resultados por condicion: n, media y rango (usa - para leer de la entrada)
```

`vfjob run` espera por defecto el limite del trabajo (manifiesto) mas un minuto; `VFJOB_WAIT` lo fija a mano.

`ab` y `bg-factors` abren `drawstress` solos si no esta abierto. Un trabajo nuevo (y `build-test`) se rechaza mientras otro corre o espera en la bandeja; `VFJOB_QUEUE=1` (o `queue: true` en el MCP) lo encola igualmente.

Variables: `VFJOB_HOST` (usuario@ip), `VFJOB_KEY` (llave SSH), `VFJOB_WAIT` (segundos de espera de `vfjob run`), `VFJOB_REPO` (ruta del repositorio en el laptop), `VFJOB_MINGW` (carpeta `bin` de g++), `VFJOB_BASE` (rama base del bundle, `main`), `VFJOB_QUEUE=1`.

## Seguridad

- El ejecutor corre elevado. Solo ejecuta scripts de `C:\ProgramData\VegaFlow\jobs\scripts`, que solo administradores pueden escribir, y comprueba el SHA-256 de cada uno contra el manifiesto. Los parámetros se validan con una expresión regular y se pasan con nombre, nunca como comando.
- Cambiar qué trabajos se permiten exige `vfjob update` con la llave SSH de un administrador; el MCP no lo expone.
- La carpeta protegida se fija con SID (`*S-1-5-32-544`, `*S-1-5-18`, `*S-1-5-32-545`): los nombres en texto fallan en un Windows en español. `doctor` informa `carpeta_escribible_por_usuarios`.
- PresentMon y `drawstress` se copian desde una carpeta del usuario sin verificar su firma; sus hashes quedan en `setup.log`.
- Para desactivarlo sin desinstalar: `Disable-ScheduledTask -TaskName VegaFlow-Run`. Para quitarlo: `Unregister-ScheduledTask` de las cinco tareas `VegaFlow-*` y borrar `C:\ProgramData\VegaFlow`.

## Límites

- Un trabajo a la vez: uno largo, como `ab`, bloquea la cola.
- Tras un reinicio hace falta iniciar sesión en el laptop; el inicio automático no funciona con la cuenta de Microsoft actual.
- No ve la pantalla ni pulsa teclas (eso lo hace WinBridge).
- La lógica de enviar y leer trabajos está duplicada en `vfjob` (bash) y `vfjob_mcp.py` (Python).
