# Entrega para la sesión de WinBridge: ejecutor de trabajos remotos (antes `vfjob`)

Estado (2026-11-05): hecho. El ejecutor está en WinBridge (`docs/JOBS.md`), VegaFlow usa el paquete `vegaflow` y `vfjob` está retirado y archivado en `scripts/remote/archive/`. Lo de abajo es el documento original.

Fecha: 2026-11-04. Documento autocontenido para quien lo implemente en el repositorio de `winbridge` (`/Volumes/dev/winbridge`), sin tocar VegaFlow. Lo que dice sobre WinBridge viene de sus documentos (`docs/GATEWAY.md`, `docs/AGENT-CONTRACT.md`) y de la evaluación de VegaFlow (`docs/PLAN_REDISENO_Y_ACTUALIZACION.md` §8.2); no se ha leído su código Go para esto, así que cada afirmación sobre él debe confirmarse allí.

## 1. Qué es hoy `vfjob`

Un ejecutor de trabajos para el laptop Windows de pruebas, usado desde la Mac por un agente (Claude Code) y por línea de comandos. Está en `scripts/remote/` de VegaFlow:

- **Ejecutor en el laptop** (`runner.ps1`): lee peticiones JSON de una bandeja (`jobs\inbox`), valida el trabajo contra un manifiesto (`manifest.json`), comprueba el SHA-256 del script, valida cada parámetro con una expresión regular, lo lanza con un tiempo límite, guarda estado, salida y errores, y archiva la petición. Corre como tarea programada interactiva y elevada en la sesión del escritorio (sesión 1), con cola.
- **Carpeta protegida** `C:\ProgramData\VegaFlow` (permisos por SID: SYSTEM y Administradores control total, Usuarios solo lectura), para que un usuario normal no pueda cambiar los scripts permitidos.
- **Cliente** en la Mac: `vfjob` (bash) y `vfjob_mcp.py` (servidor MCP por stdio y CLI): `vfjob_list`, `vfjob_submit`, `vfjob_status`, `vfjob_wait`, `vfjob_run`; en CLI además `run`, `status`, `busy`, `stats`, `sync`, `build-test`.
- **Trabajos** (específicos de VegaFlow): `state`, `doctor`, `restore`, `daemon-start`, `daemon-stop`, `game`, `kill-game`, `bench`, `ab`, `bg-factors`.

## 2. Por qué moverlo

Problemas vividos en esta sesión (cada uno con su causa):

| Problema | Causa |
| --- | --- |
| La consulta de estado se colgó 30 minutos | `ssh` leía la entrada del cliente MCP (hay que cerrarla con `stdin=DEVNULL`) |
| `vfjob run` se caía con `TimeoutExpired` | un `ssh` lento mientras el laptop medía; no había reintento |
| Dos mediciones se pisaron | nada impedía lanzar un trabajo con otro en curso |
| Mediciones fallidas | los trabajos largos cerraban el juego y los siguientes no lo reabrían |
| Hubo que reconectar `/mcp` tras cada cambio | el servidor MCP carga su código al arrancar |
| El tiempo de espera de `run` era menor que un trabajo | la espera no salía del límite del trabajo |
| Ruido `#< CLIXML` en la salida | PowerShell por SSH con `-EncodedCommand` |
| Lógica duplicada en bash y Python | dos clientes sin una librería común |
| Instalar las tareas por SSH fue denegado | el clasificador de Claude Code lo trata como persistencia sin permiso; lo hizo el usuario una vez |
| `icacls` con nombres en inglés falló sin avisar | Windows en español; hay que usar SID |
| `WindowsBuiltInRole::Administrator` hay que usarlo, no el nombre | misma causa |

Además, `RunTask` de WinBridge no sirve tal cual: 60 s fijos, sin argumentos, salida máxima de 16 KB y corre sin elevar. El gateway MCP (`CallServerTool`) admite argumentos y tiempos largos pero exige un servidor MCP que corra en el laptop.

## 3. Qué se pide (requisitos)

Separar lo genérico (el ejecutor) de lo específico (los trabajos de VegaFlow):

