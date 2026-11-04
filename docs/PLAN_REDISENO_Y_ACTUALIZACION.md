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
| Los efectos de proceso (CPU Sets, EcoQoS, prioridad I/O) solo alcanzan procesos que el detector clasifica como juego, y el detector solo ve el escritorio de su propia sesión | Verificado | `--once --dry-run` con `drawstress.exe` abierto: desde la sesión 0 no detecta nada; desde la sesión del escritorio detecta `drawstress.exe` |
| `Tasks\Games` (MMCSS) solo afecta a hilos que se registran como "Games"; un motor que no lo hace no recibe nada | Por verificar | Mecanismo documentado de MMCSS, sin medición propia |
| El daemon continuo aplica GameDVR/FSE y el perfil de energía APU al arrancar y, al cerrar, solo restaura si había un juego activo | Verificado por lectura de código | `main.cpp`: llamadas al arrancar frente al bloque de cierre condicionado a `game_active` |
| La línea base nunca fue un Windows de fábrica: siguen puestos `UMD\Tessellation=16`, `TurboSync`, `FlipQueueSize`, `TFQ`, `Main3D_DEF` y 13 entradas de `UserGpuPreferences` | Verificado | Informe de `VegaFlow-State` del 2026-11-04 |
| Esos valores del driver no tienen valor de fábrica confirmado, así que `VegaFlow-Restore` no los toca | Verificado | `tweaks_state.ps1` los marca "solo informe" |
| WinBridge no puede pulsar un UAC, pero la sesión SSH del laptop ya corre elevada y no lo necesita | Verificado | `Diagnose` con `input_desktop: secure`; `whoami /groups` por SSH: `High Mandatory Level` |
| La suite de pruebas ejecuta cambios reales (modos "Exec") y deja el sistema modificado: tras una corrida, `UMD\Tessellation` pasó de 16 a 64, `TurboSync`, `TFQ` y `Main3D_DEF` a 0 | Verificado | `VegaFlow-State` antes y después de `build.ps1 -RunTests` el 2026-11-04 |
| `GameConfigStore` (`GameDVR_Enabled=0`, `GameDVR_FSEBehaviorMode=2`) queda aplicado y `tweaks_state.ps1` no lo vigila | Verificado | Lectura del registro el 2026-11-04 |
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

Estado: los pasos 1 a 3 se pueden hacer hoy. El paso 4 también puede correr por SSH, que ya está elevado, sin registrar tareas: queda como mejora opcional una tarea `VegaFlow-Update` si se quiere ejecutar sin sesión SSH.

## 5. Fases y estado

1. Instrumento. Hecho: `perf_record` con `drawstress` solo da medias (`drawstress-log`) y un proceso lanzado desde SSH corre en la sesión 0, donde PresentMon no ve presents. Se usa `scripts/bench.ps1` con el juego lanzado en el escritorio (WinBridge, `Win+R`).
2. A/B intercalado de `drawstress` (D3D11 y D3D12). Hecho: `docs/MEDICION_AB_DAEMON.md`.
3. Auditoría de `CLAUDE.md`. Parcial: enlaces corregidos; las afirmaciones de rendimiento siguen sin medición propia (sección 6).
4. Reorganización: retirar o apagar lo que no se demuestre. Pendiente.
5. Método de actualización por paquete (sección 4). Pendiente.

## 6. Hallazgos de la sesión del 2026-11-04 y trabajo pendiente

Corregido y verificado:

- `CPMINCORES=100` del perfil APU bajaba el FPS de `drawstress` D3D11 un 21% por sí solo; con el daemon completo el coste era del 10.7%. El perfil ahora deja el valor de fábrica y el A/B posterior muestra las partes globales del daemon encendido iguales a apagado en D3D11 y D3D12 (`842df5b`); las optimizaciones por proceso no se midieron (ver punto 5).
- El cierre limpio del daemon en modo continuo no restauraba el perfil de energía si no había juego activo; ahora lo restaura siempre.
- El daemon aplicaba dos veces `SetGameDvrAndFseOptimized` y `SetApuLaptopPowerProfile` al arrancar; se quitó el duplicado.
- `tweaks_state.ps1` no vigilaba el plan de energía ni `GameConfigStore`: ahora informa `CPMINCORES`, `PERFEPP`, `PERFBOOSTMODE`, `ASPM` y los valores `GameDVR_*`.
- `CLAUDE.md` tenía 20 enlaces absolutos al proyecto original; ahora son relativos y todos resuelven.

Pendiente, por prioridad:

1. Las pruebas ejecutan cambios reales y "restauran" a valores por defecto, no a los previos (`UMD\Tessellation` quedó en 64 tras correrlas). Cambiarlas a simulación o a guardar y restaurar el valor previo.
2. Las demás partes del perfil APU (EPP 25, boost 4, ASPM 0) no tienen efecto medido en `drawstress`; medirlas en una carga con presupuesto térmico compartido con la iGPU (un juego real o un benchmark de GPU) antes de mantenerlas.
3. `GameDVR_Enabled=0` y `GameDVR_FSEBehaviorMode=2` se escriben sin guardar el valor previo y no se restauran al salir. Guardar el valor previo, como el plan de energía.
4. Resuelto: la detección funciona. El daemon no vio `drawstress.exe` en el A/B porque se lanzó desde SSH (sesión 0, sin escritorio). Comprobado con `--once --dry-run`: desde la sesión del escritorio detecta `drawstress.exe` (DirectX 11), 18 procesos de fondo y 8 núcleos. Mejora pendiente: que el daemon avise por log si arranca en una sesión sin escritorio, para no medirlo sin darse cuenta.
5. Ninguna optimización por proceso (CPU Sets, EcoQoS, prioridad de I/O) tiene todavía una medición propia. Requiere el daemon elevado en la sesión del escritorio: ejecutar una vez `scripts\setup_tasks.ps1` como administrador (un UAC) y arrancar `VegaFlow-Start` desde SSH; después repetir el A/B con `scripts/ab_loop.ps1` adaptado a esa tarea.
6. `src/main.cpp` (1925 líneas) y `src/optimizers/graphics_optimizer.cpp` (2009 líneas) concentran demasiada lógica; dividir con las pruebas actuales como red de seguridad.
7. Medir otras cargas con percentiles: Godot (`godot-benchmarks`) y la muestra `nBodyGravity` (DirectX-Graphics-Samples) para AsyncCompute.
8. Registrar en `CLAUDE.md` que una sesión SSH corre en la sesión 0 y que las mediciones de PresentMon requieren lanzar la carga en el escritorio.

## 7. Preparación para operar a distancia

Estado del laptop leído el 2026-11-04 (solo lectura salvo lo indicado):

| Punto | Estado |
| --- | --- |
| `sshd` y Tailscale | Servicios automáticos, corriendo. No verificado que respondan antes de iniciar sesión |
| Suspensión por inactividad y apagado de pantalla en corriente alterna | Nunca |
| Tapa en corriente alterna | Cambiada de 1 (suspender) a 0 (no hacer nada) el 2026-11-04; valor anterior 1. Verificado con `powercfg /qh` |
| Inicio de sesión automático | No configurado. La cuenta es de Microsoft y `DevicePasswordLessBuildVersion=2` (solo Windows Hello), lo que oculta la casilla de `netplwiz`. Lo debe configurar el usuario en el laptop: tiene que escribir su contraseña, que no pasa por el asistente |
| Reinicio pendiente de Windows Update | No. 183 GB libres |

Pasos del usuario para el inicio de sesión automático (unos 3 minutos, en el laptop):

1. Configuración, Cuentas, Opciones de inicio de sesión: desactivar "Para mejorar la seguridad, permitir solo el inicio de sesión con Windows Hello para cuentas Microsoft en este dispositivo".
2. `Win+R`, `netplwiz`: desmarcar "Los usuarios deben escribir su nombre y contraseña", aplicar y escribir la contraseña.
3. Verificación posterior sin leer la contraseña: `AutoAdminLogon=1` y `DefaultUserName=testuser`, y un reinicio de prueba por SSH para comprobar que vuelven Tailscale, SSH, la sesión del escritorio y `winbridge-mcp`.

Lista de comprobaciones previas a cada trabajo automático: Tailscale y SSH arriba, sesión iniciada y desbloqueada, cargador conectado y batería sobre el mínimo, disco libre, sin `drawstress` ni daemon sobrantes, `tweaks_state.ps1` sin valores fuera de fábrica.

Resultado del reinicio de prueba del 2026-11-04 (arranque 09:44:55):

- `tailscaled` y `sshd` arrancaron a las 09:45:06, sin sesión de usuario. Aun así el puerto 22 solo respondió a las 09:48:37, justo después de que apareciera el escritorio (`explorer` 09:48:13, `tailscale-ipn` 09:48:29). La configuración de Tailscale no tenía `ForceDaemon`: no estaba en modo desatendido y no se conectaba hasta iniciar sesión.
- Corregido con `tailscale set --unattended` (Tailscale 1.104.4); `tailscale debug prefs` muestra `ForceDaemon: true`. Falta un segundo reinicio sin intervención para comprobar que SSH vuelve sin sesión de usuario.
- El escritorio apareció 3 min 18 s después del arranque. Sin determinar si el inicio de sesión automático lo hizo solo o lo hizo el usuario.

Segundo reinicio de prueba, sin intervención (arranque 09:55:31):

- Tailscale desatendido verificado: SSH respondió a las 09:55:58, 27 s después del arranque y sin ninguna sesión de usuario. Antes tardaba hasta iniciar sesión (4 min 18 s).
- Inicio de sesión automático fallido: a las 09:55:42 Winlogon lo intentó y el evento 4625 (tipo 2, proceso User32) registró estado `0xC000006D` y subestado `0xC000023C` (red inaccesible). La pantalla mostró "No podemos conectarnos en este momento" y quedó en el inicio de sesión normal; Windows no reintenta. La configuración es correcta (`AutoAdminLogon=1`, `DefaultUserName=testuser`, secreto de LSA presente).
- Hipótesis sin verificar: la cuenta es de Microsoft y siempre se usó con huella o PIN, así que quizá el equipo no tiene verificador de contraseña en caché y necesita red para validarla; a los 11 s del arranque el Wi-Fi aún no está conectado. Siguiente prueba: iniciar sesión una vez con la contraseña (no con huella ni PIN) y repetir el reinicio.
- Si falla: cuenta local en lugar de la de Microsoft, o aceptar un inicio de sesión físico tras cada reinicio y evitar reinicios (pausar Windows Update).

## 8. Ejecutor remoto de trabajos (`scripts/remote/`)

Objetivo: operar el laptop desde la Mac sin estar delante, incluida la sesión del escritorio (donde el detector del daemon, PresentMon y los juegos sí funcionan) y sin un UAC por cada acción.

- `runner.ps1` corre en la sesión 1 y elevado (tarea `VegaFlow-Run`), procesa `jobs\inbox\*.json` y solo ejecuta trabajos de `manifest.json`, comprobando el SHA-256 del script y validando cada parámetro con su expresión regular. Los parámetros se pasan con nombre, nunca como texto de comando.
- Trabajos: `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bench`, `ab`.
- `install_runner.ps1` copia todo a `C:\ProgramData\VegaFlow` (solo administradores escriben), incluidos PresentMon y drawstress, y registra las tareas. `-SkipTasks` solo actualiza archivos y manifiesto.
- `scripts/remote/vfjob` (Mac): `deploy`, `update`, `run JOB Clave=valor`, `status ID`.
- Estado: archivos preparados en `C:\dev\vf-deploy` del laptop, con sintaxis verificada. Falta registrar las tareas, que es una persistencia con privilegios y el sistema de permisos de Claude Code la bloqueó. La instalación la hace el usuario con un clic de UAC o añadiendo una regla de permiso para `scripts/remote/vfjob deploy`.
- Riesgo asumido: PresentMon y drawstress se copian de una carpeta del usuario sin verificar su firma; se registra su hash en `setup.log` y conviene contrastarlo con la versión publicada.

### 8.1. MCP `vfjob` (`scripts/remote/vfjob_mcp.py`)

Servidor MCP de la Mac, solo Python estándar, que envuelve el ejecutor remoto: `vfjob_list`, `vfjob_submit`, `vfjob_status`, `vfjob_wait` y `vfjob_run`. Solo expone los trabajos del manifiesto; no ejecuta comandos arbitrarios ni expone el instalador, así que no puede cambiar qué scripts se permiten (eso queda en `vfjob update` por CLI).

- Registro: `claude mcp add --scope user vfjob -- python3 -I /Volumes/dev/vegaflow-universal/scripts/remote/vfjob_mcp.py`. Tras añadirlo hay que reconectar con `/mcp` o reiniciar la sesión para que aparezcan las herramientas.
- Autocomprobación sin el laptop: `python3 -I scripts/remote/vfjob_mcp.py --self-test`. Verificado: protocolo, validación de nombres e ids, y `vfjob_list` contra el laptop.
- Verificado de extremo a extremo el 2026-11-04 desde las herramientas del MCP: `vfjob_submit` más `vfjob_wait` con `doctor` terminó en `done` con `exit_code` 0 en 1.2 s, y `vfjob_run` con `bench` y `Process="x; calc.exe"` fue rechazado por el ejecutor ("valor no valido para Process") sin lanzar nada.
- Duplicación conocida: la lógica de enviar y leer trabajos está en `vfjob` (bash) y en `vfjob_mcp.py` (Python); conviene unificarla.

## 9. Medición de las optimizaciones por proceso (2026-11-04)

Detalle en `docs/MEDICION_AB_DAEMON.md`.

- Hallazgo: confinar al juego a 8 de los 12 hilos (CPU Sets) bajaba `drawstress` D3D11 un 19% a 24%. La prioridad del planificador (`Win32PrioritySeparation`) no tenía efecto.
- Corregido (`bcf360c`): `restrict_game_cpu_sets` apagado por defecto. Con el juego sin restringir, el daemon encendido iguala al apagado en D3D11 (120.4 contra 120.0 FPS) y en D3D12 no había efecto.
- Estado de las promesas del proyecto: ninguna optimización de VegaFlow tiene todavía una ganancia demostrada en `drawstress`; el perfil de energía y el confinamiento del juego tenían un coste medible, ya eliminado. El beneficio del aislamiento de fondo (EcoQoS, prioridad de I/O) no se midió: requiere una carga de fondo real.
- Siguiente medición útil: `drawstress` con una carga de fondo (`7z b`, navegador, Discord) y el daemon apagado contra encendido, para ver si el aislamiento de fondo protege al juego.