- **R1.** Un ejecutor genérico, sin nada de VegaFlow: manifiesto de trabajos con script, hash, parámetros con regex y tiempo límite.
- **R2.** Trabajos con parámetros y tiempos largos (horas), con estado `queued`, `running`, `done`, `failed`, `timeout`, `rejected`, `cancelled`, código de salida, salida y errores.
- **R3.** Cancelar un trabajo en curso (hoy no existe; solo hay un tiempo límite).
- **R4.** Rechazar o encolar con una regla explícita si hay un trabajo en curso; que el cliente sepa cuál (id y trabajo).
- **R5.** Elevación sin abrir la puerta a comandos arbitrarios: tarea interactiva y elevada, registrada una vez por el administrador; el agente de WinBridge sigue sin elevar.
- **R6.** Ejecutar en la sesión del escritorio (sesión 1) para trabajos que abren ventanas o juegos, y poder pedir explícitamente la sesión 0 para los que no.
- **R7.** Una sola librería de cliente con CLI y MCP encima, para que no haya lógica duplicada. Tolerar tiempos de espera de `ssh` con reintentos.
- **R8.** Listar los trabajos y sus parámetros de forma que el agente pueda descubrirlos (hoy `vfjob_list`).
- **R9.** Salida limpia (sin CLIXML) y sin tope de 16 KB para los registros largos (acceso por tramos o por archivo).
- **R10.** Paquetes de trabajos instalables por separado: el de VegaFlow es un paquete que se instala sobre el ejecutor, no parte de él.
- **R11.** Actualización de scripts y manifiesto sin tareas nuevas (`update` frente a `deploy`).
- **R12.** Compatible con Windows en español y otros idiomas (SID, sin nombres de grupo).

## 4. Opciones de diseño

- **A. Dentro de WinBridge, nativo en Go.** Una tabla `[jobs.*]` en `local.toml` junto a `[tasks.*]` y `[servers.*]`, con herramientas fijas nuevas (`ListJobs`, `SubmitJob`, `JobStatus`, `WaitJob`, `CancelJob`). Ventaja: una sola instalación, una sola autenticación, el mismo perfil estricto. Riesgo: reiniciar un agente mal configurado deja sin acceso a alguien que está fuera; el agente corre sin elevar, así que ejecutaría por tarea programada.
- **B. Ejecutor independiente, WinBridge como transporte.** El ejecutor es un paquete aparte (repositorio propio) con su CLI y su servidor MCP en el laptop, y WinBridge lo expone por el gateway (`[servers.jobs]`). Ventaja: no toca el agente; se puede probar y actualizar sin reiniciar WinBridge; sirve a otros proyectos. Coste: dos piezas.
- **C. Todo en uno, nuevo producto.** Combinar ejecutor y WinBridge en algo con soporte propio. Más trabajo y más superficie.

Recomendación: **B primero**, con la interfaz del punto 5, y valorar A después si el gateway resulta incómodo. Razón: B no obliga a modificar el agente en producción, y la interfaz es la misma si luego se integra de forma nativa.

## 5. Interfaz propuesta (la misma en CLI, MCP y gateway)

| Operación | Entrada | Salida |
| --- | --- | --- |
| `list` | nada | trabajos con parámetros (regex), tiempo límite y sesión |
| `submit` | `job`, `args`, `queue` (opcional) | `id`; error `busy` con los ids en curso si no se permite cola |
| `status` | `id`, tramo del registro | estado, código, error, inicio y fin, registro |
| `wait` | `id`, `timeout_s` | estado final o `running` con el id |
| `cancel` | `id` | confirmación; mata el árbol de procesos |
| `busy` | nada | ids en curso o en cola |

Reglas: los argumentos son datos, nunca una línea de comandos; el trabajo y sus parámetros se validan en el laptop, no en el cliente.

## 6. Qué se queda en VegaFlow

- Los scripts de trabajo (`bench.ps1`, `ab_loop_task.ps1`, `bg_factors.ps1`, `game.ps1`, etc.) y sus parámetros: son el paquete de VegaFlow.
- `vfjob sync` y `vfjob build-test`: son del proyecto (rutas del repositorio y del compilador). El ejecutor genérico no los conoce.
- Las mediciones y sus documentos.

La sesión de WinBridge no debe modificar `scripts/remote/` de VegaFlow mientras se ejecuta el plan de medición (`.claude/docs/PLAN.md`): esa sesión usa el ejecutor actual y no debe quedarse sin él.

## 7. Cómo no interferir

- Instalar el nuevo ejecutor en otra carpeta (por ejemplo `C:\ProgramData\WinBridgeJobs`) y con otro nombre de tareas, no en `C:\ProgramData\VegaFlow`.
- No tocar las tareas `VegaFlow-*` ni el agente que ya funciona hasta tener el nuevo verificado.
- Migrar VegaFlow al final: instalar su paquete de trabajos sobre el nuevo ejecutor, comparar una medición real (`bg-factors` o `ab`) contra la actual, y solo entonces retirar el ejecutor viejo.

## 8. Pruebas de aceptación

1. Un trabajo con parámetro inválido (`"x; calc.exe"`) es rechazado sin lanzar nada.
2. Un script modificado después de instalarse es rechazado por el hash.
3. Un trabajo de 10 minutos termina `done` y el cliente lo espera sin caerse aunque `ssh` se agote una vez.
4. `submit` con otro trabajo en curso devuelve `busy` con su id; con `queue` lo encola y corre después.
5. `cancel` detiene el trabajo y sus procesos hijos, y el estado queda `cancelled` o equivalente.
6. Un trabajo de sesión 1 abre una ventana visible en el escritorio; uno de sesión 0 no.
7. La carpeta de scripts no es escribible por un usuario normal (comprobado con SID, no con nombres).
8. En Windows en español se instala sin errores silenciosos.
9. El cliente es el mismo para CLI y MCP: una prueba de contrato corre contra ambos.

## 9. Lo que sigue en manos del usuario

- Registrar las tareas programadas una vez (el clasificador lo deniega por SSH): una línea para ejecutar.
- Decidir A o B cuando haya alguien presente en el laptop, porque reiniciar el agente de WinBridge mal configurado deja sin acceso.
- Iniciar sesión en el laptop tras un reinicio mientras el autologon no funcione.

## 10. Preguntas abiertas para la sesión de WinBridge

- ¿El gateway (`CallServerTool`) aguanta llamadas de horas, o hay que usar `submit` y `wait`? Se probó una de 400 s.
- ¿`CallServerTool` falla con la pantalla bloqueada? Su documento dice que sí; los trabajos de sesión 0 no lo necesitarían.
- ¿Conviene que el cliente use `wb-call` (que lee la clave del Llavero) en vez de `ssh`?
- ¿El tope de 16 KB de salida afecta al registro de trabajos largos en el gateway?

## 11. Respuestas de WinBridge (2026-11-04)

Plan completo en los docs privados de winbridge: `winbridge/.claude/docs/PLAN/jobs-diseno.md` y `jobs-fases.md`.

- Gateway y horas: solo se midieron 360 y 400 s; un reinicio del agente mata la llamada. Para horas, `submit` + `wait` cortos.
- Sesión bloqueada: `CallServerTool` y `ServerTools` fallan; `RunTask` no comprueba la sesión. Esto inclina la decisión a la opción A (herramientas nativas), que WinBridge recomienda en su plan.
- `wb-call`: sí lee la clave del Llavero (`--keychain-service`); el cliente nuevo será `winbridge jobs`, sobre la misma librería.
- 16 KB es solo de `RunTask`; el gateway usa `max_output` (40000 por bloque). Los registros largos se leerán por tramos.
- Elevación: el agente y el hijo del gateway corren sin elevar; con A o B hace falta el mismo ejecutor elevado e interactivo. `deny_args` no protege `args`: solo la regex del manifiesto.
- El laptop responde hoy con el agente Python (9 herramientas, sin `Diagnose`/`RunTask`/gateway); la fase 0 del plan lo devuelve al agente Go.
